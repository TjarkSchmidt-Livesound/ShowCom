#pragma once

#include <string>
#include <vector>

/**
    Findet Erwähnungen von Namen/Schlüsselwörtern in transkribiertem Text.

    - Groß-/Kleinschreibung egal, Umlaute werden gefaltet (ä -> ae, ß -> ss, é -> e ...),
      damit "Jürgen", "Juergen" und "jurgen" zusammenpassen.
    - Ganze Wörter: "Tim" matcht nicht in "Timing".
    - Mehrwort-Begriffe ("Tjark Schmidt") werden als Wortfolge gesucht.
    - Optional unscharf: Bei Wörtern ab 5 Zeichen ist 1 Tippfehler erlaubt
      (Whisper schreibt Namen gern leicht anders, z. B. "Timmon").

    Reines C++17 ohne Abhängigkeiten, damit es unabhängig getestet werden kann.
    Alle Texte sind UTF-8.
*/
class KeywordMatcher
{
public:
    struct Match
    {
        size_t byteStart = 0;   // Position im Original-UTF-8-Text
        size_t byteLength = 0;
        size_t charStart = 0;   // Position in Unicode-Codepoints (für die GUI)
        size_t charLength = 0;
        std::string keyword;    // welcher Begriff gefunden wurde (wie konfiguriert)
        bool exact = true;      // false = unscharfer Treffer
    };

    /** Begriffe kommagetrennt oder zeilenweise, z. B. "Tjark, Tjarki, Monitor-Tjark". */
    void setKeywords (const std::string& commaOrNewlineSeparated);
    void setKeywords (const std::vector<std::string>& keywords);
    void setFuzzy (bool shouldBeFuzzy)  { fuzzy = shouldBeFuzzy; }

    bool hasKeywords() const            { return ! entries.empty(); }

    std::vector<Match> findMatches (const std::string& utf8Text) const;

    // --- Hilfsfunktionen (öffentlich für Tests) ---------------------------------
    static std::string normalise (const std::string& utf8Word);
    static int levenshtein (const std::string& a, const std::string& b, int maxDistance);

private:
    struct Token
    {
        std::string norm;
        size_t byteStart, byteEnd, charStart, charEnd;
    };

    struct Entry
    {
        std::string original;
        std::vector<std::string> words;   // normalisiert
    };

    static std::vector<Token> tokenise (const std::string& utf8Text);
    bool wordMatches (const std::string& textWord, const std::string& keyWord, bool& exactOut) const;

    std::vector<Entry> entries;
    bool fuzzy = true;
};
