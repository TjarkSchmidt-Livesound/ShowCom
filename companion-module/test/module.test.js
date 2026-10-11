const test = require('node:test')
const assert = require('node:assert/strict')
const http = require('node:http')
const path = require('node:path')
const Module = require('node:module')

// @companion-module/base durch die Attrappe ersetzen
const resolve = Module._resolveFilename
Module._resolveFilename = function (request, ...rest) {
	if (request === '@companion-module/base') return path.join(__dirname, 'mock-base.js')
	return resolve.call(this, request, ...rest)
}
process.env.SHOWCOM_NO_ENTRYPOINT = '1'

const { Matcher, expandNicknames, slugify } = require('../src/matcher')
const { ShowComState } = require('../src/state')
const ShowComInstance = require('../src/main')

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
async function until(fn, ms = 3000) {
	const end = Date.now() + ms
	while (Date.now() < end) {
		if (fn()) return true
		await sleep(20)
	}
	return false
}

test('Matcher: ganze Wörter, Umlaute, Genitiv, Tippfehler, Mehrwort', () => {
	const m = new Matcher(['Jürgen', 'Tim', 'Front of House'])
	assert.ok(m.matches('juergen, kommst du mal'))
	assert.ok(m.matches('Tims Mikro ist aus'))
	assert.ok(!m.matches('Timing passt'))
	assert.ok(m.matches('Jürgn bitte'), 'ein Buchstabe Abweichung ab 5 Zeichen')
	assert.ok(m.matches('bitte zum front of house'))
	assert.ok(!new Matcher(['Tim'], true).matches('Tom'), 'kurze Namen nicht unscharf')
	assert.ok(!new Matcher(['Jürgen'], false).matches('Jürgn'))
	assert.deepEqual(new Matcher(['Anna']).find('Hallo Anna!').map((x) => [x.start, x.end]), [[6, 10]])
})

test('Spitznamen und IDs', () => {
	const persons = [{ name: 'Anna', nickname: 'Anni, Annchen' }, { name: 'Mia', nickname: '' }]
	assert.deepEqual(expandNicknames(['Anna'], persons), ['Anna', 'Anni', 'Annchen'])
	assert.deepEqual(expandNicknames(['Anni'], persons), ['Anni', 'Anna', 'Annchen'])
	assert.equal(slugify('Jürgen Müller'), 'juergen_mueller')
	assert.equal(slugify('!!!', 'x'), 'x')
})

function snapshot(extra = {}) {
	return {
		app: 'ShowCom',
		version: '0.10.0',
		listening: true,
		control: true,
		keywords: 'Tjark',
		mentionCount: 0,
		alarm: { active: false },
		lastEntryId: 3,
		persons: [
			{ id: 'u1', name: 'Tjark', nickname: 'TJ', role: 'FOH', colour: '#ba68c8', speaking: false, rf: { assigned: false } },
			{
				id: 'u2',
				name: 'Mia',
				nickname: '',
				role: 'Monitor',
				colour: '#9575cd',
				speaking: true,
				rf: { assigned: true, online: true, txOn: true, battery: 20, minutes: 95, signal: 80, afMuted: true, rfMuted: false },
				iem: { online: true, rfOn: false },
			},
		],
		entries: [],
		live: [],
		...extra,
	}
}

test('Zustand: Trigger, Live-Text, eigener Name, Haltezeit, Reset', () => {
	const s = new ShowComState()
	s.configure({ triggers: 'Licht|LD, Pyro', autoPersons: true, holdSeconds: 5 })
	s.apply(snapshot({ entries: [{ id: 3, user: 'u2', name: 'Mia', text: 'Tjark alt' }] }), 1000)
	assert.equal(s.triggerState.get('tjark').count, 0, 'alte Sätze lösen nicht aus')
	assert.deepEqual(s.triggers.map((t) => t.id), ['licht', 'pyro', 'tjark', 'mia'])
	assert.ok(s.trigger('tjark').words.includes('TJ'), 'Spitzname der Person zählt mit')

	// Live: löst sofort aus, der fertige Satz zählt nicht doppelt
	s.apply(snapshot({ live: [{ id: 0, user: 'u2', name: 'Mia', text: 'TJ kannst du' }] }), 2000)
	assert.equal(s.triggerState.get('tjark').count, 1)
	assert.ok(s.isTriggerActive('tjark'))
	s.apply(snapshot({ lastEntryId: 4, entries: [{ id: 4, user: 'u2', name: 'Mia', text: 'TJ kannst du mal kommen', time: '20:00:01' }] }), 2500)
	assert.equal(s.triggerState.get('tjark').count, 1)
	assert.equal(s.triggerState.get('tjark').text, 'TJ kannst du mal kommen')

	// Eigene Trigger mit Varianten; eigener Name zählt nicht
	s.apply(snapshot({ lastEntryId: 6, entries: [
		{ id: 5, user: 'u1', name: 'Tjark', text: 'Hier Tjark, LD bitte Blackout' },
		{ id: 6, user: 'u1', name: 'Tjark', text: 'Mia, check' },
	] }), 3000)
	assert.equal(s.triggerState.get('tjark').count, 1, 'Tjark sagt seinen eigenen Namen')
	assert.equal(s.triggerState.get('licht').count, 1)
	assert.equal(s.triggerState.get('licht').by, 'Tjark')
	assert.equal(s.triggerState.get('mia').count, 1)

	const v = s.variableValues()
	assert.equal(v.trig_licht_count, 1)
	assert.equal(v.last_trigger, 'Mia')
	assert.equal(v.p_mia_battery, 20)
	assert.equal(v.p_mia_runtime, '1:35')
	assert.equal(v.p_mia_mute, 'AF')
	assert.equal(v.p_mia_iem, 'aus')
	assert.equal(v.low_battery, 'Mia')
	assert.equal(v.muted, 'Mia')
	assert.equal(v.speaking, 'Mia')

	assert.ok(s.heard('blackout', 10, '', 3500))
	assert.ok(!s.heard('blackout', 10, 'u2', 3500), 'nur von einer bestimmten Person')
	assert.ok(!s.heard('blackout', 10, '', 30000), 'Zeitfenster abgelaufen')

	// Haltezeit 5 s
	assert.ok(s.tick(3000 + 6000))
	assert.ok(!s.isTriggerActive('licht'))

	s.simulate('pyro', 10000)
	assert.ok(s.isTriggerActive('pyro'))
	assert.ok(s.reset('*', true))
	assert.equal(s.triggerState.get('pyro').count, 0)

	// ShowCom neu gestartet (IDs kleiner): keine Geister-Auslösung
	s.apply(snapshot({ lastEntryId: 1, entries: [{ id: 1, user: 'u2', name: 'Mia', text: 'Pyro' }] }), 11000)
	assert.equal(s.triggerState.get('pyro').count, 0)
})

test('Modul gegen einen nachgebauten ShowCom-Server', async () => {
	let state = snapshot()
	const commands = []
	let lastKey = null
	const server = http.createServer((req, res) => {
		lastKey = req.headers['x-showcom-key'] ?? null
		if (lastKey !== 'geheim') {
			res.writeHead(403, { 'Content-Type': 'application/json' })
			return res.end('"wrong key"')
		}
		if (req.method === 'GET' && req.url.startsWith('/api/companion/state')) {
			res.writeHead(200, { 'Content-Type': 'application/json' })
			return res.end(JSON.stringify(state))
		}
		if (req.method === 'POST' && req.url === '/api/companion/command') {
			let body = ''
			req.on('data', (c) => (body += c))
			req.on('end', () => {
				commands.push(JSON.parse(body).cmd)
				res.writeHead(200, { 'Content-Type': 'application/json' })
				res.end('true')
			})
			return
		}
		res.writeHead(404)
		res.end()
	})
	await new Promise((r) => server.listen(0, '127.0.0.1', r))
	const port = server.address().port

	const inst = new ShowComInstance({})
	await inst.init({ host: '127.0.0.1', port, key: 'falsch', triggers: 'Pyro', pollInterval: 100 })
	assert.ok(await until(() => inst.currentStatus === 'bad_config'), 'falscher Schlüssel wird gemeldet')
	assert.match(inst.currentMessage, /Schlüssel/)

	await inst.configUpdated({ host: '127.0.0.1', port, key: 'geheim', triggers: 'Pyro', pollInterval: 100 })
	assert.ok(await until(() => inst.currentStatus === 'ok' && inst.variables.connected === true))
	assert.ok(inst.variableDefs.some((d) => d.variableId === 'trig_pyro_count'))
	assert.ok(inst.variableDefs.some((d) => d.variableId === 'p_mia_battery'))
	assert.ok(inst.presets.trigger_tjark && inst.presets.person_mia && inst.presets.alarm)
	assert.equal(inst.presets.alarm.style.text, 'ALARM\\n$(showcom:alarm_name)')

	// Jemand sagt "Pyro" -> Trigger, Feedback, Variablen
	state = snapshot({ lastEntryId: 4, entries: [{ id: 4, user: 'u2', name: 'Mia', text: 'Pyro ist scharf', time: '21:00:00' }],
		alarm: { active: true, keyword: 'Tjark', name: 'Mia', text: 'Tjark?', time: '21:00:01' } })
	assert.ok(await until(() => inst.variables.trig_pyro_count === 1))
	assert.equal(inst.variables.trig_pyro_by, 'Mia')
	assert.equal(inst.variables.alarm_name, 'Mia')
	assert.equal(inst.feedbacks.mention.callback({ options: { trigger: 'pyro', speaker: '*' } }), true)
	assert.equal(inst.feedbacks.mention.callback({ options: { trigger: 'pyro', speaker: 'u1' } }), false)
	assert.equal(inst.feedbacks.mention.callback({ options: { trigger: '*', speaker: '*' } }), true)
	assert.equal(inst.feedbacks.alarm_active.callback({ options: {} }), true)
	assert.equal(inst.feedbacks.muted.callback({ options: { person: 'u2', kind: 'af' } }), true)
	assert.equal(inst.feedbacks.muted.callback({ options: { person: 'u2', kind: 'rf' } }), false)
	assert.equal(inst.feedbacks.battery_low.callback({ options: { person: '*', threshold: 0 } }), true)
	assert.equal(inst.feedbacks.battery_low.callback({ options: { person: '*', threshold: 10 } }), false)
	assert.equal(inst.feedbacks.iem_off.callback({ options: { person: 'u2' } }), true)
	assert.equal(inst.feedbacks.heard.callback({ options: { words: 'scharf', seconds: 10, speaker: '*' } }), true)
	assert.deepEqual(inst.feedbacks.person_colour.callback({ options: { person: 'u1', text: false } }), { bgcolor: 0xba68c8, color: 0xffffff })

	// Aktionen
	await inst.actions.ack_alarm.callback({ options: { resetTriggers: true } })
	await inst.actions.listen.callback({ options: { mode: 'off' } })
	await inst.actions.listen.callback({ options: { mode: 'toggle' } })
	await inst.actions.test_alarm.callback({ options: {} })
	await inst.actions.popup.callback({ options: {} })
	assert.deepEqual(commands, ['ack', 'pause', 'toggleListen', 'testAlarm', 'popup'])
	assert.equal(inst.variables.trig_pyro, false, 'Quittieren setzt die Trigger zurück')
	assert.equal(inst.variables.trig_pyro_count, 1, 'Zähler bleibt')

	// Neue Person in ShowCom -> neue Variablen/Presets
	state = snapshot({ lastEntryId: 4, persons: [...snapshot().persons, { id: 'u3', name: 'Jürgen', nickname: '', rf: { assigned: false } }] })
	assert.ok(await until(() => inst.variableDefs.some((d) => d.variableId === 'p_juergen_name')))
	assert.ok(inst.presets.trigger_juergen)

	// ShowCom weg -> Verbindungsfehler
	await inst.destroy()
	await new Promise((r) => server.close(r))
	await inst.configUpdated({ host: '127.0.0.1', port, key: 'geheim', pollInterval: 100 })
	assert.ok(await until(() => inst.currentStatus === 'connection_failure'))
	assert.equal(inst.variables.connected, false)
	await inst.destroy()
})

test('Ältere ShowCom-Version (ohne /api/companion): Browser-Schnittstelle', async () => {
	let registered = 0
	let entries = [{ id: 7, user: 'u1', time: '20:00:00', text: 'alt' }]
	const server = http.createServer((req, res) => {
		if (req.url.startsWith('/api/companion')) {
			res.writeHead(404)
			return res.end('not found')
		}
		if (req.method === 'POST' && req.url === '/api/register') {
			registered++
			res.writeHead(200, { 'Content-Type': 'application/json' })
			return res.end('{"id":"ABCDEF"}')
		}
		if (req.url === '/api/d/ABCDEF/state') {
			res.writeHead(200, { 'Content-Type': 'application/json' })
			return res.end(JSON.stringify({
				device: { id: 'ABCDEF' },
				listening: true,
				persons: [
					{ id: 'u1', name: 'Tjark', nickname: 'TJ', colour: '#ffffff', speaking: false, rf: { assigned: false } },
					{ id: 'u2', name: 'Mia', nickname: '', colour: '#000000', speaking: true, afMuted: false, rfMuted: true,
						rf: { assigned: true, online: true, known: true, txOn: true, battery: 15, minutes: 50, signal: 0.6, muted: true },
						iem: { online: true, rfOn: true, l: -20, r: -21 }, level: [10, 40] },
				],
				entries,
				live: [],
			}))
		}
		res.writeHead(404)
		res.end()
	})
	await new Promise((r) => server.listen(0, '127.0.0.1', r))
	const port = server.address().port

	const inst = new ShowComInstance({})
	await inst.init({ host: '127.0.0.1', port, triggers: 'Pyro', pollInterval: 100 })
	assert.ok(await until(() => inst.variables.connected === true && inst.variables.persons_count === 2))
	assert.equal(inst.currentStatus, 'ok')
	assert.match(inst.currentMessage, /0\.10/)
	assert.equal(inst.savedConfig.legacyDevice, 'ABCDEF', 'Geräte-ID wird gemerkt')
	assert.equal(inst.variables.p_mia_mute, 'RF')
	assert.equal(inst.variables.p_mia_signal, 60)
	assert.equal(inst.variables.p_mia_level, 40)

	entries = [...entries, { id: 8, user: 'u2', time: '20:00:05', text: 'TJ, Pyro steht' }]
	assert.ok(await until(() => inst.variables.trig_pyro_count === 1 && inst.variables.trig_tjark_count === 1))
	assert.equal(inst.variables.trig_tjark_by, 'Mia')

	// Speichern der Geräte-ID startet die Verbindung nicht neu
	await inst.configUpdated(inst.savedConfig)
	await sleep(300)
	assert.equal(registered, 1)
	assert.equal(inst.variables.trig_pyro_count, 1)
	await inst.destroy()
	await new Promise((r) => server.close(r))
})

test('Verbindungsfehler mit verständlicher Ursache', async () => {
	const probe = http.createServer()
	await new Promise((r) => probe.listen(0, '127.0.0.1', r))
	const port = probe.address().port
	await new Promise((r) => probe.close(r))

	const inst = new ShowComInstance({})
	try {
		await inst.init({ host: '127.0.0.1', port, pollInterval: 100 })
		assert.ok(await until(() => inst.currentStatus === 'connection_failure'))
		assert.match(inst.currentMessage, /abgelehnt/)
	} finally {
		await inst.destroy()
	}
})
