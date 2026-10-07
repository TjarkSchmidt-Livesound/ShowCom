// Minimale Tests ohne Framework: ./tbs_tests  (Exit-Code 0 = alles ok)

#include "KeywordMatcher.h"
#include "SpeechSegmenter.h"
#include "ShureParser.h"
#include "Cutout.h"

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
        CHECK (len > 1.8 && len < 2.4);           // 1.5 s + Pre-Roll + kurzer Ausklang
        CHECK (start > 0.55 && start < 0.65);     // ~400 ms vor dem Einsatz
    }

    // Kurzer Klick wird verworfen
    out.clear();
    feed (tone (sr, 0.06, 0.5f));
    feed (tone (sr, 1.5, 0.0001f));
    CHECK (out.empty());

    // Langer Monolog wird bei maxSegmentSec geteilt
    out.clear();
    feed (tone (sr, 50.0, 0.3f));
    feed (tone (sr, 1.5, 0.0001f));
    CHECK (out.size() == 3);
    for (auto& s : out)
        CHECK ((double) s.audio.size() / sr <= 20.5);

    // Flush gibt laufenden Abschnitt aus
    out.clear();
    feed (tone (sr, 2.0, 0.3f));
    seg.flush (cb);
    CHECK (out.size() == 1);
}

static void testShure()
{
    {
        // ULX-D: Antwort in zwei TCP-Paketen zerteilt, gepolsterter Name
        ShureParser p;
        p.feed ("< REP MODEL {ULXD4Q                          } >< REP 1 CHAN_NAME {HH01    } >< REP 1 BATT_BA");
        auto changed = p.feed ("RS 004 >< REP 1 BATT_RUN_TIME 00479 >< REP 1 FREQUENCY 470125 >"
                               "< REP 1 TX_TYPE ULXD2 >< REP 1 BATT_CHARGE 096 >< SAMPLE 1 ALL AB 081 040 >");
        const auto& s = p.getChannels().at (1);
        CHECK (p.getFamily() == ShureParser::Family::ulxd);
        CHECK (changed.count (1) == 1);
        CHECK (s.name == "HH01");
        CHECK (s.batteryBars == 4 && s.batteryPercent == 96 && s.batteryMinutes == 479);
        CHECK (s.frequencyKHz == 470125);
        CHECK (s.txOn && s.txModel == "ULXD2");
        CHECK (std::abs (s.rfDbm - (-47.0f)) < 0.01f);      // 081 - 128
        CHECK (std::abs (s.audioDb - (-10.0f)) < 0.01f);    // 040 - 50
        CHECK (s.antenna == "AB");

        // Sender aus: Akkuwerte verschwinden
        p.feed ("< REP 1 TX_TYPE UNKN >");
        CHECK (! p.getChannels().at (1).txOn);
        CHECK (p.getChannels().at (1).batteryBars == -1);
        CHECK (p.getChannels().at (1).batteryPercentEstimate() == -1);
    }
    {
        // SLX-D: andere Schlüssel und Offsets
        ShureParser p;
        p.feed ("< REP 2 TX_MODEL SLXD2 >< REP 2 TX_BATT_BARS 003 >< REP 2 TX_BATT_MINS 65534 >"
                "< REP 2 FREQUENCY 0602125 >< REP 2 RSSI 1 083 >< REP 2 RSSI 2 090 >");
        const auto& s = p.getChannels().at (2);
        CHECK (p.getFamily() == ShureParser::Family::slxd);
        CHECK (s.batteryBars == 3 && s.batteryMinutes == -1);   // 65534 = wird berechnet
        CHECK (s.batteryPercentEstimate() == 60);
        CHECK (s.frequencyKHz == 602125);
        CHECK (std::abs (s.rfDbm - (-30.0f)) < 0.01f);           // stärkere Antenne: 090 - 120
    }
    {
        // Axient Digital: Meter mit 9 Feldern
        ShureParser p;
        p.feed ("< REP 1 TX_MODEL AD2 >< REP 1 TX_BATT_CHARGE_PERCENT 255 >< SAMPLE 1 ALL 005 031 102 102 BB 31 086 31 065 >");
        const auto& s = p.getChannels().at (1);
        CHECK (p.getFamily() == ShureParser::Family::axientDigital);
        CHECK (s.batteryPercent == -1);
        CHECK (std::abs (s.rfDbm - (-34.0f)) < 0.01f);           // max(086, 065) - 120
        CHECK (std::abs (s.audioDb - (-18.0f)) < 0.01f);
    }
    {
        ShureParser p;
        CHECK (p.feed ("garbage without brackets").empty());
        CHECK (p.feed ("< ERR >").empty());
        CHECK (p.getChannels().empty());
    }
}

static void testCutout()
{
    // Synthetisches Porträt: Wand mit Verlauf + Rauschen, Kopf (Kreis) und Oberkörper bis zum unteren Rand
    const int w = 160, h = 160;
    std::vector<uint8_t> img ((size_t) (w * h * 4));
    unsigned rnd = 12345;
    auto noise = [&rnd] { rnd = rnd * 1103515245u + 12345u; return (int) ((rnd >> 16) % 9) - 4; };
    auto inPerson = [] (int x, int y)
    {
        const int dx = x - 80, dy = y - 62;
        const bool head = dx * dx + dy * dy < 30 * 30;
        const int bx = x - 80, by = y - 175;
        const bool body = bx * bx / 4 + by * by < 62 * 62 / 1 && y > 100;
        return head || body;
    };

    int personPixels = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            auto* p = &img[(size_t) ((y * w + x) * 4)];
            const bool person = inPerson (x, y);
            personPixels += person ? 1 : 0;
            const int shade = 205 - y / 3 - std::abs (x - 80) / 6;
            p[0] = (uint8_t) (person ? (y < 95 ? 222 : 40) + noise() : shade + noise());
            p[1] = (uint8_t) (person ? (y < 95 ? 182 : 60) + noise() : shade - 3 + noise());
            p[2] = (uint8_t) (person ? (y < 95 ? 150 : 120) + noise() : shade - 8 + noise());
            p[3] = 255;
        }

    auto r = Cutout::removeBackground (img.data(), w, h);
    CHECK (r.ok);
    if (r.ok)
    {
        CHECK (r.alpha[(size_t) (62 * w + 80)] == 255);    // Gesicht
        CHECK (r.alpha[(size_t) (150 * w + 80)] == 255);   // Oberkörper am unteren Rand
        CHECK (r.alpha[(size_t) (5 * w + 5)] == 0);        // Wand oben links
        CHECK (r.alpha[(size_t) (108 * w + 3)] == 0);      // Wand links neben der Schulter (dunkler)
        CHECK (std::abs (r.foregroundFraction - (float) personPixels / (float) (w * h)) < 0.04f);
    }

    // PNG mit Transparenz bleibt, wie es ist
    for (int i = 0; i < w * h; ++i)
        img[(size_t) i * 4 + 3] = (uint8_t) ((i % w) < 40 ? 0 : 255);
    r = Cutout::removeBackground (img.data(), w, h);
    CHECK (r.ok && r.alreadyTransparent && r.alpha[0] == 0 && r.alpha[(size_t) (w / 2)] == 255);

    // Völlig unruhiges Bild: lieber nichts freistellen
    for (auto& v : img)
        v = (uint8_t) (noise() * 30 + 128);
    for (int i = 0; i < w * h; ++i)
        img[(size_t) i * 4 + 3] = 255;
    r = Cutout::removeBackground (img.data(), w, h);
    CHECK (! r.ok);
}

int main()
{
    testNormalise();
    testMatcher();
    testSegmenter();
    testShure();
    testCutout();

    if (failures == 0)
        std::printf ("All tests passed.\n");

    return failures == 0 ? 0 : 1;
}
