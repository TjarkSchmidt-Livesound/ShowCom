// Prüft die echte Spracherkennung mit Audiodateien (nur lokal, braucht ein Modell):
//   SHOWCOM_TEST_MODEL=<Parakeet-Ordner oder ggml-*.bin>  tbs_asr_test <ordner-mit-wavs> [referenz.txt]
// Ohne Modell wird der Test übersprungen (Exit 0).

#include "Transcriber.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <cstdio>

static juce::StringArray words (juce::String s)
{
    s = s.toLowerCase().replace ("-", " ");
    juce::String clean;
    for (auto c : s)
        clean += juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c) : juce::String (" ");
    juce::StringArray w;
    w.addTokens (clean, " ", "");
    w.removeEmptyStrings();
    return w;
}

static int editDistance (const juce::StringArray& a, const juce::StringArray& b)
{
    std::vector<int> d ((size_t) b.size() + 1);
    for (int j = 0; j <= b.size(); ++j) d[(size_t) j] = j;
    for (int i = 1; i <= a.size(); ++i)
    {
        auto prev = d;
        d[0] = i;
        for (int j = 1; j <= b.size(); ++j)
            d[(size_t) j] = juce::jmin (prev[(size_t) j] + 1, d[(size_t) j - 1] + 1, prev[(size_t) j - 1] + (a[i - 1] != b[j - 1] ? 1 : 0));
    }
    return d[(size_t) b.size()];
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    const auto modelPath = juce::SystemStats::getEnvironmentVariable ("SHOWCOM_TEST_MODEL", {});
    if (modelPath.isEmpty() || argc < 2)
    {
        std::printf ("ASR-Test übersprungen (SHOWCOM_TEST_MODEL nicht gesetzt)\n");
        return 0;
    }

    const juce::File dir (juce::String::fromUTF8 (argv[1]));
    juce::StringArray refs;
    if (argc > 2)
        juce::File (juce::String::fromUTF8 (argv[2])).readLines (refs);
    refs.removeEmptyStrings();

    Transcriber t;
    Transcriber::Options o;
    o.model = juce::File (modelPath);
    o.vadModel = juce::File (juce::SystemStats::getEnvironmentVariable ("SHOWCOM_TEST_VAD", {}));
    o.hotwords.addTokens (juce::SystemStats::getEnvironmentVariable ("SHOWCOM_TEST_HOTWORDS", {}), ",", "");
    o.prompt = "Talkback bei einer Live-Veranstaltung. " + o.hotwords.joinIntoString (", ");
    t.setOptions (o);

    std::map<int, Transcriber::Result> results;
    t.onResult = [&] (const Transcriber::Result& r) { if (! r.partial) results[r.utteranceId] = r; };

    auto files = dir.findChildFiles (juce::File::findFiles, false, juce::SystemStats::getEnvironmentVariable ("SHOWCOM_TEST_PATTERN", "*.wav"));
    // natürliche Reihenfolge: x_2 vor x_10
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    double audioSec = 0.0;
    for (int i = 0; i < files.size(); ++i)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (files[i]));
        if (reader == nullptr)
            continue;

        juce::AudioBuffer<float> buf (1, (int) reader->lengthInSamples);
        reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, false);

        Transcriber::Job job;
        job.userId = "test";
        job.audio.assign (buf.getReadPointer (0), buf.getReadPointer (0) + buf.getNumSamples());
        job.sampleRate = reader->sampleRate;
        job.utteranceId = i;
        audioSec += (double) buf.getNumSamples() / reader->sampleRate;
        t.enqueue (std::move (job));
    }

    // Auf Ergebnisse warten (Modell laden + rechnen)
    const auto start = juce::Time::getMillisecondCounterHiRes();
    while ((int) results.size() < files.size() && juce::Time::getMillisecondCounterHiRes() - start < 600000.0)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    double proc = 0.0;
    int errors = 0, total = 0;
    for (int i = 0; i < files.size(); ++i)
    {
        const auto& r = results[i];
        proc += r.processingSec;
        std::printf ("%-16s %5.2fs  %s\n", files[i].getFileName().toRawUTF8(), r.processingSec, r.text.toRawUTF8());
        if (i < refs.size())
        {
            const auto rw = words (refs[i]);
            errors += editDistance (rw, words (r.text));
            total += rw.size();
        }
    }

    std::printf ("\n%s: %d Dateien, %.1f s Audio, %.1f s Rechenzeit (RTF %.3f), %d verworfen",
                 t.getModelStatusText().toRawUTF8(), files.size(), audioSec, proc, audioSec > 0 ? proc / audioSec : 0.0,
                 t.getRejectedCount());
    if (total > 0)
        std::printf (", WER %.1f %%", 100.0 * errors / total);
    std::printf ("\n");
    return 0;
}
