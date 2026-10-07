#pragma once

#include "AppState.h"
#include "AudioEngine.h"
#include "Transcriber.h"
#include "TranscriptModel.h"
#include "Wireless.h"

/**
    Hält die Kernobjekte zusammen und verdrahtet sie:

        Audio-Interface -> AudioEngine -> Transcriber -> TranscriptModel -> GUI
                                    \-> WAV-Dateien

    Reihenfolge der Member ist wichtig: engine wird zuerst zerstört (stoppt Audio),
    dann transcriber, dann transcript und state.
*/
class AppController : private juce::ChangeListener,
                      private juce::Timer
{
public:
    AppController();
    ~AppController() override;

    AppState state;
    TranscriptModel transcript;
    WirelessManager wireless;
    Transcriber transcriber;
    AudioEngine engine;

    bool startSession (juce::String& errorOut);
    void stopSession();
    bool isSessionRunning() const               { return engine.isSessionRunning(); }
    juce::File getLastSessionFolder() const     { return lastSessionFolder; }

    /** Löst einen Test-Eintrag mit Erwähnung aus (Einstellungen -> "Alarm testen"). */
    void injectTestMention();

    /** Füllt die Anzeige mit Beispiel-Talkback (Start mit --demo), um die Oberfläche ohne Audio zu testen. */
    void injectDemoConversation();

    /** Funkstrecke einer Person (zugewiesen oder im Demo-Modus simuliert). */
    struct RfInfo
    {
        bool assigned = false, online = false, known = false;
        WirelessChannelState state;
        const std::deque<float>* history = nullptr;
        juce::String deviceName;
    };
    RfInfo getRfInfo (const UserProfile&) const;

    std::function<void()> onSessionStateChanged;
    std::function<void()> onAudioDeviceChanged;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;   // schließt das Protokoll, sobald die Transkription fertig ist
    void applyState();
    juce::String buildPrompt() const;

    juce::File lastSessionFolder;
    juce::String appliedKeywords;
    bool appliedFuzzy = true;
    bool keywordsApplied = false;
    int closeWaitTicks = 0;
    bool demoMode = false;
};
