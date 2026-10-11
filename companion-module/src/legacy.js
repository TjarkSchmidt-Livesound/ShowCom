/**
 * Ältere ShowCom-Versionen (vor 0.10) kennen /api/companion noch nicht. Dann liest das Modul
 * die normale Browser-Schnittstelle (/api/d/<Gerät>/state) – Trigger, Sprecher und Funk gehen damit,
 * Alarm und Steuerung erst ab ShowCom 0.10.
 */
function fromLegacy(data) {
	const names = new Map()
	const persons = (data.persons || []).map((p) => {
		names.set(p.id, p.name)
		const rf = p.rf || {}
		const usable = rf.assigned && !rf.disabled
		const out = {
			id: p.id,
			name: p.name,
			nickname: p.nickname || '',
			role: p.role || '',
			colour: p.colour,
			talkback: p.talkback,
			speaking: !!p.speaking,
			favorite: false,
			rf: { assigned: !!usable, online: !!(usable && rf.online) },
		}
		if (usable && rf.online && rf.known) {
			Object.assign(out.rf, {
				txOn: !!rf.txOn,
				name: rf.name,
				battery: rf.battery,
				minutes: rf.minutes,
				signal: typeof rf.signal === 'number' ? Math.round(rf.signal * 100) : undefined,
				afMuted: !!p.afMuted || (!!rf.muted && !p.rfMuted),
				rfMuted: !!p.rfMuted,
				freq: rf.freq,
			})
		}
		if (p.iem && !p.iem.disabled) out.iem = { online: !!p.iem.online, rfOn: !!p.iem.rfOn, l: p.iem.l, r: p.iem.r }
		if (Array.isArray(p.level) && p.level.length > 0) out.level = p.level[p.level.length - 1]
		return out
	})

	const entry = (e) => ({ id: e.id, user: e.user, name: names.get(e.user) || '', time: e.time, text: e.text })
	const entries = (data.entries || []).map(entry)
	return {
		app: 'ShowCom',
		version: 'vor 0.10',
		legacy: true,
		listening: !!data.listening,
		control: false,
		keywords: '',
		mentionCount: 0,
		alarm: { active: false },
		lastEntryId: entries.reduce((m, e) => Math.max(m, Number(e.id) || 0), 0),
		persons,
		entries,
		live: (data.live || []).map(entry),
	}
}

module.exports = { fromLegacy }
