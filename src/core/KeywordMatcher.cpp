#include "KeywordMatcher.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace
{
    // Dekodiert einen UTF-8-Codepoint ab pos; setzt len. Ungültige Bytes zählen als 1 Zeichen.
    uint32_t decodeUtf8 (const std::string& s, size_t pos, size_t& len)
    {
        const auto c0 = static_cast<unsigned char> (s[pos]);

        auto cont = [&] (size_t i) -> int
        {
            if (pos + i >= s.size()) return -1;
            const auto c = static_cast<unsigned char> (s[pos + i]);
            return (c & 0xC0) == 0x80 ? (c & 0x3F) : -1;
        };

        if (c0 < 0x80) { len = 1; return c0; }

        if ((c0 & 0xE0) == 0xC0)
        {
            const int c1 = cont (1);
            if (c1 >= 0) { len = 2; return ((c0 & 0x1Fu) << 6) | (uint32_t) c1; }
        }
        else if ((c0 & 0xF0) == 0xE0)
        {
            const int c1 = cont (1), c2 = cont (2);
            if (c1 >= 0 && c2 >= 0) { len = 3; return ((c0 & 0x0Fu) << 12) | ((uint32_t) c1 << 6) | (uint32_t) c2; }
        }
        else if ((c0 & 0xF8) == 0xF0)
        {
            const int c1 = cont (1), c2 = cont (2), c3 = cont (3);
            if (c1 >= 0 && c2 >= 0 && c3 >= 0)
            {
                len = 4;
                return ((c0 & 0x07u) << 18) | ((uint32_t) c1 << 12) | ((uint32_t) c2 << 6) | (uint32_t) c3;
            }
        }

        len = 1;
        return 0xFFFD;
    }

    void appendUtf8 (std::string& out, uint32_t cp)
    {
        if (cp < 0x80)        { out += (char) cp; }
        else if (cp < 0x800)  { out += (char) (0xC0 | (cp >> 6));  out += (char) (0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000){ out += (char) (0xE0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); }
        else                  { out += (char) (0xF0 | (cp >> 18)); out += (char) (0x80 | ((cp >> 12) & 0x3F));
                                out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); }
    }

    bool isWordChar (uint32_t cp)
    {
        if (cp < 0x80)
            return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9');

        switch (cp)
        {
            case 0x00A0: case 0x00AB: case 0x00BB: case 0x00BF: case 0x00A1: case 0x00B7:
            case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014:
            case 0x2018: case 0x2019: case 0x201A: case 0x201C: case 0x201D: case 0x201E:
            case 0x2026: case 0x2039: case 0x203A: case 0xFFFD:
                return false;
            default:
                return true;
        }
    }

    // Faltet einen Codepoint auf eine kleingeschriebene ASCII-nahe Form.
    void foldCodepoint (std::string& out, uint32_t cp)
    {
        if (cp >= 'A' && cp <= 'Z') { out += (char) (cp - 'A' + 'a'); return; }
        if (cp < 0x80)              { out += (char) cp; return; }

        switch (cp)
        {
            case 0xC4: case 0xE4: out += "ae"; return;   // Ä ä
            case 0xD6: case 0xF6: out += "oe"; return;   // Ö ö
            case 0xDC: case 0xFC: out += "ue"; return;   // Ü ü
            case 0xDF: case 0x1E9E: out += "ss"; return; // ß ẞ
            case 0xC6: case 0xE6: out += "ae"; return;   // Æ æ
            default: break;
        }

        if ((cp >= 0xC0 && cp <= 0xC5) || (cp >= 0xE0 && cp <= 0xE5)) { out += 'a'; return; }
        if (cp == 0xC7 || cp == 0xE7 || cp == 0x10C || cp == 0x10D)     { out += 'c'; return; }
        if ((cp >= 0xC8 && cp <= 0xCB) || (cp >= 0xE8 && cp <= 0xEB))   { out += 'e'; return; }
        if ((cp >= 0xCC && cp <= 0xCF) || (cp >= 0xEC && cp <= 0xEF))   { out += 'i'; return; }
        if (cp == 0xD1 || cp == 0xF1)                                    { out += 'n'; return; }
        if ((cp >= 0xD2 && cp <= 0xD5) || cp == 0xD8
            || (cp >= 0xF2 && cp <= 0xF5) || cp == 0xF8)                 { out += 'o'; return; }
        if ((cp >= 0xD9 && cp <= 0xDB) || (cp >= 0xF9 && cp <= 0xFB))   { out += 'u'; return; }
        if (cp == 0xDD || cp == 0xFD || cp == 0xFF)                      { out += 'y'; return; }
        if (cp == 0x160 || cp == 0x161)                                  { out += 's'; return; }
        if (cp == 0x17D || cp == 0x17E)                                  { out += 'z'; return; }
        if (cp == 0x141 || cp == 0x142)                                  { out += 'l'; return; }

        appendUtf8 (out, cp);
    }
}

std::string KeywordMatcher::normalise (const std::string& utf8Word)
{
    std::string out;
    out.reserve (utf8Word.size() + 4);

    for (size_t pos = 0; pos < utf8Word.size();)
    {
        size_t len = 1;
        const auto cp = decodeUtf8 (utf8Word, pos, len);
        foldCodepoint (out, cp);
        pos += len;
    }

    return out;
}

int KeywordMatcher::levenshtein (const std::string& a, const std::string& b, int maxDistance)
{
    const int n = (int) a.size(), m = (int) b.size();

    if (std::abs (n - m) > maxDistance)
        return maxDistance + 1;

    std::vector<int> prev ((size_t) m + 1), cur ((size_t) m + 1);

    for (int j = 0; j <= m; ++j)
        prev[(size_t) j] = j;

    for (int i = 1; i <= n; ++i)
    {
        cur[0] = i;
        int rowMin = cur[0];

        for (int j = 1; j <= m; ++j)
        {
            const int cost = a[(size_t) i - 1] == b[(size_t) j - 1] ? 0 : 1;
            cur[(size_t) j] = std::min ({ prev[(size_t) j] + 1, cur[(size_t) j - 1] + 1, prev[(size_t) j - 1] + cost });
            rowMin = std::min (rowMin, cur[(size_t) j]);
        }

        if (rowMin > maxDistance)
            return maxDistance + 1;

        std::swap (prev, cur);
    }

    return prev[(size_t) m];
}

std::vector<KeywordMatcher::Token> KeywordMatcher::tokenise (const std::string& text)
{
    std::vector<Token> tokens;

    size_t pos = 0, charIndex = 0;
    bool inWord = false;
    Token current {};
    std::string raw;

    auto finish = [&]
    {
        if (inWord)
        {
            current.norm = normalise (raw);
            tokens.push_back (current);
            raw.clear();
            inWord = false;
        }
    };

    while (pos < text.size())
    {
        size_t len = 1;
        const auto cp = decodeUtf8 (text, pos, len);

        if (isWordChar (cp))
        {
            if (! inWord)
            {
                inWord = true;
                current = {};
                current.byteStart = pos;
                current.charStart = charIndex;
            }

            raw.append (text, pos, len);
            current.byteEnd = pos + len;
            current.charEnd = charIndex + 1;
        }
        else
        {
            finish();
        }

        pos += len;
        ++charIndex;
    }

    finish();
    return tokens;
}

void KeywordMatcher::setKeywords (const std::string& list)
{
    std::vector<std::string> items;
    std::string cur;

    for (char c : list)
    {
        if (c == ',' || c == ';' || c == '\n' || c == '\r')
        {
            items.push_back (cur);
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }

    items.push_back (cur);
    setKeywords (items);
}

void KeywordMatcher::setKeywords (const std::vector<std::string>& keywords)
{
    entries.clear();

    for (const auto& k : keywords)
    {
        Entry e;

        for (auto& t : tokenise (k))
            e.words.push_back (t.norm);

        if (e.words.empty())
            continue;

        // Original ohne führende/folgende Leerzeichen merken
        const auto first = k.find_first_not_of (" \t");
        const auto last  = k.find_last_not_of (" \t");
        e.original = k.substr (first, last - first + 1);

        entries.push_back (std::move (e));
    }
}

bool KeywordMatcher::wordMatches (const std::string& textWord, const std::string& keyWord, bool& exactOut) const
{
    if (textWord == keyWord)
        return true;

    // Deutscher Genitiv / Plural: "Tims Mikro", "Timons Monitor"
    if (textWord.size() == keyWord.size() + 1 && textWord.back() == 's'
        && textWord.compare (0, keyWord.size(), keyWord) == 0)
        return true;

    if (fuzzy && keyWord.size() >= 5)
    {
        if (levenshtein (textWord, keyWord, 1) <= 1)
        {
            exactOut = false;
            return true;
        }
    }

    return false;
}

std::vector<KeywordMatcher::Match> KeywordMatcher::findMatches (const std::string& text) const
{
    std::vector<Match> found;

    if (entries.empty())
        return found;

    const auto tokens = tokenise (text);

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        for (const auto& e : entries)
        {
            if (i + e.words.size() > tokens.size())
                continue;

            bool exact = true;
            bool ok = true;

            for (size_t w = 0; w < e.words.size() && ok; ++w)
                ok = wordMatches (tokens[i + w].norm, e.words[w], exact);

            if (! ok)
                continue;

            const auto& first = tokens[i];
            const auto& last  = tokens[i + e.words.size() - 1];

            Match m;
            m.byteStart  = first.byteStart;
            m.byteLength = last.byteEnd - first.byteStart;
            m.charStart  = first.charStart;
            m.charLength = last.charEnd - first.charStart;
            m.keyword    = e.original;
            m.exact      = exact;
            found.push_back (m);
        }
    }

    // Überlappungen auflösen: früher beginnend gewinnt, bei Gleichstand der längere / exakte Treffer.
    std::sort (found.begin(), found.end(), [] (const Match& a, const Match& b)
    {
        if (a.byteStart != b.byteStart) return a.byteStart < b.byteStart;
        if (a.byteLength != b.byteLength) return a.byteLength > b.byteLength;
        return a.exact && ! b.exact;
    });

    std::vector<Match> result;
    size_t coveredUntil = 0;

    for (auto& m : found)
    {
        if (! result.empty() && m.byteStart < coveredUntil)
            continue;

        coveredUntil = m.byteStart + m.byteLength;
        result.push_back (std::move (m));
    }

    return result;
}
