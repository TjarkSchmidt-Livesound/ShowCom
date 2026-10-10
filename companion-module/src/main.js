const { InstanceBase, InstanceStatus, runEntrypoint } = require('@companion-module/base')
const UpgradeScripts = require('./upgrades')
const { getConfigFields } = require('./config')
const { ShowComState } = require('./state')
const { buildActions } = require('./actions')
const { buildFeedbacks } = require('./feedbacks')
const { buildVariableDefinitions } = require('./variables')
const { buildPresets } = require('./presets')

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
		this.config = config
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
			const since = this.state.since()
			const res = await fetch(`${this.baseUrl()}/api/companion/state?since=${since}`, {
				headers: this.headers(),
				signal: AbortSignal.timeout(2500),
			})

			if (res.status === 403) {
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
				this.setStatus(InstanceStatus.UnknownError, `HTTP ${res.status} – ShowCom zu alt? (ab Version 0.10)`)
				this.markDisconnected()
				delay = 3000
			} else {
				const data = await res.json()
				if (data?.app !== 'ShowCom') throw new Error('Keine ShowCom-Antwort')
				this.setStatus(InstanceStatus.Ok)
				const changed = this.state.apply(data, Date.now())
				this.updateDefinitions(false)
				this.publish(changed)
			}
		} catch (e) {
			this.setStatus(InstanceStatus.ConnectionFailure, `ShowCom nicht erreichbar (${this.baseUrl()}) – Webzugriff an?`)
			this.markDisconnected()
			delay = 2000
		}

		if (this.running) this.pollTimer = setTimeout(() => this.poll(), delay)
	}

	markDisconnected() {
		if (this.state.setConnected(false)) this.publish(true)
	}

	async sendCommand(cmd) {
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
