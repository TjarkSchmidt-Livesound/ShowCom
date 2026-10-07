#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <vector>

/** Eine Person am Talkback (FOH, Monitor, Licht, Stage-Manager ...). */
struct UserProfile
{
    juce::String id;            // stabil, UUID
    juce::String name;
    juce::String role;          // z. B. "FOH", "Monitor", "Backline"
    juce::String avatarFile;    // Dateiname im Avatar-Ordner (leer = Initialen)
    juce::Colour colour;
    int channel = -1;           // Index des Eingangs am Audio-Interface (0-basiert), -1 = keiner
    float thresholdDb = -45.0f; // Sprach-Schwelle (RMS, dBFS)
};

struct AppSettings
{
    juce::String modelPath;
    juce::String language = "de";       // "de", "en", "auto" ...
    juce::String keywords;              // "Timon, Timo"
    bool fuzzyKeywords = true;
    bool flashAlert = true;
    bool recordAudio = true;
    bool useGpu = true;
    bool accurateDecoding = false;      // Beam-Search statt Greedy
    juce::String recordingsDir;
    juce::String audioDeviceState;      // XML von AudioDeviceManager
};

/**
    Zentrale, persistente Konfiguration (Benutzer + Einstellungen).
    Nur vom Message-Thread benutzen. Änderungen werden per ChangeBroadcaster gemeldet.
*/
class AppState : public juce::ChangeBroadcaster
{
public:
    AppState();
    ~AppState() override;

    static juce::File getAppDataDir();
    static juce::File getAvatarDir();
    static juce::File getModelsDir();
    juce::File getRecordingsDir() const;

    void load();
    void save() const;

    // --- Benutzer -------------------------------------------------------------
    const std::vector<UserProfile>& getUsers() const       { return users; }
    const UserProfile* findUser (const juce::String& id) const;
    juce::String addUser();
    void updateUser (const UserProfile& user);
    void removeUser (const juce::String& id);
    void moveUser (const juce::String& id, int delta);

    /** Liefert den Benutzer, dem ein Eingang zugewiesen ist (oder nullptr). */
    const UserProfile* findUserForChannel (int channel, const juce::String& excludeId = {}) const;

    // --- Avatare --------------------------------------------------------------
    /** Lädt ein beliebiges Bild, schneidet es quadratisch zu und speichert es als PNG. */
    bool setAvatarFromFile (const juce::String& userId, const juce::File& imageFile);
    void clearAvatar (const juce::String& userId);
    juce::Image getAvatar (const juce::String& userId) const;

    // --- Einstellungen --------------------------------------------------------
    const AppSettings& getSettings() const                  { return settings; }
    void updateSettings (const AppSettings& s);

    /** Speichert den Audio-Geräte-Zustand ohne Änderungs-Meldung (kein GUI-Neuaufbau). */
    void setAudioDeviceState (const juce::String& xml);

    static juce::Colour paletteColour (int index);

private:
    void changed();

    std::vector<UserProfile> users;
    AppSettings settings;
    mutable std::map<juce::String, juce::Image> avatarCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppState)
};
