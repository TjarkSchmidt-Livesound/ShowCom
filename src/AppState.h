#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <set>
#include <vector>

/** Ein Funkempfänger im Netzwerk. */
struct WirelessDeviceConfig
{
    enum class Type { shure, sennheiserEwdx, simulated, shurePsm, simulatedPsm };

    juce::String id;            // stabil, UUID
    Type type = Type::shure;
    juce::String host;          // IP-Adresse oder Hostname
    juce::String label;         // frei, z. B. "Rack links"
    int numChannels = 0;        // 0 = automatisch
    bool enabled = true;        // aus = nicht verbinden, nicht überwachen
    std::set<int> disabledChannels;   // einzelne Kanäle (Empfänger bzw. Sender) nicht überwachen

    bool isPsm() const          { return type == Type::shurePsm || type == Type::simulatedPsm; }
};

/** Eine Person am Talkback (FOH, Monitor, Licht, Stage-Manager ...). */
struct UserProfile
{
    juce::String id;            // stabil, UUID
    juce::String name;
    juce::String role;          // z. B. "FOH", "Monitor", "Backline"
    juce::String nickname;      // Spitzname(n), kommagetrennt, z. B. "Tjarki, TJ"
    juce::String avatarFile;    // Dateiname im Avatar-Ordner (leer = Initialen)
    juce::Colour colour;
    int channel = -1;           // Index des Eingangs am Audio-Interface (0-basiert), -1 = keiner
    float thresholdDb = -45.0f; // Sprach-Schwelle (RMS, dBFS)
    bool talkbackLog = true;    // Talkback dieser Person mitloggen; aus = nur Funküberwachung (Redner, Band)
    juce::String rfDeviceId;    // zugewiesene Funkstrecke (leer = keine)
    int rfChannel = 0;          // Empfängerkanal 1..4
    bool cutout = true;         // Profilbild freistellen (Hintergrund ausblenden)
    bool favorite = true;       // oben in der Kopfleiste und als eigener Tab zeigen
    bool levelFromWireless = false;   // Pegelverlauf vom Funkempfänger statt vom Audio-Interface
    juce::String iemDeviceId;   // In-Ear-Sender (Shure Axient Digital PSM)
    int iemChannel = 0;
};

struct AppSettings
{
    juce::String modelPath;
    juce::String language = "de";       // "de", "en", "auto" ...
    juce::String keywords;              // "Tjark, Tjarki"
    bool fuzzyKeywords = true;
    bool flashAlert = true;
    bool useGpu = true;
    int recognitionMode = 2;            // 0 = schnell, 1 = ausgewogen, 2 = genau
    juce::String vocabulary;            // eigene Begriffe: Bandname, Songs, Fachwörter
    bool webEnabled = true;             // Browser-Zugriff im lokalen Netz
    int webPort = 8080;
    bool liveText = true;               // Zwischenstände schon während des Sprechens
    bool companionEnabled = true;       // Bitfocus Companion darf den Zustand abfragen
    bool companionControl = true;       // ... und Befehle schicken (Alarm quittieren, Zuhören an/aus)
    juce::String companionKey;          // optionaler Schlüssel (leer = keiner)
    juce::StringArray boardOrder;       // eigene Reihenfolge der Kacheln am Host (Personen-IDs)
    juce::StringArray boardHidden;      // am Host ausgeblendete Kacheln
    juce::StringArray boardBreaks;      // Personen, mit denen eine neue Zeile beginnt
    juce::String audioDeviceState;      // XML von AudioDeviceManager
    std::vector<WirelessDeviceConfig> wirelessDevices;
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

    void load();
    void save() const;

    // --- Benutzer -------------------------------------------------------------
    const std::vector<UserProfile>& getUsers() const       { return users; }
    const UserProfile* findUser (const juce::String& id) const;
    juce::String addUser();
    void updateUser (const UserProfile& user);
    void removeUser (const juce::String& id);
    void moveUser (const juce::String& id, int delta);

    /** Personen, deren Talkback transkribiert wird (Talkback an + Eingang zugewiesen). */
    static bool logsTalkback (const UserProfile& u)          { return u.talkbackLog && u.channel >= 0; }

    /** Liefert den Benutzer, dem ein Eingang zugewiesen ist (oder nullptr). */
    const UserProfile* findUserForChannel (int channel, const juce::String& excludeId = {}) const;

    // --- Board (Dashboard am Host) ---------------------------------------------
    /** Personen in Board-Reihenfolge; ausgeblendete nur mit includeHidden. */
    std::vector<const UserProfile*> getBoardUsers (bool includeHidden) const;
    bool isOnBoard (const juce::String& userId) const      { return ! settings.boardHidden.contains (userId); }

    /** Speichert Reihenfolge/Sichtbarkeit. Meldet nur über boardLayoutChanged (kein Neuaufbau von Audio/Web). */
    void setBoardLayout (const juce::StringArray& order, const juce::StringArray& hidden);
    void setBoardBreaks (const juce::StringArray& breaks);
    juce::ChangeBroadcaster boardLayoutChanged;

    // --- Avatare --------------------------------------------------------------
    /** Lädt ein beliebiges Bild, schneidet es quadratisch zu und speichert es als PNG (plus freigestellte Fassung). */
    bool setAvatarFromFile (const juce::String& userId, const juce::File& imageFile);
    void clearAvatar (const juce::String& userId);

    /** Bild zur Anzeige: freigestellt, wenn eingeschaltet und möglich, sonst das Foto. */
    juce::Image getAvatar (const juce::String& userId) const;
    juce::Image getAvatarOriginal (const juce::String& userId) const;

    enum class CutoutState { noImage, cut, notPossible, off };
    CutoutState getCutoutState (const juce::String& userId) const;
    bool isAvatarCutout (const juce::String& userId) const  { return getCutoutState (userId) == CutoutState::cut; }

    /** Freistellung berechnen (Apple Vision auf dem Mac, sonst eigenes Verfahren). Leeres Bild = nicht möglich. */
    static juce::Image makeCutout (const juce::Image& square);

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
    juce::File cutoutFileFor (const UserProfile&) const;
    juce::Image getCutoutImage (const UserProfile&) const;

    mutable std::map<juce::String, juce::Image> avatarCache;   // Originale
    mutable std::map<juce::String, juce::Image> cutoutCache;   // freigestellt (ungültig = nicht möglich)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppState)
};
