#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <deque>

#include "ParakeetEngine.h"

struct whisper_context;

/**
    Transkription auf einem eigenen Hintergrund-Thread.

    Zwei Erkennungen:
    - Parakeet (Standard): NVIDIA Parakeet TDT v3 über sherpa-onnx – schnell, sparsam, kein Erfinden.
      Modell = Ordner mit encoder/decoder/joiner/tokens.
    - Whisper (whisper.cpp): Modell = ggml-*.bin-Datei.

    Vor der Erkennung prüft Silero VAD, ob im Abschnitt überhaupt gesprochen wird.
    Abschnitte aus der Audio-Engine werden in eine Warteschlange gestellt, auf 16 kHz
    gewandelt, normalisiert und nacheinander transkribiert. Ergebnisse kommen über
    onResult auf dem Message-Thread an.
*/
class Transcriber : private juce::Thread
{
public:
    struct Job
    {
        juce::String userId;
        std::vector<float> audio;
        double sampleRate = 48000.0;
        double sessionOffsetSec = 0.0;
        juce::Time wallClock;
        bool partial = false;      // Zwischenstand, während noch gesprochen wird
        int utteranceId = 0;       // gleiche Ansage -> gleiche Id (Zwischenstände + Endergebnis)
    };

    struct Result
    {
        juce::String userId;
        juce::String text;
        juce::Time wallClock;
        double sessionOffsetSec = 0.0;
        double durationSec = 0.0;
        double processingSec = 0.0;
        bool partial = false;
        int utteranceId = 0;
    };

    struct Options
    {
        juce::File model;             // Parakeet-Ordner oder Whisper-Datei
        juce::File vadModel;          // silero_vad.onnx (leer = keine Sprachprüfung)
        juce::String language = "de";
        juce::String prompt;          // Namen/Begriffe, damit Whisper sie richtig schreibt
        juce::StringArray hotwords;   // dasselbe für Parakeet: Namen, Alarm-Namen, eigene Begriffe
        bool useGpu = true;
        int mode = 2;                 // Whisper: 0 = schnell, 1 = ausgewogen, 2 = genau (Endergebnisse)
        int threads = 0;              // 0 = automatisch (sparsam)
    };

    enum class ModelState { none, loading, ready, failed };
    enum class Engine { none, parakeet, whisper };

    static Engine engineForModel (const juce::File&);
    Engine getEngine() const                    { return engine.load(); }

    /** Zählt Abschnitte, die die Sprachprüfung verworfen hat (Musik, Rauschen). */
    int getRejectedCount() const                { return rejected.load(); }

    Transcriber();
    ~Transcriber() override;

    /** Message-Thread. Lädt das Modell bei Bedarf im Hintergrund neu. */
    void setOptions (const Options&);

    /** Beliebiger Thread. */
    void enqueue (Job&&);

    int getQueueSize() const                    { return queueSize.load(); }
    bool isBusy() const                         { return busy.load(); }
    ModelState getModelState() const            { return modelState.load(); }
    juce::String getModelStatusText() const;
    double getLastRealtimeFactor() const        { return lastRtf.load(); }

    std::function<void (const Result&)> onResult;   // Message-Thread

    /** Entfernt typische Whisper-Halluzinationen ("Untertitel im Auftrag des ZDF" ...). */
    static juce::String cleanText (const juce::String& raw);

private:
    void run() override;
    void loadModelIfNeeded();
    std::vector<float> prepareAudio (const Job&) const;
    bool isReady() const;
    void loadVadIfNeeded();
    void transcribe (Job&);

    juce::CriticalSection lock;            // schützt pendingOptions und queue
    Options pendingOptions, activeOptions;
    bool optionsChanged = false;
    std::deque<Job> queue;
    juce::WaitableEvent wakeUp;

    whisper_context* ctx = nullptr;
    ParakeetEngine parakeet;
    juce::String loadedModelPath, loadedVadPath;
    bool loadedWithGpu = false;
    int loadedThreads = 0;
    std::atomic<Engine> engine { Engine::none };
    std::atomic<int> rejected { 0 };

    std::atomic<int> queueSize { 0 };
    std::atomic<bool> busy { false };
    std::atomic<ModelState> modelState { ModelState::none };
    std::atomic<double> lastRtf { 0.0 };
    juce::String statusText;               // geschützt durch lock

    // Damit verspätete callAsync-Lambdas nach dem Zerstören nichts mehr anfassen
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Transcriber)
};
