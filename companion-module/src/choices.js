/** Auswahllisten für Optionen (Personen, Trigger). */
function personChoices(state, withAny = true) {
	const list = state.persons.map((p) => ({ id: p.id, label: p.role ? `${p.name} (${p.role})` : p.name }))
	return withAny ? [{ id: '*', label: '– irgendeine Person –' }, ...list] : list
}

function triggerChoices(state, withAll = false) {
	const list = state.triggers.map((t) => ({ id: t.id, label: t.words.length > 1 ? `${t.label} (${t.words.slice(1).join(', ')})` : t.label }))
	return withAll ? [{ id: '*', label: '– alle Trigger –' }, ...list] : list
}

function firstId(choices, fallback = '') {
	return choices.length > 0 ? choices[0].id : fallback
}

module.exports = { personChoices, triggerChoices, firstId }
