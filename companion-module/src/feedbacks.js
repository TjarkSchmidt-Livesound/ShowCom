const { combineRgb } = require('@companion-module/base')
const { personChoices, triggerChoices, firstId } = require('./choices')

const RED = combineRgb(204, 0, 0)
const WHITE = combineRgb(255, 255, 255)
const BLACK = combineRgb(0, 0, 0)
const GREEN = combineRgb(0, 153, 51)
const YELLOW = combineRgb(255, 191, 0)
const ORANGE = combineRgb(230, 110, 0)
const GREY = combineRgb(70, 70, 70)

function hexToRgb(hex) {
	const m = /^#?([0-9a-f]{6})$/i.exec(String(hex || ''))
	if (!m) return null
	const n = parseInt(m[1], 16)
	return combineRgb((n >> 16) & 255, (n >> 8) & 255, n & 255)
}

function buildFeedbacks(self) {
	const state = self.state
	const persons = personChoices(state)
	const personsOnly = personChoices(state, false)
	const triggers = triggerChoices(state, true)
	const personOption = { type: 'dropdown', id: 'person', label: 'Person', default: '*', choices: persons }
	const speakerOption = { type: 'dropdown', id: 'speaker', label: 'Gesagt von', default: '*', choices: persons }

	return {
		mention: {
			type: 'boolean',
			name: 'Name/Begriff erwähnt (Trigger)',
			description: 'An, sobald der Trigger gesagt wurde – bis zum Zurücksetzen bzw. bis die Haltezeit abläuft',
			defaultStyle: { bgcolor: RED, color: WHITE },
			options: [
				{
					type: 'dropdown',
					id: 'trigger',
					label: 'Trigger',
					default: firstId(triggers.slice(1), '*'),
					choices: triggers.map((c) => (c.id === '*' ? { id: '*', label: '– irgendein Trigger –' } : c)),
				},
				speakerOption,
			],
			callback: (fb) => {
				const id = fb.options.trigger || '*'
				const speaker = fb.options.speaker || '*'
				const ids = id === '*' ? state.triggers.map((t) => t.id) : [id]
				return ids.some((tid) => {
					const s = state.triggerState.get(tid)
					return !!s && s.active && (speaker === '*' || s.byId === speaker)
				})
			},
		},
		heard: {
			type: 'boolean',
			name: 'Wort gehört (frei, in den letzten N Sekunden)',
			description: 'Beliebige Wörter ohne festen Trigger, z. B. „Pyro, Konfetti“ oder „Stand by“',
			defaultStyle: { bgcolor: ORANGE, color: BLACK },
			options: [
				{ type: 'textinput', id: 'words', label: 'Wörter (Komma trennt)', default: '' },
				{ type: 'number', id: 'seconds', label: 'Zeitfenster in Sekunden', default: 10, min: 1, max: 600 },
				speakerOption,
			],
			callback: (fb) =>
				state.heard(String(fb.options.words || ''), fb.options.seconds, fb.options.speaker === '*' ? '' : fb.options.speaker, Date.now()),
		},
		alarm_active: {
			type: 'boolean',
			name: 'ShowCom-Alarm aktiv (Mein Name)',
			description: 'Der Alarm am ShowCom-Rechner ist offen (noch nicht quittiert)',
			defaultStyle: { bgcolor: RED, color: WHITE },
			options: [],
			callback: () => !!state.data?.alarm?.active,
		},
		listening: {
			type: 'boolean',
			name: 'ShowCom hört zu',
			defaultStyle: { bgcolor: GREEN, color: WHITE },
			options: [],
			callback: () => !!state.data?.listening,
		},
		connected: {
			type: 'boolean',
			name: 'Verbindung zu ShowCom',
			defaultStyle: { bgcolor: GREEN, color: WHITE },
			options: [],
			callback: () => state.connected,
		},
		speaking: {
			type: 'boolean',
			name: 'Person spricht gerade',
			defaultStyle: { bgcolor: GREEN, color: WHITE },
			options: [personOption],
			callback: (fb) => state.anyPerson(fb.options.person, (s) => s.speaking),
		},
		muted: {
			type: 'boolean',
			name: 'Funk: Sender stummgeschaltet',
			defaultStyle: { bgcolor: RED, color: WHITE },
			options: [
				personOption,
				{
					type: 'dropdown',
					id: 'kind',
					label: 'Art',
					default: 'any',
					choices: [
						{ id: 'any', label: 'AF oder RF' },
						{ id: 'af', label: 'nur AF (Audio)' },
						{ id: 'rf', label: 'nur RF (Funk)' },
					],
				},
			],
			callback: (fb) =>
				state.anyPerson(fb.options.person, (s) =>
					fb.options.kind === 'af' ? s.afMuted : fb.options.kind === 'rf' ? s.rfMuted : s.mute !== '',
				),
		},
		battery_low: {
			type: 'boolean',
			name: 'Funk: Akku schwach',
			defaultStyle: { bgcolor: YELLOW, color: BLACK },
			options: [
				personOption,
				{ type: 'number', id: 'threshold', label: 'Grenze in % (0 = aus der Konfiguration)', default: 0, min: 0, max: 100 },
			],
			callback: (fb) => {
				const limit = Number(fb.options.threshold) || state.options.lowBattery
				return state.anyPerson(fb.options.person, (s) => s.battery !== null && s.battery <= limit)
			},
		},
		tx_problem: {
			type: 'boolean',
			name: 'Funk: Sender aus oder Empfänger offline',
			defaultStyle: { bgcolor: GREY, color: WHITE },
			options: [personOption],
			callback: (fb) => state.anyPerson(fb.options.person, (s) => s.txProblem),
		},
		weak_signal: {
			type: 'boolean',
			name: 'Funk: Signal schwach',
			defaultStyle: { bgcolor: ORANGE, color: BLACK },
			options: [personOption],
			callback: (fb) => state.anyPerson(fb.options.person, (s) => s.weak),
		},
		iem_off: {
			type: 'boolean',
			name: 'In-Ear (AD PSM): Sender aus oder offline',
			defaultStyle: { bgcolor: RED, color: WHITE },
			options: [personOption],
			callback: (fb) => state.anyPerson(fb.options.person, (s) => s.iemOff),
		},
		person_colour: {
			type: 'advanced',
			name: 'Personenfarbe aus ShowCom',
			description: 'Färbt die Taste in der Farbe der Person (wie im ShowCom-Board)',
			options: [
				{ type: 'dropdown', id: 'person', label: 'Person', default: firstId(personsOnly), choices: personsOnly },
				{ type: 'checkbox', id: 'text', label: 'Statt Hintergrund die Schrift färben', default: false },
			],
			callback: (fb) => {
				const c = hexToRgb(state.person(fb.options.person)?.colour)
				if (c === null) return {}
				return fb.options.text ? { color: c } : { bgcolor: c, color: WHITE }
			},
		},
	}
}

module.exports = { buildFeedbacks, colours: { RED, WHITE, BLACK, GREEN, YELLOW, ORANGE, GREY } }
