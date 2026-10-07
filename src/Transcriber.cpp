#include "Transcriber.h"

#include <whisper.h>

namespace
{
    constexpr double whisperRate = 16000.0;
    constexpr size_t maxQueue = 300;

    void quietLog (ggml_log_level level, const char* text, void*)
    {
        juce::ignoreUnused (level, text);
       #if JUCE_DEBUG
        if (level >= GGML_LOG_LEVEL_WARN)
            DBG ("whisper: " << juce::String::fromUTF8 (text).trimEnd());
       #endif
    }
}

Transcriber::Transcriber()
    : juce::Thread ("ShowCom Transcriber")
{
    whisper_log_set (quietLog, nullptr);
    statusText = juce::String::fromUTF8 ("Kein Modell gewählt");
    startThread (juce::Thread::Priority::normal);
}

Transcriber::~Transcriber()
{
    alive->store (false);
    signalThreadShouldExit();
    wakeUp.signal();
    stopThread (30000);   // eine laufende Transkription darf noch zu Ende rechnen

    if (ctx != nullptr)
        whisper_free (ctx);
}

void Transcriber::setOptions (const Options& o)
{
    const juce::ScopedLock sl (lock);
    pendingOptions = o;
    optionsChanged = true;
    wakeUp.signal();
}

void Transcriber::enqueue (Job&& job)
{
    {
        const juce::ScopedLock sl (lock);

        if (queue.size() >= maxQueue)
            queue.pop_front();   // Notbremse: lieber Altes verwerfen als ewig hinterherhinken

        queue.push_back (std::move (job));
        queueSize.store ((int) queue.size());
    }

    wakeUp.signal();
}

juce::String Transcriber::getModelStatusText() const
{
    const juce::ScopedLock sl (lock);
    return statusText;
}

//==============================================================================
void Transcriber::loadModelIfNeeded()
{
    Options opts;
    {
        const juce::ScopedLock sl (lock);
        if (! optionsChanged)
            return;

        opts = pendingOptions;
        optionsChanged = false;
    }

    activeOptions = opts;

    const auto path = opts.model.getFullPathName();
    if (ctx != nullptr && path == loadedModelPath && opts.useGpu == loadedWithGpu)
        return;   // nur Sprache/Prompt geändert: kein Neuladen nötig

    if (ctx != nullptr)
    {
        whisper_free (ctx);
        ctx = nullptr;
        loadedModelPath = {};
    }

    if (path.isEmpty() || ! opts.model.existsAsFile())
    {
        modelState.store (ModelState::none);
        const juce::ScopedLock sl (lock);
        statusText = path.isEmpty() ? juce::String::fromUTF8 ("Kein Modell gewählt") : juce::String ("Modelldatei fehlt");
        return;
    }

    modelState.store (ModelState::loading);
    {
        const juce::ScopedLock sl (lock);
        statusText = "Lade " + opts.model.getFileName() + " ...";
    }

    auto cparams = whisper_context_default_params();
    cparams.use_gpu = opts.useGpu;
    cparams.flash_attn = opts.useGpu;

    ctx = whisper_init_from_file_with_params (path.toRawUTF8(), cparams);

    if (ctx == nullptr && opts.useGpu)
    {
        // Fallback: manche Treiber/GPUs mögen nicht – dann CPU
        cparams.use_gpu = false;
        cparams.flash_attn = false;
        ctx = whisper_init_from_file_with_params (path.toRawUTF8(), cparams);
    }

    const juce::ScopedLock sl (lock);

    if (ctx == nullptr)
    {
        modelState.store (ModelState::failed);
        statusText = "Modell konnte nicht geladen werden";
        return;
    }

    loadedModelPath = path;
    loadedWithGpu = opts.useGpu;
    modelState.store (ModelState::ready);
    statusText = opts.model.getFileNameWithoutExtension().replace ("ggml-", "")
               + (cparams.use_gpu ? " · GPU" : " · CPU");
}

//==============================================================================
std::vector<float> Transcriber::prepareAudio (const Job& job) const
{
    const int n = (int) job.audio.size();
    std::vector<float> in (job.audio);

    // Anti-Aliasing vor dem Heruntertakten auf 16 kHz (2 × 2. Ordnung)
    if (job.sampleRate > whisperRate)
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            juce::IIRFilter lp;
            lp.setCoefficients (juce::IIRCoefficients::makeLowPass (job.sampleRate, 7000.0, 0.707));
            lp.processSamples (in.data(), n);
        }
    }

    const double ratio = job.sampleRate / whisperRate;
    const int outLen = (int) ((double) n / ratio);
    std::vector<float> out ((size_t) juce::jmax (outLen, 0));

    if (outLen > 0)
    {
        juce::LagrangeInterpolator interp;
        interp.process (ratio, in.data(), out.data(), outLen);
    }

    // Leise Talkback-Mikros anheben (max. +30 dB), Spitze auf ca. -6 dBFS
    float peak = 0.0f;
    for (float s : out)
        peak = juce::jmax (peak, std::abs (s));

    if (peak > 1.0e-5f)
    {
        const float gain = juce::jmin (0.5f / peak, 31.6f);
        for (auto& s : out)
            s *= gain;
    }

    // whisper.cpp braucht mindestens 1 s Audio
    const size_t minLen = (size_t) (whisperRate * 1.1);
    if (out.size() < minLen)
        out.resize (minLen, 0.0f);

    return out;
}

void Transcriber::transcribe (Job& job)
{
    const auto startMs = juce::Time::getMillisecondCounterHiRes();
    const auto pcm = prepareAudio (job);

    auto params = whisper_full_default_params (activeOptions.accurate ? WHISPER_SAMPLING_BEAM_SEARCH
                                                                      : WHISPER_SAMPLING_GREEDY);

    const std::string language = activeOptions.language.isEmpty() ? std::string ("auto")
                                                                    : activeOptions.language.toStdString();
    const std::string prompt = activeOptions.prompt.toStdString();

    params.n_threads        = juce::jlimit (2, 8, juce::SystemStats::getNumPhysicalCpus());
    params.language         = language.c_str();
    params.detect_language  = false;
    params.translate        = false;
    params.no_context       = true;     // jeder Abschnitt unabhängig: keine Fehlerfortpflanzung
    params.no_timestamps    = true;
    params.single_segment   = false;
    params.print_progress   = false;
    params.print_realtime   = false;
    params.print_special    = false;
    params.print_timestamps = false;
    params.suppress_blank   = true;
    params.suppress_nst     = true;
    params.initial_prompt   = prompt.empty() ? nullptr : prompt.c_str();
    params.beam_search.beam_size = 5;
    params.greedy.best_of        = 2;

    if (whisper_full (ctx, params, pcm.data(), (int) pcm.size()) != 0)
        return;

    juce::String text;
    float maxNoSpeech = 0.0f, minNoSpeech = 1.0f;
    const int numSegments = whisper_full_n_segments (ctx);

    for (int i = 0; i < numSegments; ++i)
    {
        text << juce::String::fromUTF8 (whisper_full_get_segment_text (ctx, i));
        const float p = whisper_full_get_segment_no_speech_prob (ctx, i);
        maxNoSpeech = juce::jmax (maxNoSpeech, p);
        minNoSpeech = juce::jmin (minNoSpeech, p);
    }

    text = cleanText (text);

    // Whisper "hört" in Rauschen gern kurze Floskeln – wenn das Modell selbst unsicher ist, weg damit.
    if (numSegments > 0 && minNoSpeech > 0.6f && text.length() < 25)
        text = {};

    const double duration = (double) job.audio.size() / job.sampleRate;
    const double elapsed = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0;
    lastRtf.store (duration > 0.0 ? elapsed / duration : 0.0);

    if (text.isEmpty())
        return;

    Result r;
    r.userId = job.userId;
    r.text = text;
    r.wallClock = job.wallClock;
    r.sessionOffsetSec = job.sessionOffsetSec;
    r.durationSec = duration;
    r.processingSec = elapsed;

    juce::MessageManager::callAsync ([this, flag = alive, r]
    {
        if (flag->load() && onResult)
            onResult (r);
    });
}

void Transcriber::run()
{
    while (! threadShouldExit())
    {
        wakeUp.wait (250);
        loadModelIfNeeded();

        if (ctx == nullptr)
            continue;   // Jobs bleiben liegen, bis ein Modell geladen ist

        while (! threadShouldExit())
        {
            Job job;
            {
                const juce::ScopedLock sl (lock);
                if (queue.empty())
                    break;

                busy.store (true);   // vor dem Entnehmen setzen: Queue leer + nicht busy heißt wirklich fertig
                job = std::move (queue.front());
                queue.pop_front();
                queueSize.store ((int) queue.size());
            }

            transcribe (job);
            busy.store (false);

            loadModelIfNeeded();   // Modellwechsel zwischen zwei Abschnitten erlauben
            if (ctx == nullptr)
                break;
        }
    }
}

//==============================================================================
juce::String Transcriber::cleanText (const juce::String& raw)
{
    juce::String t;

    // Klammer-Anmerkungen entfernen: [Musik], (Applaus), *Lachen*, ♪
    int depth = 0;
    bool inStars = false;

    for (auto p = raw.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;

        if (c == '[' || c == '(') { ++depth; continue; }
        if (c == ']' || c == ')') { depth = juce::jmax (0, depth - 1); continue; }
        if (c == '*')             { inStars = ! inStars; continue; }
        if (c == 0x266A || c == 0x266B) continue;   // ♪ ♫

        if (depth == 0 && ! inStars)
            t += juce::String::charToString (c);
    }

    t = t.replace ("\n", " ").trim();
    while (t.contains ("  "))
        t = t.replace ("  ", " ");

    // Nur Satzzeichen übrig?
    bool hasLetter = false;
    for (auto p = t.getCharPointer(); ! p.isEmpty(); ++p)
        if (juce::CharacterFunctions::isLetterOrDigit (*p))
            hasLetter = true;

    if (! hasLetter)
        return {};

    // Bekannte Halluzinationen (stammen aus Untertiteln im Trainingsmaterial)
    static const char* phrases[] = {
        "untertitel im auftrag des zdf", "untertitelung des zdf", "untertitel von stephanie geiges",
        "untertitel der amara.org-community", "untertitelung aufgrund der amara.org-community",
        "vielen dank für's zuschauen", "vielen dank fürs zuschauen", "danke fürs zuschauen",
        "bis zum nächsten mal", "copyright wdr", "swr 20", "zdf 20", "zdf, 20",
        "thank you for watching", "thanks for watching", "subtitles by the amara.org community",
        "please subscribe", "abonniert den kanal"
    };

    const auto lower = t.toLowerCase();
    for (auto* ph : phrases)
    {
        const auto phrase = juce::String::fromUTF8 (ph);
        if (lower.contains (phrase) && lower.length() < phrase.length() + 25)
            return {};
    }

    return t;
}
