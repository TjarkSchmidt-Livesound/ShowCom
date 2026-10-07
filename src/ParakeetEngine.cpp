#include "ParakeetEngine.h"

#if TBS_HAVE_SHERPA
 // Bewusst die C-Schnittstelle: stabil über Compiler-/Standardbibliotheks-Grenzen hinweg
 #include <sherpa-onnx/c-api/c-api.h>
 #include <cstring>
#endif

namespace
{
    juce::File findFirst (const juce::File& dir, const juce::String& pattern)
    {
        // Bevorzugt die int8-Fassung (kleiner, schneller auf der CPU)
        auto files = dir.findChildFiles (juce::File::findFiles, false, pattern);
        files.sort();
        for (auto& f : files)
            if (f.getFileName().containsIgnoreCase ("int8"))
                return f;
        return files.isEmpty() ? juce::File() : files.getFirst();
    }
}

#if TBS_HAVE_SHERPA
struct ParakeetEngine::Impl
{
    const SherpaOnnxOfflineRecognizer* recognizer = nullptr;
    const SherpaOnnxVoiceActivityDetector* vad = nullptr;

    ~Impl()
    {
        if (recognizer != nullptr) SherpaOnnxDestroyOfflineRecognizer (recognizer);
        if (vad != nullptr)        SherpaOnnxDestroyVoiceActivityDetector (vad);
    }
};
#else
struct ParakeetEngine::Impl {};
#endif

ParakeetEngine::ParakeetEngine() : impl (std::make_unique<Impl>()) {}
ParakeetEngine::~ParakeetEngine() = default;

bool ParakeetEngine::isAvailable()
{
   #if TBS_HAVE_SHERPA
    return true;
   #else
    return false;
   #endif
}

bool ParakeetEngine::isModelDir (const juce::File& dir)
{
    return dir.isDirectory()
        && findFirst (dir, "encoder*.onnx").existsAsFile()
        && findFirst (dir, "decoder*.onnx").existsAsFile()
        && findFirst (dir, "joiner*.onnx").existsAsFile()
        && dir.getChildFile ("tokens.txt").existsAsFile();
}

juce::File ParakeetEngine::findModelDir (const juce::File& fileOrDir)
{
    if (isModelDir (fileOrDir))
        return fileOrDir;
    if (fileOrDir.existsAsFile() && isModelDir (fileOrDir.getParentDirectory()))
        return fileOrDir.getParentDirectory();
    return {};
}

bool ParakeetEngine::writeBpeVocab (const juce::File& tokens, const juce::File& target)
{
    // tokens.txt: "<token> <id>" je Zeile. SentencePiece sortiert die Teilwörter nach Priorität,
    // daher reicht -id als Bewertung für die Zerlegung der Hotwords.
    juce::StringArray lines;
    tokens.readLines (lines);

    juce::String out;
    for (auto& line : lines)
    {
        const auto t = line.trimEnd();
        const int space = t.lastIndexOfChar (' ');
        if (space <= 0)
            continue;

        const auto token = t.substring (0, space);
        const auto id = t.substring (space + 1).getIntValue();
        if (token == "<blk>")
            continue;
        out << token << "\t" << (-id) << "\n";
    }

    return out.isNotEmpty() && target.replaceWithText (out, false, false, "\n");
}

#if TBS_HAVE_SHERPA

bool ParakeetEngine::load (const juce::File& modelDir, int numThreads, juce::String& error)
{
    unload();

    if (! isModelDir (modelDir))
    {
        error = juce::String::fromUTF8 ("Modellordner unvollständig");
        return false;
    }

    // Für die Hotwords (Namen, Fachbegriffe) wird eine BPE-Liste gebraucht
    auto bpe = modelDir.getChildFile ("bpe.vocab");
    if (! bpe.existsAsFile())
    {
        if (! writeBpeVocab (modelDir.getChildFile ("tokens.txt"), bpe))
        {
            // Ordner evtl. schreibgeschützt: im Temp-Ordner anlegen
            bpe = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("showcom_parakeet_bpe.vocab");
            writeBpeVocab (modelDir.getChildFile ("tokens.txt"), bpe);
        }
    }

    // Pfade müssen bis zum Erzeugen leben
    const auto encoder = findFirst (modelDir, "encoder*.onnx").getFullPathName().toStdString();
    const auto decoder = findFirst (modelDir, "decoder*.onnx").getFullPathName().toStdString();
    const auto joiner  = findFirst (modelDir, "joiner*.onnx").getFullPathName().toStdString();
    const auto tokens  = modelDir.getChildFile ("tokens.txt").getFullPathName().toStdString();
    const auto bpePath = bpe.getFullPathName().toStdString();

    SherpaOnnxOfflineRecognizerConfig config;
    std::memset (&config, 0, sizeof (config));
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;   // Parakeet liest die echte Größe (128) aus dem Modell
    config.model_config.transducer.encoder = encoder.c_str();
    config.model_config.transducer.decoder = decoder.c_str();
    config.model_config.transducer.joiner  = joiner.c_str();
    config.model_config.tokens             = tokens.c_str();
    config.model_config.model_type         = "nemo_transducer";
    config.model_config.num_threads        = juce::jlimit (1, 8, numThreads);
    config.model_config.provider           = "cpu";
    config.decoding_method                 = "greedy_search";
    config.max_active_paths                = 4;
    config.hotwords_score                  = 1.5f;

    if (bpe.existsAsFile())
    {
        config.model_config.modeling_unit = "bpe";
        config.model_config.bpe_vocab     = bpePath.c_str();
        config.decoding_method            = "modified_beam_search";
        config.hotwords_score             = 1.8f;   // höher: erfundene Namen an falschen Stellen
    }

    impl->recognizer = SherpaOnnxCreateOfflineRecognizer (&config);
    if (impl->recognizer == nullptr)
    {
        error = "Modell konnte nicht geladen werden";
        return false;
    }

    return true;
}

bool ParakeetEngine::isLoaded() const   { return impl->recognizer != nullptr; }

void ParakeetEngine::unload()
{
    if (impl->recognizer != nullptr)
        SherpaOnnxDestroyOfflineRecognizer (impl->recognizer);
    impl->recognizer = nullptr;
}

juce::String ParakeetEngine::transcribe (const std::vector<float>& pcm, const juce::StringArray& hotwords)
{
    if (impl->recognizer == nullptr || pcm.empty())
        return {};

    // Inline-Hotwords: Zeilen mit '/' getrennt (ein '/' im Begriff selbst geht nicht)
    juce::StringArray clean;
    for (auto h : hotwords)
    {
        h = h.replaceCharacter ('/', ' ').replaceCharacter (':', ' ').trim();
        if (h.length() >= 2 && ! clean.contains (h))
            clean.add (h);
    }

    const auto hw = clean.joinIntoString ("/").toStdString();
    const auto* stream = hw.empty() ? SherpaOnnxCreateOfflineStream (impl->recognizer)
                                    : SherpaOnnxCreateOfflineStreamWithHotwords (impl->recognizer, hw.c_str());
    if (stream == nullptr)
        return {};

    SherpaOnnxAcceptWaveformOffline (stream, 16000, pcm.data(), (int32_t) pcm.size());
    SherpaOnnxDecodeOfflineStream (impl->recognizer, stream);

    juce::String text;
    if (const auto* r = SherpaOnnxGetOfflineStreamResult (stream))
    {
        if (r->text != nullptr)
            text = juce::String::fromUTF8 (r->text).trim();
        SherpaOnnxDestroyOfflineRecognizerResult (r);
    }

    SherpaOnnxDestroyOfflineStream (stream);
    return text;
}

bool ParakeetEngine::loadVad (const juce::File& sileroModel)
{
    if (impl->vad != nullptr)
        SherpaOnnxDestroyVoiceActivityDetector (impl->vad);
    impl->vad = nullptr;

    if (! sileroModel.existsAsFile())
        return false;

    const auto path = sileroModel.getFullPathName().toStdString();

    SherpaOnnxVadModelConfig config;
    std::memset (&config, 0, sizeof (config));
    config.silero_vad.model = path.c_str();
    config.silero_vad.threshold = 0.35f;              // Talkback-Mikros sind oft leise und dumpf: eher großzügig
    config.silero_vad.min_silence_duration = 0.3f;
    config.silero_vad.min_speech_duration = 0.2f;
    config.silero_vad.window_size = 512;
    config.silero_vad.max_speech_duration = 60.0f;
    config.sample_rate = 16000;
    config.num_threads = 1;
    config.provider = "cpu";

    impl->vad = SherpaOnnxCreateVoiceActivityDetector (&config, 70.0f);
    return impl->vad != nullptr;
}

bool ParakeetEngine::hasVad() const    { return impl->vad != nullptr; }

double ParakeetEngine::speechSeconds (const std::vector<float>& pcm)
{
    if (impl->vad == nullptr)
        return -1.0;

    const auto* vad = impl->vad;
    SherpaOnnxVoiceActivityDetectorReset (vad);
    SherpaOnnxVoiceActivityDetectorClear (vad);

    constexpr int window = 512;
    for (size_t i = 0; i + window <= pcm.size(); i += window)
        SherpaOnnxVoiceActivityDetectorAcceptWaveform (vad, pcm.data() + i, window);
    SherpaOnnxVoiceActivityDetectorFlush (vad);

    double total = 0.0;
    while (SherpaOnnxVoiceActivityDetectorEmpty (vad) == 0)
    {
        if (const auto* seg = SherpaOnnxVoiceActivityDetectorFront (vad))
        {
            total += (double) seg->n / 16000.0;
            SherpaOnnxDestroySpeechSegment (seg);
        }
        SherpaOnnxVoiceActivityDetectorPop (vad);
    }

    SherpaOnnxVoiceActivityDetectorReset (vad);
    return total;
}

#else   // ohne sherpa-onnx gebaut

bool ParakeetEngine::load (const juce::File&, int, juce::String& error)
{
    error = juce::String::fromUTF8 ("Diese Version wurde ohne Parakeet gebaut");
    return false;
}
bool ParakeetEngine::isLoaded() const                                          { return false; }
void ParakeetEngine::unload()                                                  {}
juce::String ParakeetEngine::transcribe (const std::vector<float>&, const juce::StringArray&) { return {}; }
bool ParakeetEngine::loadVad (const juce::File&)                               { return false; }
bool ParakeetEngine::hasVad() const                                            { return false; }
double ParakeetEngine::speechSeconds (const std::vector<float>&)               { return -1.0; }

#endif
