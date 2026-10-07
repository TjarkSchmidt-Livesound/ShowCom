#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

/**
    Schneidet einen kontinuierlichen Mono-Audiostrom in Sprach-Abschnitte.

    Talkback-Mikros sind meist offen oder per Taster geschaltet; dazwischen liegt
    Stille bzw. Bühnen-Übersprechen. Ein pegelbasiertes Gate mit Vorlauf (Pre-Roll),
    Nachlauf (Hangover) und Mindestlänge reicht dafür zuverlässig aus und kostet
    praktisch keine CPU. Der Schwellwert ist pro Person einstellbar.

    Nicht echtzeitkritisch: läuft auf dem Verarbeitungs-Thread, nicht im Audio-Callback.
*/
class SpeechSegmenter
{
public:
    struct Config
    {
        double sampleRate     = 48000.0;
        float  thresholdDb    = -45.0f;  // RMS-Schwelle in dBFS
        float  frameMs        = 20.0f;
        float  preRollMs      = 300.0f;  // Audio vor dem Einsatz mitnehmen (Anlaute!)
        float  hangoverMs     = 500.0f;  // so lange unter der Schwelle, bis der Abschnitt endet
        float  minSpeechMs    = 250.0f;  // kürzere Abschnitte (Klicks, Huster) verwerfen
        float  maxSegmentSec  = 20.0f;   // Whisper verarbeitet max. 30 s am Stück
    };

    struct Segment
    {
        std::vector<float> audio;
        int64_t startSample = 0;   // Position im Strom (seit reset)
        int utteranceId = 0;       // fortlaufende Nummer der Ansage
    };

    using Callback = std::function<void (Segment&&)>;

    SpeechSegmenter();

    void prepare (const Config& newConfig);
    void setThresholdDb (float db)      { config.thresholdDb = db; }
    float getThresholdDb() const        { return config.thresholdDb; }

    /** Gibt fertige Abschnitte über den Callback aus. */
    void process (const float* samples, int numSamples, const Callback& onSegment);

    /** Gibt einen laufenden Abschnitt sofort aus (z. B. beim Stoppen der Session). */
    void flush (const Callback& onSegment);

    void reset();

    bool isInSpeech() const             { return inSpeech; }

    /** Für Live-Zwischenstände: bisher aufgenommene Audiodaten der laufenden Ansage. */
    const std::vector<float>& getCurrentAudio() const   { return current; }
    int64_t getCurrentStart() const                     { return currentStart; }
    int getUtteranceId() const                          { return utteranceId; }
    float getLastFrameDb() const        { return lastFrameDb; }

private:
    void processFrame (const Callback& onSegment);
    void emit (const Callback& onSegment);

    Config config;
    int frameSize = 960, preRollFrames = 15, hangoverFrames = 35, minSpeechFrames = 13, maxFrames = 1250;

    std::vector<float> frame;
    int frameFill = 0;

    std::deque<std::vector<float>> preRoll;

    std::vector<float> current;
    int64_t currentStart = 0;
    int64_t streamPos = 0;      // Samples, die vollständig in Frames verarbeitet wurden
    int speechFrames = 0, silentFrames = 0, segmentFrames = 0;
    bool inSpeech = false;
    float lastFrameDb = -120.0f;
    int utteranceId = 0;
};
