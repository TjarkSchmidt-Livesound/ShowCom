const { colours } = require('./feedbacks')
const { RED, WHITE, BLACK, GREEN, YELLOW, GREY } = colours
const DARK = 0x202020

function button(category, name, text, style, feedbacks, down, extra = {}) {
	return {
		type: 'button',
		category,
		name,
		style: { text, size: 'auto', color: WHITE, bgcolor: DARK, ...style },
		feedbacks,
		steps: [{ down, up: [] }],
		...extra,
	}
}

function buildPresets(state, label = 'showcom') {
	const p = {}
	const v = (name) => `$(${label}:${name})`

	// --- Bedienung -------------------------------------------------------------
	p.alarm = button(
		'ShowCom',
		'Alarm (zeigt wer gerufen hat, Druck quittiert)',
		`ALARM\\n${v('alarm_name')}`,
		{ size: '14' },
		[{ feedbackId: 'alarm_active', options: {}, style: { bgcolor: RED, color: WHITE } }],
		[{ actionId: 'ack_alarm', options: { resetTriggers: true } }],
	)
	p.listen = button(
		'ShowCom',
		'Zuhören an/aus',
		'ZUHÖREN',
		{ size: '14' },
		[{ feedbackId: 'listening', options: {}, style: { bgcolor: GREEN, color: WHITE } }],
		[{ actionId: 'listen', options: { mode: 'toggle' } }],
	)
	p.popup = button('ShowCom', 'Alarm-Popup am Rechner', 'ALARM\\nPOPUP', { size: '14' }, [], [{ actionId: 'popup', options: {} }])
	p.test = button('ShowCom', 'Testalarm', 'TEST\\nALARM', { size: '14' }, [], [{ actionId: 'test_alarm', options: {} }])
	p.reset = button(
		'ShowCom',
		'Alle Trigger zurücksetzen',
		`RESET\\n${v('trigger_total')}`,
		{ size: '14' },
		[{ feedbackId: 'mention', options: { trigger: '*', speaker: '*' }, style: { bgcolor: RED, color: WHITE } }],
		[{ actionId: 'reset_trigger', options: { trigger: '*', counts: false } }],
	)
	p.last = button('ShowCom', 'Letzter Satz', `${v('last_speaker')}\\n${v('last_text')}`, { size: '7' }, [], [])
	p.speaking = button(
		'ShowCom',
		'Wer spricht',
		`${v('speaking')}`,
		{ size: '14' },
		[{ feedbackId: 'speaking', options: { person: '*' }, style: { bgcolor: GREEN, color: WHITE } }],
		[],
	)
	p.connection = button(
		'ShowCom',
		'Verbindung',
		'SHOWCOM',
		{ size: '14', bgcolor: RED },
		[{ feedbackId: 'connected', options: {}, style: { bgcolor: GREEN, color: WHITE } }],
		[],
	)

	// --- Funk-Übersicht ----------------------------------------------------------
	p.rf_muted = button(
		'Funk',
		'Stummgeschaltet (alle)',
		`MUTE\\n${v('muted')}`,
		{ size: '7' },
		[{ feedbackId: 'muted', options: { person: '*', kind: 'any' }, style: { bgcolor: RED, color: WHITE } }],
		[],
	)
	p.rf_battery = button(
		'Funk',
		'Akku schwach (alle)',
		`AKKU\\n${v('low_battery')}`,
		{ size: '7' },
		[{ feedbackId: 'battery_low', options: { person: '*', threshold: 0 }, style: { bgcolor: YELLOW, color: BLACK } }],
		[],
	)
	p.rf_tx = button(
		'Funk',
		'Sender aus/offline (alle)',
		`TX\\n${v('tx_problems')}`,
		{ size: '7' },
		[{ feedbackId: 'tx_problem', options: { person: '*' }, style: { bgcolor: GREY, color: WHITE } }],
		[],
	)
	p.rf_iem = button(
		'Funk',
		'In-Ear aus/offline (alle)',
		`IEM\\n${v('iem_off')}`,
		{ size: '7' },
		[{ feedbackId: 'iem_off', options: { person: '*' }, style: { bgcolor: RED, color: WHITE } }],
		[],
	)

	// --- Trigger -----------------------------------------------------------------
	for (const t of state.triggers) {
		p[`trigger_${t.id}`] = button(
			'Trigger',
			`${t.label} erwähnt (Druck setzt zurück)`,
			`${t.label.toUpperCase()}\\n${v(`trig_${t.id}_by`)}`,
			{ size: '14' },
			[{ feedbackId: 'mention', options: { trigger: t.id, speaker: '*' }, style: { bgcolor: RED, color: WHITE } }],
			[{ actionId: 'reset_trigger', options: { trigger: t.id, counts: false } }],
		)
	}

	// --- Personen ----------------------------------------------------------------
	for (const person of state.persons) {
		const s = state.personSlugs.get(person.id)
		p[`person_${s}`] = button(
			'Personen',
			`${person.name}: Status (spricht, Funk, Mute)`,
			`${person.name}\\n${v(`p_${s}_battery`)}% ${v(`p_${s}_mute`)}`,
			{ size: '14' },
			[
				{ feedbackId: 'person_colour', options: { person: person.id, text: true } },
				{ feedbackId: 'speaking', options: { person: person.id }, style: { bgcolor: GREEN, color: WHITE } },
				{ feedbackId: 'battery_low', options: { person: person.id, threshold: 0 }, style: { bgcolor: YELLOW, color: BLACK } },
				{ feedbackId: 'tx_problem', options: { person: person.id }, style: { bgcolor: GREY, color: WHITE } },
				{ feedbackId: 'muted', options: { person: person.id, kind: 'any' }, style: { bgcolor: RED, color: WHITE } },
			],
			[],
		)
		p[`person_said_${s}`] = button(
			'Personen',
			`${person.name}: zuletzt gesagt`,
			`${person.name}\\n${v(`p_${s}_last_text`)}`,
			{ size: '7' },
			[{ feedbackId: 'speaking', options: { person: person.id }, style: { bgcolor: GREEN, color: WHITE } }],
			[],
		)
	}

	return p
}

module.exports = { buildPresets }
