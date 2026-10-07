// Integrationstest der Audio-Engine mit einem simulierten 8-Kanal-Interface.
// Prüft Kanal-Zuordnung, FIFO, Sprach-Schnitt, Zeitstempel und WAV-Mitschnitt.

#include "AudioEngine.h"
#include "Wireless.h"
#include "WebServer.h"
#include <cstdio>
#include <cstring>

namespace
{
    constexpr double fakeRate = 48000.0;
    constexpr int fakeBlock = 256;
    constexpr int fakeInputs = 8;
    constexpr int talkChannel = 2;   // "Eingang 3"

    // Pegelmuster auf Eingang 3: (Startzeit s, Dauer s)
    const std::vector<std::pair<double, double>> bursts { { 1.0, 1.5 }, { 4.0, 1.0 } };

    class FakeDevice : public juce::AudioIODevice, private juce::Thread
    {
    public:
        FakeDevice() : AudioIODevice ("Fake 8in", "Fake"), Thread ("fake audio") {}
        ~FakeDevice() override { close(); }

        juce::StringArray getOutputChannelNames() override { return {}; }
        juce::StringArray getInputChannelNames() override
        {
            juce::StringArray s;
            for (int i = 0; i < fakeInputs; ++i) s.add ("In " + juce::String (i + 1));
            return s;
        }
        juce::Array<double> getAvailableSampleRates() override  { return { fakeRate }; }
        juce::Array<int> getAvailableBufferSizes() override     { return { fakeBlock }; }
        int getDefaultBufferSize() override                     { return fakeBlock; }

        juce::String open (const juce::BigInteger& ins, const juce::BigInteger&, double, int) override
        {
            active = ins;
            active.setRange (fakeInputs, 64, false);
            opened = true;
            return {};
        }
        void close() override                     { stop(); opened = false; }
        bool isOpen() override                    { return opened; }
        void start (juce::AudioIODeviceCallback* cb) override
        {
            callback = cb;
            callback->audioDeviceAboutToStart (this);
            startThread (juce::Thread::Priority::highest);
        }
        void stop() override
        {
            stopThread (2000);
            if (callback != nullptr) { callback->audioDeviceStopped(); callback = nullptr; }
        }
        bool isPlaying() override                 { return callback != nullptr; }
        juce::String getLastError() override      { return {}; }
        int getCurrentBufferSizeSamples() override{ return fakeBlock; }
        double getCurrentSampleRate() override    { return fakeRate; }
        int getCurrentBitDepth() override         { return 24; }
        juce::BigInteger getActiveOutputChannels() const override { return {}; }
        juce::BigInteger getActiveInputChannels() const override  { return active; }
        int getOutputLatencyInSamples() override  { return 0; }
        int getInputLatencyInSamples() override   { return 0; }

    private:
        void run() override
        {
            juce::AudioBuffer<float> buf (fakeInputs, fakeBlock);
            juce::int64 pos = 0;
            const double start = juce::Time::getMillisecondCounterHiRes();

            while (! threadShouldExit())
            {
                buf.clear();
                for (int s = 0; s < fakeBlock; ++s)
                {
                    const double t = (double) (pos + s) / fakeRate;
                    float v = 0.00005f * std::sin ((float) t * 1234.0f);   // Rauschteppich ~ -86 dB
                    for (auto& [b0, len] : bursts)
                        if (t >= b0 && t < b0 + len)
                            v = 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * 300.0f * (float) t);
                    buf.setSample (talkChannel, s, v);
                    buf.setSample (0, s, 0.3f * std::sin ((float) t * 2000.0f));   // nicht zugewiesen
                }

                // Nur aktive Kanäle übergeben, wie ein echter Treiber
                std::vector<const float*> ins;
                for (int ch = 0; ch < fakeInputs; ++ch)
                    if (active[ch]) ins.push_back (buf.getReadPointer (ch));

                callback->audioDeviceIOCallbackWithContext (ins.data(), (int) ins.size(), nullptr, 0, fakeBlock, {});
                pos += fakeBlock;

                // Echtzeit simulieren
                const double due = start + 1000.0 * (double) pos / fakeRate;
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (due > now) juce::Thread::sleep ((int) (due - now));
            }
        }

        juce::AudioIODeviceCallback* callback = nullptr;
        juce::BigInteger active;
        bool opened = false;
    };

    class FakeType : public juce::AudioIODeviceType
    {
    public:
        FakeType() : AudioIODeviceType ("Fake") {}
        void scanForDevices() override {}
        juce::StringArray getDeviceNames (bool wantInputs) const override { return wantInputs ? juce::StringArray ("Fake 8in") : juce::StringArray(); }
        int getDefaultDeviceIndex (bool) const override { return 0; }
        int getIndexOfDevice (juce::AudioIODevice*, bool asInput) const override { return asInput ? 0 : -1; }
        bool hasSeparateInputsAndOutputs() const override { return true; }
        juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override { return new FakeDevice(); }
    };

    int failures = 0;
    void check (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok) ++failures;
    }
}

namespace
{
    // Simulierter Shure ULX-D4D im Netzwerk
    class FakeShure : public juce::Thread
    {
    public:
        explicit FakeShure (int p) : Thread ("fake shure"), port (p) { listener.createListener (port, "127.0.0.1"); startThread(); }
        ~FakeShure() override { signalThreadShouldExit(); listener.close(); if (client) client->close(); stopThread (2000); }

        juce::String received;

    private:
        void run() override
        {
            client.reset (listener.waitForNextConnection());
            if (client == nullptr) return;

            char buf[1024];
            bool replied = false;

            while (! threadShouldExit() && client->isConnected())
            {
                if (client->waitUntilReady (true, 100) > 0)
                {
                    const int n = client->read (buf, sizeof (buf), false);
                    if (n <= 0) break;
                    received += juce::String::fromUTF8 (buf, n);
                }

                if (! replied && received.contains ("GET 0 ALL"))
                {
                    send ("< REP MODEL {ULXD4D                          } >"
                          "< REP 1 CHAN_NAME {HH01    } >< REP 1 TX_TYPE ULXD2 >< REP 1 BATT_BARS 004 >"
                          "< REP 1 BATT_CHARGE 087 >< REP 1 BATT_RUN_TIME 00312 >< REP 1 FREQUENCY 470125 >"
                          "< REP 2 CHAN_NAME {BP02    } >< REP 2 TX_TYPE UNKN >< REP 2 FREQUENCY 471300 >");
                    replied = true;
                }

                if (replied)
                    send ("< SAMPLE 1 ALL AB 078 060 >");
            }
        }

        void send (const char* s)   { client->write (s, (int) strlen (s)); }

        juce::StreamingSocket listener;
        std::unique_ptr<juce::StreamingSocket> client;
        int port;
    };

    // Simulierter Sennheiser EW-DX EM 2 (SSCv1, UDP)
    class FakeEwdx : public juce::Thread
    {
    public:
        explicit FakeEwdx (int p) : Thread ("fake ewdx"), sock (false) { sock.bindToPort (p, "127.0.0.1"); startThread(); }
        ~FakeEwdx() override { signalThreadShouldExit(); sock.shutdown(); stopThread (2000); }

        int subscriptions = 0;

    private:
        void run() override
        {
            char buf[4096];
            while (! threadShouldExit())
            {
                if (sock.waitUntilReady (true, 100) <= 0) continue;
                juce::String ip; int port = 0;
                const int n = sock.read (buf, sizeof (buf), false, ip, port);
                if (n <= 0) continue;

                const auto msg = juce::String::fromUTF8 (buf, n);
                if (! msg.contains ("subscribe")) continue;
                ++subscriptions;

                const char* replies[] = {
                    R"({"osc":{"state":{"subscribe":[{"#":{"lifetime":60},"rx1":{"name":"VOX 1","frequency":548000,"mute":false}}]}}})",
                    R"({"mates":{"tx1":{"battery":{"gauge":80,"lifetime":372,"type":"Battery"},"type":"SKM","mute":false}}})",
                    R"({"m":{"rx1":{"rsqi":92,"divi":2}}})",
                    R"({"m":{"rx2":{"rsqi":0,"divi":0}}})",
                };
                for (auto* r : replies)
                    sock.write (ip, port, r, (int) strlen (r));
            }
        }

        juce::DatagramSocket sock;
    };

    template <typename Fn>
    bool waitFor (Fn&& condition, int ms)
    {
        for (int t = 0; t < ms; t += 50)
        {
            if (condition()) return true;
            juce::Thread::sleep (50);
        }
        return condition();
    }

    void testWireless()
    {
        {
            FakeShure fake (22020);
            WirelessDeviceConfig c;
            c.id = "shure"; c.type = WirelessDeviceConfig::Type::shure; c.host = "127.0.0.1:22020";
            ShureClient client (c);

            const bool ok = waitFor ([&] { auto s = client.snapshot(); return s.count (1) && s[1].batteryPercent == 87 && ! std::isnan (s[1].rfDbm); }, 4000);
            check (ok, "Shure: Verbindung, Akku und HF-Pegel empfangen");
            check (client.isOnline(), "Shure: Status online");
            check (fake.received.contains ("METER_RATE"), "Shure: Meter-Rate angefordert");

            auto s = client.snapshot();
            if (ok)
            {
                std::printf ("      Kanal 1: %s, %d %%, %d min, %.3f MHz, %.0f dBm, Modell %s\n", s[1].name.c_str(),
                             s[1].batteryPercent, s[1].batteryMinutes, s[1].frequencyKHz / 1000.0, s[1].rfDbm,
                             client.getModel().toRawUTF8());
                check (s[1].name == "HH01" && s[1].batteryMinutes == 312 && std::abs (s[1].rfDbm + 50.0f) < 0.1f, "Shure: Werte korrekt umgerechnet");
                check (s.count (2) && ! s[2].txOn, "Shure: ausgeschalteter Sender erkannt");
            }
        }
        {
            FakeEwdx fake (24545);
            WirelessDeviceConfig c;
            c.id = "ewdx"; c.type = WirelessDeviceConfig::Type::sennheiserEwdx; c.host = "127.0.0.1:24545"; c.numChannels = 2;
            SennheiserClient client (c);

            const bool ok = waitFor ([&] { auto s = client.snapshot(); return s.count (1) && s[1].batteryPercent == 80 && s[1].rfQualityPercent == 92 && s[1].frequencyKHz > 0; }, 4000);
            check (ok, "Sennheiser EW-DX: Abo, Akku, Signalqualität und Frequenz empfangen");

            auto s = client.snapshot();
            if (ok)
            {
                std::printf ("      Kanal 1: %s, %d %%, %d min, %.3f MHz, RSQI %d %%, Antenne %s\n", s[1].name.c_str(),
                             s[1].batteryPercent, s[1].batteryMinutes, s[1].frequencyKHz / 1000.0, s[1].rfQualityPercent, s[1].antenna.c_str());
                check (s[1].name == "VOX 1" && s[1].batteryMinutes == 372 && s[1].antenna == "B" && s[1].txOn, "Sennheiser: Werte korrekt");
                check (s.count (2) && ! s[2].txOn, "Sennheiser: Kanal ohne Sender erkannt");
            }
            check (fake.subscriptions >= 4, "Sennheiser: ein Abo pro Kanal gesendet");
        }
    }
}

namespace
{
    juce::String httpRequest (int port, const juce::String& method, const juce::String& path, const juce::String& body, int& status)
    {
        juce::StreamingSocket s;
        status = 0;
        if (! s.connect ("127.0.0.1", port, 2000))
            return {};

        juce::String req;
        req << method << " " << path << " HTTP/1.1\r\nHost: localhost\r\nContent-Length: "
            << (int) body.getNumBytesAsUTF8() << "\r\nConnection: close\r\n\r\n" << body;
        s.write (req.toRawUTF8(), (int) req.getNumBytesAsUTF8());

        juce::MemoryBlock data;
        char buf[4096];
        while (s.waitUntilReady (true, 2000) > 0)
        {
            const int n = s.read (buf, sizeof (buf), false);
            if (n <= 0) break;
            data.append (buf, (size_t) n);
        }

        const auto text = juce::String::fromUTF8 ((const char*) data.getData(), (int) data.getSize());
        status = text.fromFirstOccurrenceOf (" ", false, false).getIntValue();
        return text.fromFirstOccurrenceOf ("\r\n\r\n", false, false);
    }

    void testWeb()
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("showcom_web_test.json");
        file.deleteFile();

        WebServer web;
        web.setDevicesFile (file);

        auto snap = std::make_shared<WebServer::Snapshot>();
        snap->listening = true;
        WebServer::Person a; a.id = "u1"; a.name = "Anna"; a.colour = "#ba68c8"; a.talkback = true;
        WebServer::Person b; b.id = "u2"; b.name = "Mia";  b.colour = "#9575cd"; b.talkback = false;
        b.rfAssigned = b.rfOnline = b.rfKnown = true; b.rf.txOn = true; b.rf.batteryPercent = 64; b.rf.frequencyKHz = 470125;
        snap->persons = { a, b };
        snap->entries.push_back ({ 1, "u1", "20:01:02", juce::String::fromUTF8 ("Jürgen, kommst du mal zur Bühne?"), 3 });
        snap->entries.push_back ({ 2, "u1", "20:01:09", "Alles gut.", 4 });
        web.publish (snap);

        web.configure (true, 18765);
        check (web.isRunning(), "Web: Server läuft");

        int st = 0;
        auto page = httpRequest (18765, "GET", "/", {}, st);
        check (st == 200 && page.contains ("api/register"), "Web: Startseite");

        auto reg = juce::JSON::parse (httpRequest (18765, "POST", "/api/register", {}, st));
        const auto id = reg["id"].toString();
        check (st == 200 && id.length() == 6, "Web: Gerät registriert, eigene ID");

        httpRequest (18765, "GET", "/d/" + id, {}, st);
        check (st == 200, "Web: Geräte-Oberfläche erreichbar");

        httpRequest (18765, "POST", "/api/d/" + id + "/settings", R"({"keywords":"Juergen"})", st);
        auto state = juce::JSON::parse (httpRequest (18765, "GET", "/api/d/" + id + "/state", {}, st));
        check (st == 200 && state["persons"].size() == 2, "Web: Zustand mit Personen");
        check ((bool) state["entries"][0]["mention"] && ! (bool) state["entries"][1]["mention"],
               "Web: eigener Alarm-Name des Geräts wird erkannt (Umlaut-Faltung)");
        check ((int) state["persons"][1]["rf"]["battery"] == 64 && ! (bool) state["persons"][1]["talkback"], "Web: Funkdaten und nur-Funk-Person");

        httpRequest (18765, "POST", "/api/d/" + id + "/visibility", R"({"user":"u2","visible":false})", st);
        state = juce::JSON::parse (httpRequest (18765, "GET", "/api/d/" + id + "/state", {}, st));
        check (! (bool) state["persons"][1]["visible"] && (bool) state["persons"][0]["visible"], "Web: Person pro Gerät ausblenden");

        // Ein zweites Gerät hat seine eigene Sicht
        const auto id2 = juce::JSON::parse (httpRequest (18765, "POST", "/api/register", {}, st))["id"].toString();
        auto state2 = juce::JSON::parse (httpRequest (18765, "GET", "/api/d/" + id2 + "/state", {}, st));
        check (id2 != id && (bool) state2["persons"][1]["visible"] && ! (bool) state2["entries"][0]["mention"],
               "Web: zweites Gerät unabhängig (eigene Sichtbarkeit, kein Alarm-Name)");

        web.removeDevice (id2);
        httpRequest (18765, "GET", "/api/d/" + id2 + "/state", {}, st);
        check (st == 404, "Web: am Host entferntes Gerät hat keinen Zugriff mehr");

        check (file.loadFileAsString().contains (id) && ! file.loadFileAsString().contains (id2), "Web: Geräte werden gespeichert");

        web.configure (false, 0);
        check (! web.isRunning(), "Web: Server gestoppt");
        file.deleteFile();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    testWeb();
    testWireless();

    AudioEngine engine;
    engine.getDeviceManager().addAudioDeviceType (std::make_unique<FakeType>());
    engine.initialise ({});
    // Erst nach initialise() umschalten: sonst wählt JUCE ggf. das System-Interface (CoreAudio/WASAPI)
    engine.getDeviceManager().setCurrentAudioDeviceType ("Fake", true);
    check (engine.getDeviceManager().getCurrentAudioDevice() != nullptr
           && engine.getDeviceManager().getCurrentAudioDevice()->getTypeName() == "Fake", "simuliertes Interface aktiv");

    // Nur Eingang 1 aktiv: die Engine muss Eingang 3 für die Zuweisung selbst einschalten
    {
        auto setup = engine.getDeviceManager().getAudioDeviceSetup();
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setBit (0);
        engine.getDeviceManager().setAudioDeviceSetup (setup, true);
    }

    std::vector<AudioEngine::Segment> segments, partials;
    juce::CriticalSection segLock;
    engine.onSegment = [&] (AudioEngine::Segment&& s)
    {
        const juce::ScopedLock sl (segLock);
        (s.partial ? partials : segments).push_back (std::move (s));
    };

    AudioEngine::Assignment a;
    a.userId = "anna";
    a.channel = talkChannel;
    a.thresholdDb = -40.0f;
    engine.setAssignments ({ a });

    check (engine.isChannelActive (talkChannel), "zugewiesener Eingang wurde automatisch aktiviert");

    // Zuhören einschalten, sobald das Gerät läuft (Zeitbasis der Bursts = Gerätestart; Toleranz unten)
    engine.setListening (true);
    check (engine.isListening(), "Zuhören ist aktiv");

    bool sawSpeaking = false;
    for (int i = 0; i < 65; ++i)   // 6.5 s
    {
        juce::Thread::sleep (100);
        sawSpeaking = sawSpeaking || engine.isSpeaking (talkChannel);
    }

    const float level = engine.getLevelDb (0);
    engine.setListening (false);

    check (sawSpeaking, "Sprech-Anzeige wurde aktiv");
    {
        const juce::ScopedLock sl (segLock);
        check (! partials.empty(), "Live-Zwischenstände während des Sprechens gesendet");
        if (! partials.empty() && ! segments.empty())
            check (partials.front().utteranceId == segments.front().utteranceId
                   && partials.front().audio.size() < segments.front().audio.size(),
                   "Zwischenstand gehört zur selben Ansage und ist kürzer als das Endergebnis");
        std::printf ("      %d Zwischenstände, %d Endergebnisse\n", (int) partials.size(), (int) segments.size());
    }
    check (level > -20.0f, "Pegelanzeige für nicht zugewiesenen Eingang funktioniert");
    check (engine.getOverflowCount() == 0, "keine FIFO-Überläufe");

    {
        const juce::ScopedLock sl (segLock);
        check (segments.size() == 2, "genau zwei Sprach-Abschnitte erkannt");

        if (segments.size() == 2)
        {
            for (size_t i = 0; i < 2; ++i)
            {
                const auto& s = segments[i];
                const double len = (double) s.audio.size() / s.sampleRate;
                std::printf ("      Abschnitt %d: Start %.2f s, Länge %.2f s, user=%s\n",
                             (int) i + 1, s.sessionOffsetSec, len, s.userId.toRawUTF8());
            }

            // Session-Start liegt kurz nach Gerätestart -> Offsets etwas kleiner als die Burst-Zeiten
            check (segments[0].userId == "anna", "Abschnitt gehört zur richtigen Person");
            check (std::abs (segments[0].audio.size() / 48000.0 - 2.15) < 0.25, "Länge Abschnitt 1 plausibel (Burst + Pre-Roll + Ausklang)");
            check (std::abs ((segments[1].sessionOffsetSec - segments[0].sessionOffsetSec) - 3.0) < 0.05, "Abstand der Abschnitte stimmt (3,0 s)");
        }
    }

    // Der Abschnitt muss das tatsächliche Signal enthalten (nicht Stille oder einen anderen Kanal)
    {
        const juce::ScopedLock sl (segLock);
        if (segments.size() == 2)
        {
            float peak = 0.0f;
            for (float v : segments[1].audio)
                peak = juce::jmax (peak, std::abs (v));
            check (peak > 0.2f && peak < 0.3f, "Abschnitt enthält das Sprachsignal von Eingang 3");
        }
    }

    // Nach dem Pausieren dürfen keine Abschnitte mehr kommen
    {
        size_t before;
        { const juce::ScopedLock sl (segLock); before = segments.size(); }
        juce::Thread::sleep (1500);
        const juce::ScopedLock sl (segLock);
        check (segments.size() == before, "pausiert: keine weiteren Abschnitte");
    }

    std::printf (failures == 0 ? "Engine test passed.\n" : "Engine test FAILED.\n");
    return failures == 0 ? 0 : 1;
}
