#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "WirelessState.h"

#include <map>
#include <memory>
#include <set>

/**
    Eingebauter Webserver für den Browser-Zugriff im lokalen Netz.

    - Jedes Gerät (Handy, Tablet, Laptop) bekommt beim ersten Aufruf eine eigene Geräte-ID
      und damit eine eigene Oberfläche mit eigenem Alarm-Namen.
    - Über das Web lassen sich Personen nur ein- und ausblenden.
      Personen anlegen/löschen und Geräte entfernen geht nur am Host.

    Die Daten kommen als Momentaufnahme (publish) vom Message-Thread; die Anfragen laufen in
    einem kleinen Thread-Pool und fassen die App-Objekte nie direkt an.
*/
class WebServer : private juce::Thread
{
public:
    WebServer();
    ~WebServer() override;

    struct Person
    {
        juce::String id, name, role, colour;   // colour als "#rrggbb"
        juce::String avatarKey;                // ändert sich, wenn sich das Bild ändert (leer = keins)
        bool talkback = true, speaking = false;
        bool rfAssigned = false, rfOnline = false, rfKnown = false;
        WirelessChannelState rf;
        std::vector<float> history;            // HF-Verlauf 0..1
    };

    struct Entry
    {
        juce::int64 id = 0;
        juce::String userId, time, text;
        int utterance = -1;
    };

    struct Snapshot
    {
        std::vector<Person> persons;
        std::vector<Entry> entries;   // die letzten Einträge
        std::vector<Entry> live;      // was gerade gesprochen wird
        bool listening = false;
        std::map<juce::String, std::shared_ptr<juce::MemoryBlock>> avatars;   // userId -> PNG
    };

    /** Neue Momentaufnahme (beliebiger Thread). */
    void publish (std::shared_ptr<const Snapshot>);

    /** Message-Thread. Startet/stoppt den Server bzw. wechselt den Port. */
    void configure (bool enabled, int port);

    bool isRunning() const                       { return running.load(); }
    int getPort() const                          { return currentPort.load(); }
    juce::String getStatusText() const;
    juce::StringArray getUrls() const;

    // --- Geräte (Browser-Clients) -----------------------------------------------
    struct Device
    {
        juce::String id;
        juce::String label;        // frei, z. B. "iPad Monitor"
        juce::String keywords;     // eigener Alarm-Name (kommagetrennt)
        std::set<juce::String> hidden;
        juce::int64 lastSeenMs = 0;
    };

    std::vector<Device> getDevices() const;
    void removeDevice (const juce::String& id);   // nur am Host

    // --- Für Tests ------------------------------------------------------------
    struct Response
    {
        int status = 200;
        juce::String contentType = "text/plain; charset=utf-8";
        juce::MemoryBlock body;
        bool cacheable = false;
    };

    Response handle (const juce::String& method, const juce::String& target, const juce::String& body);
    void setDevicesFile (const juce::File& f);

private:
    void run() override;               // Accept-Schleife
    void serve (std::unique_ptr<juce::StreamingSocket>);

    Response stateJson (const juce::String& deviceId);
    Response jsonResponse (const juce::var&, int status = 200) const;
    Response textResponse (const juce::String& text, int status, const juce::String& type) const;

    void loadDevices();
    void saveDevices() const;          // devicesLock gehalten
    juce::String createDevice();

    std::unique_ptr<juce::StreamingSocket> listener;
    std::unique_ptr<juce::ThreadPool> pool;
    std::atomic<bool> running { false };
    std::atomic<int> currentPort { 0 };
    juce::String status;
    mutable juce::CriticalSection statusLock;

    mutable juce::CriticalSection snapshotLock;
    std::shared_ptr<const Snapshot> snapshot;

    mutable juce::CriticalSection devicesLock;
    std::map<juce::String, Device> devices;
    juce::File devicesFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebServer)
};
