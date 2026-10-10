const { Matcher, splitList, expandNicknames, slugify, normalise } = require('./matcher')

const RECENT_MS = 10 * 60 * 1000 // für "in den letzten N Sekunden gehört"
const LIVE_MEMORY_MS = 60 * 1000 // Live-Treffer nicht doppelt zählen, wenn der fertige Text kommt

/**
 * Der gesamte Zustand des Moduls – ohne Abhängigkeit von Companion, damit er testbar ist.
 */
class ShowComState {
	constructor() {
		this.options = {
			triggers: '',
			autoPersons: true,
			nicknames: true,
			fuzzy: true,
			useLive: true,
			ignoreSelf: true,
			holdSeconds: 0,
			lowBattery: 25,
			weakSignal: 20,
		}
		this.connected = false
		this.data = null
		this.persons = []
		this.personSlugs = new Map() // id -> slug
		this.triggers = [] // siehe rebuildTriggers()
		this.triggerState = new Map() // id -> Zähler usw. (überlebt Neuaufbau)
		this.lastEntryId = null
		this.liveFired = new Map()
		this.recent = [] // fertige Sätze: { ms, user, name, text }
		this.lastEntry = { name: '', text: '', time: '' }
		this.lastTrigger = { label: '', by: '', text: '', time: '' }
		this.personLast = new Map() // id -> { text, time }
		this.totalHits = 0
		this.summaryKey = ''
		this.personsKey = ''
		this.heardCache = new Map()
	}

	configure(options) {
		Object.assign(this.options, options)
		this.heardCache.clear()
		this.rebuildTriggers()
	}

	setConnected(c) {
		if (this.connected === c) return false
		this.connected = c
		return true
	}

	since() {
		return this.lastEntryId ?? 0
	}

	// --- Trigger ----------------------------------------------------------------------

	rebuildTriggers() {
		const defs = []
		const seen = new Set()
		const add = (label, words, personId) => {
			const norm = normalise(label)
			if (!label || seen.has(norm)) return
			seen.add(norm)
			let id = slugify(label, 'trigger')
			while (defs.some((d) => d.id === id)) id += '_2'
			const all = this.options.nicknames ? expandNicknames(words, this.persons) : words
			defs.push({ id, label, words: all, personId, matcher: new Matcher(all, this.options.fuzzy) })
		}

		// Eigene Trigger aus der Konfiguration: "Tjark, Licht|LD, Ton"
		for (const item of String(this.options.triggers ?? '').split(/[,;\n\r]/)) {
			const alts = splitList(item)
			if (alts.length > 0) add(alts[0], alts, this.personIdForName(alts[0]))
		}

		// Automatisch: ein Trigger pro Person in ShowCom
		if (this.options.autoPersons) {
			for (const p of this.persons) if (p.name) add(p.name, [p.name], p.id)
		}

		this.triggers = defs
		for (const t of defs) {
			if (!this.triggerState.has(t.id)) {
				this.triggerState.set(t.id, { count: 0, by: '', byId: '', text: '', time: '', lastHitMs: 0, active: false })
			}
		}
	}

	personIdForName(name) {
		const n = normalise(name)
		const p = this.persons.find(
			(x) => normalise(x.name) === n || splitList(x.nickname).some((k) => normalise(k) === n),
		)
		return p ? p.id : ''
	}

	trigger(id) {
		const t = this.triggers.find((x) => x.id === id)
		return t ? { ...t, ...this.triggerState.get(t.id) } : null
	}

	isTriggerActive(id) {
		if (id === '*') return this.triggers.some((t) => this.triggerState.get(t.id)?.active)
		return !!this.triggerState.get(id)?.active
	}

	fire(t, e, now) {
		const s = this.triggerState.get(t.id)
		s.count++
		s.by = e.name || ''
		s.byId = e.user || ''
		s.text = e.text || ''
		s.time = e.time || ''
		s.lastHitMs = now
		s.active = true
		this.totalHits++
		this.lastTrigger = { label: t.label, by: s.by, text: s.text, time: s.time }
	}

	/** Trigger zurücksetzen ("*" = alle). Mit counts=true auch die Zähler. */
	reset(id, counts = false) {
		let changed = false
		for (const [tid, s] of this.triggerState) {
			if (id !== '*' && tid !== id) continue
			if (s.active || (counts && s.count > 0)) changed = true
			s.active = false
			if (counts) {
				s.count = 0
				s.by = s.byId = s.text = s.time = ''
			}
		}
		if (counts && id === '*') {
			this.totalHits = 0
			this.lastTrigger = { label: '', by: '', text: '', time: '' }
		}
		return changed
	}

	/** Für eigene Tests per Aktion: Trigger auslösen, als hätte jemand den Namen gesagt. */
	simulate(id, now) {
		const t = this.triggers.find((x) => x.id === id)
		if (!t) return false
		this.fire(t, { name: 'Test', user: '', text: `${t.label} (Test aus Companion)`, time: clock(now) }, now)
		return true
	}

	// --- Daten von ShowCom ------------------------------------------------------------

	/** Antwort von /api/companion/state übernehmen. Liefert true, wenn sich etwas geändert hat. */
	apply(data, now) {
		let changed = this.setConnected(true)
		this.data = data

		// Personen
		const persons = Array.isArray(data.persons) ? data.persons : []
		const personsKey = JSON.stringify(persons.map((p) => [p.id, p.name, p.nickname]))
		this.persons = persons
		if (personsKey !== this.personsKey) {
			this.personsKey = personsKey
			this.assignSlugs()
			this.rebuildTriggers()
			changed = true
		}

		// Neue fertige Sätze
		const last = Number(data.lastEntryId) || 0
		if (this.lastEntryId === null || last < this.lastEntryId) {
			// Erster Kontakt oder ShowCom neu gestartet: alte Sätze nicht als neue Erwähnungen werten
			this.lastEntryId = last
		} else {
			const entries = (Array.isArray(data.entries) ? data.entries : [])
				.filter((e) => Number(e.id) > this.lastEntryId)
				.sort((a, b) => a.id - b.id)
			for (const e of entries) {
				this.processEntry(e, false, now)
				changed = true
			}
			this.lastEntryId = last
		}

		// Live-Text (während gesprochen wird)
		if (this.options.useLive && Array.isArray(data.live)) {
			for (const e of data.live) if (this.processEntry(e, true, now)) changed = true
		}

		const summary = JSON.stringify([
			data.listening,
			data.control,
			data.alarm,
			data.mentionCount,
			persons.map((p) => [p.speaking, p.rf, p.iem, p.level > 5]),
		])
		if (summary !== this.summaryKey) {
			this.summaryKey = summary
			changed = true
		}
		return changed
	}

	processEntry(e, live, now) {
		let fired = false
		for (const t of this.triggers) {
			if (this.options.ignoreSelf && t.personId && t.personId === e.user) continue
			if (!t.matcher.matches(e.text)) continue

			const key = `${t.id}|${e.user}`
			const prior = this.liveFired.get(key)
			if (live) {
				if (prior && now - prior < LIVE_MEMORY_MS) continue
				this.liveFired.set(key, now)
				this.fire(t, e, now)
				fired = true
			} else if (prior && now - prior < LIVE_MEMORY_MS) {
				// Schon beim Sprechen ausgelöst: nur den fertigen Text übernehmen
				this.liveFired.delete(key)
				const s = this.triggerState.get(t.id)
				s.text = e.text
				if (this.lastTrigger.label === t.label) this.lastTrigger.text = e.text
			} else {
				this.fire(t, e, now)
				fired = true
			}
		}

		if (!live) {
			this.lastEntry = { name: e.name || '', text: e.text || '', time: e.time || '' }
			if (e.user) this.personLast.set(e.user, { text: e.text || '', time: e.time || '' })
			this.recent.push({ ms: now, user: e.user, name: e.name, text: e.text })
		}
		return fired
	}

	/** Zeitabhängiges (Haltezeit, Aufräumen). Liefert true, wenn sich etwas geändert hat. */
	tick(now) {
		let changed = false
		const hold = Number(this.options.holdSeconds) || 0
		if (hold > 0) {
			for (const s of this.triggerState.values()) {
				if (s.active && now - s.lastHitMs > hold * 1000) {
					s.active = false
					changed = true
				}
			}
		}
		for (const [k, ms] of this.liveFired) if (now - ms > LIVE_MEMORY_MS) this.liveFired.delete(k)
		while (this.recent.length > 0 && now - this.recent[0].ms > RECENT_MS) this.recent.shift()
		if (this.recent.length > 500) this.recent.splice(0, this.recent.length - 500)
		return changed
	}

	/** Wurde einer der Begriffe in den letzten `seconds` Sekunden gesagt (optional von einer Person)? */
	heard(words, seconds, userId, now) {
		let m = this.heardCache.get(words)
		if (!m) {
			const list = splitList(words)
			m = new Matcher(this.options.nicknames ? expandNicknames(list, this.persons) : list, this.options.fuzzy)
			if (this.heardCache.size > 200) this.heardCache.clear()
			this.heardCache.set(words, m)
		}
		if (m.empty) return false
		const from = now - Math.max(1, Number(seconds) || 10) * 1000
		for (let i = this.recent.length - 1; i >= 0; i--) {
			const r = this.recent[i]
			if (r.ms < from) break
			if ((!userId || r.user === userId) && m.matches(r.text)) return true
		}
		if (this.options.useLive && this.data?.live) {
			for (const l of this.data.live) if ((!userId || l.user === userId) && m.matches(l.text)) return true
		}
		return false
	}

	// --- Personen ---------------------------------------------------------------------

	assignSlugs() {
		this.personSlugs.clear()
		const used = new Set()
		for (const p of this.persons) {
			let slug = slugify(p.name, `person_${p.id}`)
			let n = 2
			while (used.has(slug)) slug = `${slugify(p.name, 'person')}_${n++}`
			used.add(slug)
			this.personSlugs.set(p.id, slug)
		}
	}

	person(id) {
		return this.persons.find((p) => p.id === id) || null
	}

	/** Aufbereitete Werte einer Person für Variablen und Feedbacks. */
	status(p) {
		const rf = p.rf || {}
		const live = rf.assigned && rf.online && rf.txOn !== undefined
		const st = {
			speaking: !!p.speaking,
			battery: live && rf.txOn && rf.battery >= 0 ? rf.battery : null,
			minutes: live && rf.txOn && rf.minutes >= 0 ? rf.minutes : null,
			signal: live && rf.txOn && typeof rf.signal === 'number' ? rf.signal : null,
			afMuted: !!(live && rf.afMuted),
			rfMuted: !!(live && rf.rfMuted),
			tx: !rf.assigned ? '' : !rf.online ? 'offline' : live ? (rf.txOn ? 'an' : 'aus') : '?',
			iem: '',
			iemOff: false,
			level: typeof p.level === 'number' ? p.level : null,
		}
		if (p.iem) {
			st.iem = !p.iem.online ? 'offline' : p.iem.rfOn ? 'an' : 'aus'
			st.iemOff = st.iem !== 'an'
		}
		st.mute = st.afMuted && st.rfMuted ? 'AF+RF' : st.afMuted ? 'AF' : st.rfMuted ? 'RF' : ''
		st.lowBattery = st.battery !== null && st.battery <= this.options.lowBattery
		st.weak = st.signal !== null && st.signal <= this.options.weakSignal
		st.txProblem = st.tx === 'aus' || st.tx === 'offline'
		return st
	}

	/** Prüft eine Bedingung für eine Person oder ("*") für irgendeine Person. */
	anyPerson(personId, fn) {
		const list = personId && personId !== '*' ? [this.person(personId)].filter(Boolean) : this.persons
		return list.some((p) => fn(this.status(p), p))
	}

	namesWhere(fn) {
		return this.persons
			.filter((p) => fn(this.status(p), p))
			.map((p) => p.name)
			.join(', ')
	}

	// --- Ausgabe ----------------------------------------------------------------------

	definitionsKey() {
		return JSON.stringify([
			this.persons.map((p) => [p.id, p.name, this.personSlugs.get(p.id)]),
			this.triggers.map((t) => [t.id, t.label]),
		])
	}

	variableValues() {
		const d = this.data || {}
		const alarm = d.alarm || {}
		const v = {
			connected: this.connected,
			version: d.version || '',
			listening: !!d.listening,
			control: !!d.control,
			alarm_active: !!alarm.active,
			alarm_keyword: alarm.keyword || '',
			alarm_name: alarm.name || '',
			alarm_text: alarm.text || '',
			alarm_time: alarm.time || '',
			mention_count: Number(d.mentionCount) || 0,
			keywords: d.keywords || '',
			last_speaker: this.lastEntry.name,
			last_text: this.lastEntry.text,
			last_time: this.lastEntry.time,
			trigger_total: this.totalHits,
			last_trigger: this.lastTrigger.label,
			last_trigger_by: this.lastTrigger.by,
			last_trigger_text: this.lastTrigger.text,
			last_trigger_time: this.lastTrigger.time,
			speaking: this.namesWhere((s) => s.speaking),
			speaking_count: this.persons.filter((p) => p.speaking).length,
			low_battery: this.namesWhere((s) => s.lowBattery),
			muted: this.namesWhere((s) => s.mute !== ''),
			tx_problems: this.namesWhere((s) => s.txProblem),
			weak_signal: this.namesWhere((s) => s.weak),
			iem_off: this.namesWhere((s) => s.iemOff),
			persons_count: this.persons.length,
		}

		for (const t of this.triggers) {
			const s = this.triggerState.get(t.id)
			v[`trig_${t.id}`] = s.active
			v[`trig_${t.id}_count`] = s.count
			v[`trig_${t.id}_by`] = s.by
			v[`trig_${t.id}_text`] = s.text
			v[`trig_${t.id}_time`] = s.time
		}

		for (const p of this.persons) {
			const slug = this.personSlugs.get(p.id)
			const st = this.status(p)
			const last = this.personLast.get(p.id) || { text: '', time: '' }
			v[`p_${slug}_name`] = p.name || ''
			v[`p_${slug}_nickname`] = p.nickname || ''
			v[`p_${slug}_role`] = p.role || ''
			v[`p_${slug}_speaking`] = st.speaking
			v[`p_${slug}_battery`] = st.battery ?? ''
			v[`p_${slug}_runtime`] = st.minutes !== null ? `${Math.floor(st.minutes / 60)}:${String(st.minutes % 60).padStart(2, '0')}` : ''
			v[`p_${slug}_signal`] = st.signal ?? ''
			v[`p_${slug}_mute`] = st.mute
			v[`p_${slug}_tx`] = st.tx
			v[`p_${slug}_iem`] = st.iem
			v[`p_${slug}_level`] = st.level ?? ''
			v[`p_${slug}_last_text`] = last.text
			v[`p_${slug}_last_time`] = last.time
		}
		return v
	}
}

function clock(ms) {
	const d = new Date(ms)
	return [d.getHours(), d.getMinutes(), d.getSeconds()].map((n) => String(n).padStart(2, '0')).join(':')
}

module.exports = { ShowComState }
