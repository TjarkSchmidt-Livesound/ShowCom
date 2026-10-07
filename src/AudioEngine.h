#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "SpeechSegmenter.h"

#include <array>
#include <atomic>
#include <map>

/**
    Audio-Engine

    Audio-Callback (Echtzeit-Thread, ASIO/CoreAudio):
        kopiert jeden aktiven Eingang in einen lock-freien FIFO und misst den Spitzenpegel.
        Keine Allokation, kein Lock, kein Datei-I/O.

    Verarbeitungs-Thread (alle ~10 ms):
        liest die FIFOs, berechnet Pegelanzeigen, schneidet pro zugewiesenem Kanal
        Sprach-Abschnitte (SpeechSegmenter). Fertige Abschnitte gehen über onSegment an die
        Transkription. Es wird nichts auf die Platte geschrieben.
*/
class AudioEngine : private juce::AudioIODeviceCallback,
                    private juce::Thread
{
public:
    static constexpr int maxChannels = 64;

    struct Assignment
    {
        juce::String userId;
        int channel = -1;
        float thresholdDb = -45.0f;
    };

    struct Segment
    {
        juce::String userId;
        int channel = -1;
        std::vector<float> audio;
        double sampleRate = 48000.0;
        double sessionOffsetSec = 0.0;   // Start relativ zum Beginn des Zuhörens
        juce::Time wallClock;
    };

    AudioEngine();
    ~AudioEngine() override;

    /** Wird auf dem Verarbeitungs-Thread aufgerufen. Muss thread-sicher sein. */
    std::function<void (Segment&&)> onSegment;

    juce::AudioDeviceManager& getDeviceManager()            { return deviceManager; }

    /** Öffnet das zuletzt benutzte Gerät (XML aus den Einstellungen). Gibt Fehlertext zurück. */
    juce::String initialise (const juce::String& savedStateXml);
    juce::String createStateXml() const;

    /** Message-Thread. Aktiviert zugewiesene Eingänge automatisch am Interface. */
    void setAssignments (const std::vector<Assignment>& newAssignments);

    // --- Zuhören (Abschnitte an die Transkription weitergeben) ------------------
    void setListening (bool shouldListen);
    bool isListening() const                                { return listening.load(); }
    double getListeningSeconds() const;

    // --- Status für die GUI (beliebiger Thread) -------------------------------
    float getLevelDb (int channel) const;
    bool isSpeaking (int channel) const;
    double getSampleRate() const                            { return deviceSampleRate.load(); }
    int getOverflowCount() const                            { return overflowCount.load(); }
    juce::StringArray getInputChannelNames() const;
    bool isChannelActive (int channel) const;
    juce::String getDeviceDescription() const;

private:
    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& errorMessage) override;

    // Thread
    void run() override;

    struct ChannelFifo
    {
        juce::AbstractFifo fifo { 1 };
        std::vector<float> buffer;
    };

    struct ChannelState
    {
        Assignment assignment;
        SpeechSegmenter segmenter;
        double streamBaseSec = 0.0;   // Zeit seit Beginn des Zuhörens, zu der dieser Segmenter gestartet ist
    };

    void rebuildChannelStates();                       // processLock gehalten
    void prepareState (ChannelState&, double baseSec); // processLock gehalten
    void emitSegment (ChannelState&, SpeechSegmenter::Segment&&);
    double listeningSecondsNow() const;

    juce::AudioDeviceManager deviceManager;

    // Vom Audio-Thread benutzt
    std::array<ChannelFifo, maxChannels> fifos;
    std::array<int, maxChannels> activeInputMap {};    // Callback-Index -> Geräte-Kanal
    std::atomic<int> numActiveInputs { 0 };
    std::array<std::atomic<float>, maxChannels> peaks {};

    // Vom Verarbeitungs-Thread geschrieben, von der GUI gelesen
    std::array<std::atomic<float>, maxChannels> levelsDb {};
    std::array<std::atomic<bool>, maxChannels> speaking {};

    std::atomic<double> deviceSampleRate { 0.0 };
    std::atomic<int> overflowCount { 0 };

    juce::CriticalSection processLock;   // schützt alles darunter + FIFO-Neuallokation
    std::vector<Assignment> pendingAssignments;
    bool assignmentsDirty = false;
    std::map<int, std::unique_ptr<ChannelState>> states;
    double preparedSampleRate = 0.0;
    std::vector<float> scratch;
    std::array<float, maxChannels> smoothedLevel {};

    std::atomic<bool> listening { false };
    juce::Time listenStartTime;
    std::atomic<juce::int64> listenStartMs { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
