const { InstanceBase, InstanceStatus, runEntrypoint } = require('@companion-module/base')
const UpgradeScripts = require('./upgrades')
const { getConfigFields } = require('./config')
const { ShowComState } = require('./state')
const { buildActions } = require('./actions')
const { buildFeedbacks } = require('./feedbacks')
const { buildVariableDefinitions } = require('./variables')
const { buildPresets } = require('./presets')
const { fromLegacy } = require('./legacy')

/**
 * Bitfocus Companion – ShowCom
 *
 * Fragt ShowCom im lokalen Netz ab (GET /api/companion/state) und stellt Namens-Trigger,
 * Alarm, Sprecher und Funkdaten als Variablen, Feedbacks und Presets bereit.
 * Befehle (Alarm quittieren, Zuhören an/aus …) gehen per POST /api/companion/command zurück.
 */
class ShowComInstance extends InstanceBase {
	constructor(internal) {
		super(internal)
		this.state = new ShowComState()
		this.pollTimer = null
		this.tickTimer = null
		this.running = false
		this.lastStatus = ''
		this.definitionsKey = ''
	}

	async init(config) {
		this.config = config
		this.applyConfig()
		this.updateDefinitions(true)
		this.start()
	}

	async destroy() {
		this.stop()
	}

	async configUpdated(config) {
		// Nur die gemerkte Geräte-ID (ältere ShowCom-Versionen) hat sich geändert: nichts neu starten
		const strip = (c) => JSON.stringify({ ...c, legacyDevice: undefined })
		if (this.config && strip(this.config) === strip(config)) {
			this.config = config
			return
		}
		this.config = config
		this.legacy = false
		this.stop()
		this.state = new ShowComState()
		this.applyConfig()
		this.updateDefinitions(true)
		this.start()
	}

	getConfigFields() {
		return getConfigFields()
	}

	// --- Verbindung ---------------------------------------------------------------

	baseUrl() {
		const host = String(this.config.host || '127.0.0.1').trim()
		const port = Number(this.config.port) || 8080
		return `http://${host.includes(':') && !host.startsWith('[') ? `[${host}]` : host}:${port}`
	}

	headers() {
		const h = { 'Content-Type': 'application/json' }
		const key = String(this.config.key || '').trim()
		if (key) h['X-ShowCom-Key'] = key
		return h
	}

	applyConfig() {
		this.state.configure({
			triggers: this.config.triggers,
			autoPersons: this.config.autoPersons !== false,
			nicknames: this.config.nicknames !== false,
			fuzzy: this.config.fuzzy !== false,
			useLive: this.config.useLive !== false,
			ignoreSelf: this.config.ignoreSelf !== false,
			holdSeconds: Number(this.config.holdSeconds) || 0,
			lowBattery: Number(this.config.lowBattery) || 25,
			weakSignal: Number(this.config.weakSignal) || 20,
		})
	}

	start() {
		this.running = true
		this.setStatus(InstanceStatus.Connecting, 'Verbinde …')
		this.poll()
		// Zeitabhängige Feedbacks (Haltezeit, "in den letzten N Sekunden") regelmäßig prüfen
		this.tickTimer = setInterval(() => {
			if (this.state.tick(Date.now())) this.publish()
			this.checkFeedbacks('mention', 'heard')
		}, 1000)
	}

	stop() {
		this.running = false
		if (this.pollTimer) clearTimeout(this.pollTimer)
		if (this.tickTimer) clearInterval(this.tickTimer)
		this.pollTimer = null
		this.tickTimer = null
	}

	setStatus(status, message) {
		const key = `${status}|${message ?? ''}`
		if (key === this.lastStatus) return
		this.lastStatus = key
		this.updateStatus(status, message)
	}

	async poll() {
		if (!this.running) return
		const interval = Math.max(100, Math.min(5000, Number(this.config.pollInterval) || 250))
		let delay = interval

		try {
			let data = null
			if (!this.legacy) {
				const since = this.state.since()
				const res = await fetch(`${this.baseUrl()}/api/companion/state?since=${since}`, {
					headers: this.headers(),
					signal: AbortSignal.timeout(2500),
				})

				if (res.status === 404) {
					// ShowCom vor 0.10: über die normale Browser-Schnittstelle weiter
					this.legacy = true
					this.log('info', 'ShowCom ohne Companion-Schnittstelle (vor 0.10) – nutze die Browser-Schnittstelle')
				} else if (res.status === 403) {
					const reason = await res.json().catch(() => '')
					this.setStatus(
						InstanceStatus.BadConfig,
						reason === 'wrong key'
							? 'Schlüssel falsch (ShowCom → Einstellungen → Bitfocus Companion)'
							: 'In ShowCom ist der Companion-Zugriff ausgeschaltet',
					)
					this.markDisconnected()
					delay = 3000
				} else if (!res.ok) {
					this.setStatus(InstanceStatus.UnknownError, `ShowCom antwortet mit HTTP ${res.status}`)
					this.markDisconnected()
					delay = 3000
				} else {
					data = await res.json()
					if (data?.app !== 'ShowCom') throw new Error('Antwort kommt nicht von ShowCom (Port richtig?)')
					this.setStatus(InstanceStatus.Ok)
				}
			}

			if (this.legacy) {
				data = await this.pollLegacy()
				if (data)
					this.setStatus(InstanceStatus.Ok, 'Ältere ShowCom-Version: Trigger, Sprecher und Funk gehen; Alarm und Steuerung ab ShowCom 0.10')
			}

			if (data) {
				const changed = this.state.apply(data, Date.now())
				this.updateDefinitions(false)
				this.publish(changed)
			}
		} catch (e) {
			const code = e?.cause?.code || e?.name || ''
			const why =
				code === 'ECONNREFUSED'
					? 'Verbindung abgelehnt – läuft ShowCom, ist der Webzugriff an, stimmt der Port?'
					: code === 'TimeoutError' || code === 'ETIMEDOUT' || code === 'EHOSTUNREACH'
						? 'keine Antwort – IP richtig, gleiches Netz, Firewall?'
						: code === 'ENOTFOUND'
							? 'Rechnername unbekannt – IP-Adresse eintragen'
							: e?.message || 'unbekannter Fehler'
			this.setStatus(InstanceStatus.ConnectionFailure, `${this.baseUrl()}: ${why}`)
			this.log('debug', `Verbindung: ${code} ${e?.message ?? ''}`)
			this.markDisconnected()
			delay = 2000
		}

		if (this.running) this.pollTimer = setTimeout(() => this.poll(), delay)
	}

	/** Ältere ShowCom-Versionen: als eigenes "Gerät" an der Browser-Schnittstelle anmelden und lesen. */
	async pollLegacy() {
		let id = String(this.config.legacyDevice || '')
		if (id) {
			const res = await fetch(`${this.baseUrl()}/api/d/${id}/state`, { signal: AbortSignal.timeout(2500) })
			if (res.ok) return fromLegacy(await res.json())
			if (res.status !== 404) throw new Error(`HTTP ${res.status}`)
		}

		// Noch kein Gerät (oder am Host entfernt): neu anmelden und die ID merken
		const reg = await fetch(`${this.baseUrl()}/api/register`, {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify({ name: 'Bitfocus Companion' }),
			signal: AbortSignal.timeout(2500),
		})
		if (!reg.ok) throw new Error(`Anmeldung bei ShowCom fehlgeschlagen (HTTP ${reg.status})`)
		id = String((await reg.json())?.id || '')
		if (!id) throw new Error('Antwort kommt nicht von ShowCom (Port richtig?)')
		this.config = { ...this.config, legacyDevice: id }
		this.saveConfig(this.config)
		return null
	}

	markDisconnected() {
		if (this.state.setConnected(false)) this.publish(true)
	}

	async sendCommand(cmd) {
		if (this.legacy) {
			this.log('warn', `"${cmd}" geht erst ab ShowCom 0.10 – bitte ShowCom aktualisieren`)
			return
		}
		try {
			const res = await fetch(`${this.baseUrl()}/api/companion/command`, {
				method: 'POST',
				headers: this.headers(),
				body: JSON.stringify({ cmd }),
				signal: AbortSignal.timeout(2500),
			})
			if (res.status === 403) this.log('warn', `ShowCom lehnt "${cmd}" ab – Steuerung in ShowCom erlaubt?`)
			else if (!res.ok) this.log('warn', `ShowCom: Befehl "${cmd}" fehlgeschlagen (HTTP ${res.status})`)
		} catch (e) {
			this.log('warn', `ShowCom nicht erreichbar: ${e.message}`)
		}
	}

	// --- Companion aktualisieren ------------------------------------------------------

	/** Definitionen neu senden, wenn sich Personen oder Trigger geändert haben. */
	updateDefinitions(force) {
		const key = this.state.definitionsKey()
		if (!force && key === this.definitionsKey) return
		this.definitionsKey = key

		this.setActionDefinitions(buildActions(this))
		this.setFeedbackDefinitions(buildFeedbacks(this))
		this.setVariableDefinitions(buildVariableDefinitions(this.state))
		this.setPresetDefinitions(buildPresets(this.state, this.label || "showcom"))
		this.publish(true)
	}

	publish(changed = true) {
		const values = this.state.variableValues()
		const diff = {}
		let any = false
		this.varCache = this.varCache || {}
		for (const [k, v] of Object.entries(values)) {
			if (this.varCache[k] !== v) {
				diff[k] = v
				this.varCache[k] = v
				any = true
			}
		}
		if (any) this.setVariableValues(diff)
		if (changed || any) this.checkFeedbacks()
	}
}

module.exports = ShowComInstance

// Für die Tests ohne Companion abschaltbar
if (process.env.SHOWCOM_NO_ENTRYPOINT !== '1') {
	runEntrypoint(ShowComInstance, UpgradeScripts)
}
