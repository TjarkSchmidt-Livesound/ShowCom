/** Variablen-Definitionen: global, pro Trigger, pro Person. */
function buildVariableDefinitions(state) {
	const defs = [
		{ variableId: 'connected', name: 'Verbunden mit ShowCom' },
		{ variableId: 'version', name: 'ShowCom-Version' },
		{ variableId: 'listening', name: 'ShowCom hört zu' },
		{ variableId: 'control', name: 'Steuerung aus Companion erlaubt' },
		{ variableId: 'alarm_active', name: 'Alarm aktiv (Mein Name am ShowCom-Rechner)' },
		{ variableId: 'alarm_keyword', name: 'Alarm: erkannter Name' },
		{ variableId: 'alarm_name', name: 'Alarm: wer hat gerufen' },
		{ variableId: 'alarm_text', name: 'Alarm: Text' },
		{ variableId: 'alarm_time', name: 'Alarm: Uhrzeit' },
		{ variableId: 'mention_count', name: 'Erwähnungen von „Mein Name“ (Sitzung)' },
		{ variableId: 'keywords', name: 'Alarm-Namen am ShowCom-Rechner' },
		{ variableId: 'last_speaker', name: 'Letzter Satz: wer' },
		{ variableId: 'last_text', name: 'Letzter Satz: Text' },
		{ variableId: 'last_time', name: 'Letzter Satz: Uhrzeit' },
		{ variableId: 'trigger_total', name: 'Trigger: Anzahl gesamt' },
		{ variableId: 'last_trigger', name: 'Letzter Trigger' },
		{ variableId: 'last_trigger_by', name: 'Letzter Trigger: gesagt von' },
		{ variableId: 'last_trigger_text', name: 'Letzter Trigger: Text' },
		{ variableId: 'last_trigger_time', name: 'Letzter Trigger: Uhrzeit' },
		{ variableId: 'speaking', name: 'Wer spricht gerade (Liste)' },
		{ variableId: 'speaking_count', name: 'Anzahl sprechender Personen' },
		{ variableId: 'low_battery', name: 'Funk: Akku schwach (Liste)' },
		{ variableId: 'muted', name: 'Funk: stummgeschaltet (Liste)' },
		{ variableId: 'tx_problems', name: 'Funk: Sender aus/offline (Liste)' },
		{ variableId: 'weak_signal', name: 'Funk: Signal schwach (Liste)' },
		{ variableId: 'iem_off', name: 'In-Ear: Sender aus/offline (Liste)' },
		{ variableId: 'persons_count', name: 'Anzahl Personen' },
	]

	for (const t of state.triggers) {
		defs.push(
			{ variableId: `trig_${t.id}`, name: `Trigger ${t.label}: aktiv` },
			{ variableId: `trig_${t.id}_count`, name: `Trigger ${t.label}: Anzahl` },
			{ variableId: `trig_${t.id}_by`, name: `Trigger ${t.label}: gesagt von` },
			{ variableId: `trig_${t.id}_text`, name: `Trigger ${t.label}: Text` },
			{ variableId: `trig_${t.id}_time`, name: `Trigger ${t.label}: Uhrzeit` },
		)
	}

	for (const p of state.persons) {
		const s = state.personSlugs.get(p.id)
		const n = p.name
		defs.push(
			{ variableId: `p_${s}_name`, name: `${n}: Name` },
			{ variableId: `p_${s}_nickname`, name: `${n}: Spitzname` },
			{ variableId: `p_${s}_role`, name: `${n}: Funktion` },
			{ variableId: `p_${s}_speaking`, name: `${n}: spricht` },
			{ variableId: `p_${s}_battery`, name: `${n}: Akku %` },
			{ variableId: `p_${s}_runtime`, name: `${n}: Restlaufzeit (h:mm)` },
			{ variableId: `p_${s}_signal`, name: `${n}: Funksignal %` },
			{ variableId: `p_${s}_mute`, name: `${n}: Mute (AF/RF)` },
			{ variableId: `p_${s}_tx`, name: `${n}: Sender (an/aus/offline)` },
			{ variableId: `p_${s}_iem`, name: `${n}: In-Ear-Sender (an/aus/offline)` },
			{ variableId: `p_${s}_level`, name: `${n}: Pegel vom Funk %` },
			{ variableId: `p_${s}_last_text`, name: `${n}: zuletzt gesagt` },
			{ variableId: `p_${s}_last_time`, name: `${n}: zuletzt gesagt um` },
		)
	}
	return defs
}

module.exports = { buildVariableDefinitions }
