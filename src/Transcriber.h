#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <deque>

struct whisper_context;

/**
    Transkription mit whisper.cpp auf einem eigenen Hintergrund-Thread.

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
    };

    struct Result
    {
        juce::String userId;
        juce::String text;
        juce::Time wallClock;
        double sessionOffsetSec = 0.0;
        double durationSec = 0.0;
        double processingSec = 0.0;
    };

    struct Options
    {
        juce::File model;
        juce::String language = "de";
        juce::String prompt;          // Namen/Begriffe, damit Whisper sie richtig schreibt
        bool useGpu = true;
        bool accurate = false;        // Beam-Search
    };

    enum class ModelState { none, loading, ready, failed };

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
    void transcribe (Job&);

    juce::CriticalSection lock;            // schützt pendingOptions und queue
    Options pendingOptions, activeOptions;
    bool optionsChanged = false;
    std::deque<Job> queue;
    juce::WaitableEvent wakeUp;

    whisper_context* ctx = nullptr;
    juce::String loadedModelPath;
    bool loadedWithGpu = false;

    std::atomic<int> queueSize { 0 };
    std::atomic<bool> busy { false };
    std::atomic<ModelState> modelState { ModelState::none };
    std::atomic<double> lastRtf { 0.0 };
    juce::String statusText;               // geschützt durch lock

    // Damit verspätete callAsync-Lambdas nach dem Zerstören nichts mehr anfassen
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Transcriber)
};
