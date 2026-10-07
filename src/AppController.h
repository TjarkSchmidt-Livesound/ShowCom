#pragma once

#include "AppState.h"
#include "AudioEngine.h"
#include "Transcriber.h"
#include "TranscriptModel.h"
#include "Wireless.h"
#include "WebServer.h"

/**
    Hält die Kernobjekte zusammen und verdrahtet sie:

        Audio-Interface -> AudioEngine -> Transcriber -> TranscriptModel -> GUI

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
    WebServer web;
    Transcriber transcriber;
    AudioEngine engine;

    /** Zuhören an/aus. Läuft ab Programmstart; Pause z. B. in der Umbaupause. */
    void setListening (bool shouldListen);
    bool isListening() const                    { return engine.isListening(); }

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

    std::function<void()> onListeningChanged;
    std::function<void()> onAudioDeviceChanged;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;   // Momentaufnahme für den Webserver
    std::map<juce::String, std::pair<juce::String, std::shared_ptr<juce::MemoryBlock>>> webAvatarCache;
    void applyState();
    juce::String buildPrompt() const;
    juce::StringArray buildHotwords() const;
    static juce::File getVadModelFile();

    juce::String appliedKeywords;
    bool appliedFuzzy = true;
    bool keywordsApplied = false;
    bool demoMode = false;
};
