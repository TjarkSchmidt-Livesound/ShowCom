// Minimale Tests ohne Framework: ./tbs_tests  (Exit-Code 0 = alles ok)

#include "KeywordMatcher.h"
#include "SpeechSegmenter.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond) do { if (! (cond)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void testNormalise()
{
    CHECK (KeywordMatcher::normalise ("Jürgen") == "juergen");
    CHECK (KeywordMatcher::normalise ("STRAẞE") == "strasse");
    CHECK (KeywordMatcher::normalise ("Ölfaß") == "oelfass");
    CHECK (KeywordMatcher::normalise ("René") == "rene");
    CHECK (KeywordMatcher::levenshtein ("timon", "timmon", 1) == 1);
    CHECK (KeywordMatcher::levenshtein ("timon", "simone", 1) > 1);
}

static void testMatcher()
{
    KeywordMatcher m;
    m.setKeywords ("Timon, Monitor-Timon ,  Jürgen Schmidt");

    {
        auto r = m.findMatches ("Timon, kannst du mal den Wedge vier lauter machen?");
        CHECK (r.size() == 1);
        CHECK (r.size() == 1 && r[0].charStart == 0 && r[0].charLength == 5);
        CHECK (r.size() == 1 && r[0].keyword == "Timon");
    }
    {
        // Ganzes Wort: "Timing" ist kein Treffer, Genitiv schon
        auto r = m.findMatches ("Das Timing passt. Timons Mix ist gut.");
        CHECK (r.size() == 1);
        CHECK (r.size() == 1 && r[0].byteStart == 18);
    }
    {
        // Mehrwort mit Bindestrich, längster Treffer gewinnt
        auto r = m.findMatches ("Hey Monitor-Timon!");
        CHECK (r.size() == 1);
        CHECK (r.size() == 1 && r[0].keyword == "Monitor-Timon");
        CHECK (r.size() == 1 && r[0].charStart == 4 && r[0].charLength == 13);
    }
    {
        // Umlaut-Faltung und Codepoint-Positionen
        auto r = m.findMatches ("Grüße an Juergen Schmidt.");
        CHECK (r.size() == 1);
        CHECK (r.size() == 1 && r[0].charStart == 9 && r[0].charLength == 15);
    }
    {
        // Unscharf: Whisper-Schreibfehler
        auto r = m.findMatches ("timmon bitte melden");
        CHECK (r.size() == 1 && ! r[0].exact);

        m.setFuzzy (false);
        CHECK (m.findMatches ("timmon bitte melden").empty());
        m.setFuzzy (true);
    }
    {
        KeywordMatcher shortName;
        shortName.setKeywords ("Tim");
        CHECK (shortName.findMatches ("Tom, hörst du?").empty());   // kurze Namen nie unscharf
        CHECK (shortName.findMatches ("„Tim“ – bitte!").size() == 1); // typografische Anführungszeichen
    }
    {
        KeywordMatcher empty;
        empty.setKeywords (" , ,");
        CHECK (! empty.hasKeywords());
        CHECK (empty.findMatches ("Timon").empty());
    }
}

static std::vector<float> tone (double sr, double seconds, float amp)
{
    std::vector<float> v ((size_t) (sr * seconds));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = amp * (float) std::sin (2.0 * 3.14159265 * 220.0 * (double) i / sr);
    return v;
}

static void testSegmenter()
{
    const double sr = 48000.0;
    SpeechSegmenter seg;
    SpeechSegmenter::Config c;
    c.sampleRate = sr;
    c.thresholdDb = -40.0f;
    seg.prepare (c);

    std::vector<SpeechSegmenter::Segment> out;
    auto cb = [&] (SpeechSegmenter::Segment&& s) { out.push_back (std::move (s)); };

    auto feed = [&] (const std::vector<float>& v)
    {
        // in krummen Blockgrößen füttern wie ein echtes Interface
        for (size_t i = 0; i < v.size(); i += 333)
            seg.process (v.data() + i, (int) std::min<size_t> (333, v.size() - i), cb);
    };

    feed (tone (sr, 1.0, 0.0001f));   // Stille (-80 dB)
    feed (tone (sr, 1.5, 0.3f));      // Sprache
    feed (tone (sr, 1.5, 0.0001f));   // Stille -> Abschnitt endet

    CHECK (out.size() == 1);
    if (out.size() == 1)
    {
        const double len = (double) out[0].audio.size() / sr;
        const double start = (double) out[0].startSample / sr;
        CHECK (len > 1.6 && len < 2.2);           // 1.5 s + Pre-Roll + kurzer Ausklang
        CHECK (start > 0.6 && start < 0.75);      // ~300 ms vor dem Einsatz
    }

    // Kurzer Klick wird verworfen
    out.clear();
    feed (tone (sr, 0.06, 0.5f));
    feed (tone (sr, 1.5, 0.0001f));
    CHECK (out.empty());

    // Langer Monolog wird bei maxSegmentSec geteilt
    out.clear();
    feed (tone (sr, 60.0, 0.3f));
    feed (tone (sr, 1.5, 0.0001f));
    CHECK (out.size() == 3);
    for (auto& s : out)
        CHECK ((double) s.audio.size() / sr <= 25.5);

    // Flush gibt laufenden Abschnitt aus
    out.clear();
    feed (tone (sr, 2.0, 0.3f));
    seg.flush (cb);
    CHECK (out.size() == 1);
}

int main()
{
    testNormalise();
    testMatcher();
    testSegmenter();

    if (failures == 0)
        std::printf ("All tests passed.\n");

    return failures == 0 ? 0 : 1;
}
