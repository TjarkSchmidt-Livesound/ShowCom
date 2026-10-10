// Namens-Erkennung wie in ShowCom (KeywordMatcher.cpp):
// ganze Wörter, Groß/Klein egal, Umlaute gefaltet (ü = ue), Mehrwort-Namen,
// Genitiv-"s" ("Tims Mikro") und optional 1 Buchstabe Abweichung ab 5 Zeichen.

const FOLD = {
	ä: 'ae', ö: 'oe', ü: 'ue', ß: 'ss', ẞ: 'ss', æ: 'ae',
	à: 'a', á: 'a', â: 'a', ã: 'a', å: 'a', ç: 'c', č: 'c',
	è: 'e', é: 'e', ê: 'e', ë: 'e', ì: 'i', í: 'i', î: 'i', ï: 'i',
	ñ: 'n', ò: 'o', ó: 'o', ô: 'o', õ: 'o', ø: 'o',
	ù: 'u', ú: 'u', û: 'u', ý: 'y', ÿ: 'y', š: 's', ž: 'z', ł: 'l',
}

function normalise(word) {
	let out = ''
	for (const ch of String(word).toLowerCase()) out += FOLD[ch] ?? ch
	return out
}

// Wortzeichen: Buchstaben (alle Schriften) und Ziffern
const WORD = /[\p{L}\p{N}\p{M}]+/gu

function tokenise(text) {
	const tokens = []
	for (const m of String(text ?? '').matchAll(WORD)) {
		tokens.push({ norm: normalise(m[0]), start: m.index, end: m.index + m[0].length })
	}
	return tokens
}

function levenshteinAtMost1(a, b) {
	if (Math.abs(a.length - b.length) > 1) return false
	let i = 0
	let j = 0
	let edits = 0
	while (i < a.length && j < b.length) {
		if (a[i] === b[j]) {
			i++
			j++
			continue
		}
		if (++edits > 1) return false
		if (a.length > b.length) i++
		else if (a.length < b.length) j++
		else {
			i++
			j++
		}
	}
	return edits + (a.length - i) + (b.length - j) <= 1
}

/** Liste "Anna, Anni; Tjark" -> ["Anna", "Anni", "Tjark"] */
function splitList(list) {
	return String(list ?? '')
		.split(/[,;\n\r|]/)
		.map((s) => s.trim())
		.filter((s) => s.length > 0)
}

class Matcher {
	/** @param {string[]} keywords  @param {boolean} fuzzy */
	constructor(keywords, fuzzy = true) {
		this.fuzzy = fuzzy
		this.entries = []
		for (const k of keywords) {
			const words = tokenise(k).map((t) => t.norm)
			if (words.length > 0) this.entries.push({ words, original: String(k).trim() })
		}
	}

	get empty() {
		return this.entries.length === 0
	}

	wordMatches(textWord, keyWord) {
		if (textWord === keyWord) return true
		if (textWord.length === keyWord.length + 1 && textWord.endsWith('s') && textWord.startsWith(keyWord)) return true
		return this.fuzzy && keyWord.length >= 5 && levenshteinAtMost1(textWord, keyWord)
	}

	/** Alle Treffer im Text: [{ keyword, start, end }] (ohne Überlappungen) */
	find(text) {
		if (this.entries.length === 0) return []
		const tokens = tokenise(text)
		const found = []
		for (let i = 0; i < tokens.length; i++) {
			for (const e of this.entries) {
				if (i + e.words.length > tokens.length) continue
				let ok = true
				for (let w = 0; w < e.words.length && ok; w++) ok = this.wordMatches(tokens[i + w].norm, e.words[w])
				if (ok) found.push({ keyword: e.original, start: tokens[i].start, end: tokens[i + e.words.length - 1].end })
			}
		}
		found.sort((a, b) => a.start - b.start || b.end - b.start - (a.end - a.start))
		const result = []
		let covered = 0
		for (const m of found) {
			if (result.length > 0 && m.start < covered) continue
			covered = m.end
			result.push(m)
		}
		return result
	}

	matches(text) {
		return this.find(text).length > 0
	}
}

/**
 * Spitznamen ergänzen: Steht der Name (oder ein Spitzname) einer ShowCom-Person in der Liste,
 * kommen alle ihre Namen dazu.
 * @param {string[]} words  @param {{name:string,nickname:string}[]} persons
 */
function expandNicknames(words, persons) {
	const result = [...words]
	const has = (w) => result.some((r) => normalise(r) === normalise(w))
	const original = words.map(normalise)
	for (const p of persons ?? []) {
		const all = [p.name, ...splitList(p.nickname)].filter((s) => s && s.trim())
		if (all.some((w) => original.includes(normalise(w)))) {
			for (const w of all) if (!has(w)) result.push(w.trim())
		}
	}
	return result
}

/** Variablen-taugliche ID: "Jürgen Müller" -> "juergen_mueller" */
function slugify(text, fallback = 'x') {
	const s = normalise(text)
		.replace(/[^a-z0-9]+/g, '_')
		.replace(/^_+|_+$/g, '')
	return s.length > 0 ? s.substring(0, 32) : fallback
}

module.exports = { Matcher, normalise, tokenise, splitList, expandNicknames, slugify }
