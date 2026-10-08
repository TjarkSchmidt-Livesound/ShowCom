#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "AppState.h"
#include "ShureParser.h"
#include "WirelessState.h"

#include <deque>
#include <map>
#include <set>

/**
    Netzwerk-Anbindung von Funkempfängern.

    - Shure Axient Digital (AD/ADX), QLX-D, SLX-D, SLX-D+: TCP-Port 2202, ASCII-Befehle (ShureParser)
    - Shure Axient Digital PSM (ADTD/ADTQ): In-Ear-Sender, gleiche Verbindung, Sender-Modus
    - Sennheiser EW-DX (EM 2 / EM 4): Sound Control Protocol v1, JSON über UDP-Port 45
    - Simuliert: für den Demo-Modus ohne Hardware

    Jedes Gerät läuft in einem eigenen Thread; die GUI liest Momentaufnahmen.
*/
class WirelessClient : public juce::Thread
{
public:
    WirelessClient (const WirelessDeviceConfig& c, const juce::String& name)
        : juce::Thread (name), config (c) {}

    const WirelessDeviceConfig config;

    bool isOnline() const;
    juce::String getStatusText() const;
    juce::String getModel() const;
    std::map<int, WirelessChannelState> snapshot() const;

    /** Die letzten empfangenen Rohdaten und unbekannten Schlüssel (Diagnose). */
    juce::String getRawLog() const;

protected:
    void logRaw (const juce::String& text);
    juce::StringArray rawLog;
    std::set<std::string> unknownKeys;
    void setOnline (bool online, const juce::String& status);
    void setModel (const juce::String& m);
    void updateChannel (int ch, const WirelessChannelState& s);
    void markAlive();

    mutable juce::CriticalSection lock;
    std::map<int, WirelessChannelState> channels;
    bool online = false;
    juce::String statusText { "Verbinde ..." }, model;
    juce::int64 lastDataMs = 0;
};

//==============================================================================
class ShureClient : public WirelessClient
{
public:
    explicit ShureClient (const WirelessDeviceConfig&);
    ~ShureClient() override;
    static constexpr int port = 2202;

private:
    void run() override;
    std::unique_ptr<juce::StreamingSocket> socket;
};

//==============================================================================
class SennheiserClient : public WirelessClient
{
public:
    explicit SennheiserClient (const WirelessDeviceConfig&);
    ~SennheiserClient() override;
    static constexpr int port = 45;

    /** Wertet eine SSCv1-Antwort aus (öffentlich für Tests). */
    static void applyJson (const juce::var& json, std::map<int, WirelessChannelState>& channels, std::set<int>& changed);
    static juce::StringArray subscribeMessages (int numChannels);

private:
    void run() override;
};

//==============================================================================
class SimulatedClient : public WirelessClient
{
public:
    explicit SimulatedClient (const WirelessDeviceConfig&);
    ~SimulatedClient() override;

private:
    void run() override;
};

/** Simulierter In-Ear-Sender (Demo). */
class SimulatedPsmClient : public WirelessClient
{
public:
    explicit SimulatedPsmClient (const WirelessDeviceConfig&);
    ~SimulatedPsmClient() override;

private:
    void run() override;
};

//==============================================================================
/** Verwaltet alle Empfänger und hält die Verläufe für die Grafiken. Nur Message-Thread. */
class WirelessManager : private juce::Timer
{
public:
    static constexpr int historyLength = 240;   // 60 s bei 4 Hz
    static constexpr const char* demoPsmDeviceId = "demo-psm";

    WirelessManager();
    ~WirelessManager() override;

    void setDevices (const std::vector<WirelessDeviceConfig>& devices);
    void setDemoDevice (bool enabled);

    struct ChannelRef
    {
        juce::String deviceId;
        int channel = 0;
        juce::String label;
        bool iem = false;     // In-Ear-Sender statt Empfänger
    };

    /** Alle bekannten Kanäle für Auswahllisten. */
    std::vector<ChannelRef> listChannels() const;

    /** Abgeschaltetes Gerät oder abgeschalteter Kanal: nicht überwachen, nicht warnen. */
    bool isDisabled (const juce::String& deviceId, int channel) const;

    /** Audiopegel vom Funkempfänger (0..1, 4 Hz). */
    const std::deque<float>* getAudioHistory (const juce::String& deviceId, int channel) const;

    juce::String getRawLog (const juce::String& deviceId) const;

    bool getChannel (const juce::String& deviceId, int channel, WirelessChannelState& out) const;
    const std::deque<float>* getHistory (const juce::String& deviceId, int channel) const;

    struct DeviceStatus { bool exists = false, online = false; juce::String text, model; };
    DeviceStatus getStatus (const juce::String& deviceId) const;

    static juce::String typeName (WirelessDeviceConfig::Type);
    static int defaultChannelCount (const WirelessDeviceConfig&);

    static constexpr const char* demoDeviceId = "demo";

private:
    void timerCallback() override;
    std::unique_ptr<WirelessClient> createClient (const WirelessDeviceConfig&);

    std::vector<std::unique_ptr<WirelessClient>> clients;
    std::map<juce::String, std::map<int, WirelessChannelState>> cache;
    std::map<juce::String, std::deque<float>> histories, audioHistories;
    std::vector<WirelessDeviceConfig> configs;   // inkl. abgeschalteter Geräte
    bool demoEnabled = false;
};
