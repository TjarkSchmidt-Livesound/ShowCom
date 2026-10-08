#include "Wireless.h"

namespace
{
    juce::int64 nowMs()     { return juce::Time::currentTimeMillis(); }

    /** "192.168.1.50" oder "192.168.1.50:2202" -> Host + Port */
    std::pair<juce::String, int> splitHostPort (const juce::String& host, int defaultPort)
    {
        const auto h = host.trim();
        if (h.containsChar (':'))
        {
            const int p = h.fromLastOccurrenceOf (":", false, false).getIntValue();
            if (p > 0 && p < 65536)
                return { h.upToLastOccurrenceOf (":", false, false), p };
        }
        return { h, defaultPort };
    }
}

//==============================================================================
bool WirelessClient::isOnline() const
{
    const juce::ScopedLock sl (lock);
    return online;
}

juce::String WirelessClient::getStatusText() const
{
    const juce::ScopedLock sl (lock);
    return statusText;
}

juce::String WirelessClient::getModel() const
{
    const juce::ScopedLock sl (lock);
    return model;
}

std::map<int, WirelessChannelState> WirelessClient::snapshot() const
{
    const juce::ScopedLock sl (lock);
    return channels;
}

void WirelessClient::setOnline (bool o, const juce::String& status)
{
    const juce::ScopedLock sl (lock);
    online = o;
    statusText = status;
}

void WirelessClient::setModel (const juce::String& m)
{
    const juce::ScopedLock sl (lock);
    if (m.isNotEmpty())
        model = m;
}

void WirelessClient::updateChannel (int ch, const WirelessChannelState& s)
{
    const juce::ScopedLock sl (lock);
    channels[ch] = s;
}

juce::String WirelessClient::getRawLog() const
{
    const juce::ScopedLock sl (lock);
    juce::String out;
    out << "Modell: " << model << "\nStatus: " << statusText << "\n";
    if (! unknownKeys.empty())
    {
        out << "Unbekannte Schlüssel:";
        for (auto& k : unknownKeys)
            out << " " << juce::String (k);
        out << "\n";
    }
    out << "\nLetzte Meldungen:\n" << rawLog.joinIntoString ("\n") << "\n";
    return out;
}

void WirelessClient::logRaw (const juce::String& text)
{
    const juce::ScopedLock sl (lock);
    rawLog.add (juce::Time::getCurrentTime().formatted ("%H:%M:%S  ") + text.trim());
    while (rawLog.size() > 400)
        rawLog.remove (0);
}

void WirelessClient::markAlive()
{
    const juce::ScopedLock sl (lock);
    lastDataMs = nowMs();
}

//==============================================================================
ShureClient::ShureClient (const WirelessDeviceConfig& c)
    : WirelessClient (c, "Shure " + c.host)
{
    startThread();
}

ShureClient::~ShureClient()
{
    stopThread (5000);
}

void ShureClient::run()
{
    while (! threadShouldExit())
    {
        if (config.host.isEmpty())
        {
            setOnline (false, "IP-Adresse eintragen");
            wait (1000);
            continue;
        }

        const auto [host, tcpPort] = splitHostPort (config.host, port);
        socket = std::make_unique<juce::StreamingSocket>();
        setOnline (false, "Verbinde mit " + host + " ...");

        if (! socket->connect (host, tcpPort, 3000))
        {
            setOnline (false, "Keine Verbindung zu " + host + ":" + juce::String (tcpPort));
            socket.reset();
            wait (3000);
            continue;
        }

        ShureParser parser;
        parser.setTransmitterMode (config.isPsm());
        auto send = [this] (const std::string& cmd) { return socket->write (cmd.data(), (int) cmd.size()) == (int) cmd.size(); };

        for (auto& cmd : ShureParser::initialCommands())
            send (cmd);

        setOnline (true, "Verbunden");
        markAlive();

        auto lastRefresh = nowMs();
        auto lastData = nowMs();
        char buf[4096];

        while (! threadShouldExit() && socket->isConnected())
        {
            const int ready = socket->waitUntilReady (true, 250);
            if (ready < 0)
                break;

            if (ready > 0)
            {
                const int n = socket->read (buf, (int) sizeof (buf), false);
                if (n <= 0)
                    break;

                lastData = nowMs();
                markAlive();

                const std::string chunk (buf, (size_t) n);
                {
                    // Meter (SAMPLE) nicht ins Protokoll – sonst sieht man vor lauter Pegeln nichts
                    std::string text;
                    size_t pos = 0;
                    while (pos < chunk.size())
                    {
                        const auto start = chunk.find ("< SAMPLE", pos);
                        if (start == std::string::npos) { text += chunk.substr (pos); break; }
                        text += chunk.substr (pos, start - pos);
                        const auto end = chunk.find ('>', start);
                        if (end == std::string::npos) break;
                        pos = end + 1;
                    }
                    if (juce::String (text).trim().isNotEmpty())
                        logRaw (juce::String::fromUTF8 (text.data(), (int) text.size()));
                }

                for (int ch : parser.feed (chunk))
                    updateChannel (ch, parser.getChannels().at (ch));

                setModel (juce::String (parser.getModel()));
                {
                    const juce::ScopedLock sl (lock);
                    unknownKeys = parser.getUnknownKeys();
                }
            }

            // Akku- und Namenswerte regelmäßig neu holen (Meter kommen jede Sekunde von selbst)
            if (nowMs() - lastRefresh > 10000)
            {
                if (! send (ShureParser::refreshCommand()))
                    break;
                lastRefresh = nowMs();
            }

            if (nowMs() - lastData > 20000)
                break;   // Gerät antwortet nicht mehr
        }

        socket->close();
        socket.reset();
        setOnline (false, juce::String::fromUTF8 ("Verbindung verloren – neuer Versuch ..."));
        wait (2000);
    }
}

//==============================================================================
SennheiserClient::SennheiserClient (const WirelessDeviceConfig& c)
    : WirelessClient (c, "Sennheiser " + c.host)
{
    startThread();
}

SennheiserClient::~SennheiserClient()
{
    stopThread (3000);
}

juce::StringArray SennheiserClient::subscribeMessages (int numChannels)
{
    // Ein Kanal pro Nachricht: Ein EM 2 lehnt Abfragen für rx3/rx4 ab, ohne die übrigen zu verwerfen.
    juce::StringArray msgs;

    for (int i = 1; i <= numChannels; ++i)
    {
        const auto n = juce::String (i);
        msgs.add (R"({"osc":{"state":{"subscribe":[{"#":{"lifetime":60},"rx)" + n
                  + R"(":{"name":null,"frequency":null,"mute":null,"warnings":null}}]}}})");
        msgs.add (R"({"osc":{"state":{"subscribe":[{"#":{"lifetime":60},"mates":{"tx)" + n
                  + R"(":{"battery":{"gauge":null,"type":null,"lifetime":null},"type":null,"mute":null,"name":null}}},{"m":{"rx)" + n
                  + R"(":{"rsqi":null,"divi":null}}}]}}})");
    }

    return msgs;
}

void SennheiserClient::applyJson (const juce::var& json, std::map<int, WirelessChannelState>& channels, std::set<int>& changed)
{
    if (! json.isObject())
        return;

    // Antwort auf das Abonnement enthält die aktuellen Werte im selben Aufbau
    const auto& osc = json["osc"];
    if (osc.isObject())
        if (auto* arr = osc["state"]["subscribe"].getArray())
            for (auto& item : *arr)
                applyJson (item, channels, changed);

    for (int i = 1; i <= 4; ++i)
    {
        const auto rx = json[juce::Identifier ("rx" + juce::String (i))];
        if (rx.isObject())
        {
            auto& s = channels[i];
            if (rx.hasProperty ("name"))      s.name = rx["name"].toString().trim().toStdString();
            if (rx.hasProperty ("frequency")) s.frequencyKHz = (int) rx["frequency"];
            changed.insert (i);
        }

        const auto meter = json["m"][juce::Identifier ("rx" + juce::String (i))];
        if (meter.isObject())
        {
            auto& s = channels[i];
            if (meter.hasProperty ("rsqi"))
            {
                s.rfQualityPercent = juce::jlimit (0, 100, (int) meter["rsqi"]);
                s.txOn = s.rfQualityPercent > 0;
                if (! s.txOn)
                    s.batteryPercent = s.batteryMinutes = -1;
            }
            if (meter.hasProperty ("divi"))
            {
                const int d = (int) meter["divi"];
                s.antenna = d == 1 ? "A" : d == 2 ? "B" : "";
            }
            changed.insert (i);
        }

        const auto tx = json["mates"][juce::Identifier ("tx" + juce::String (i))];
        if (tx.isObject())
        {
            auto& s = channels[i];
            const auto& bat = tx["battery"];
            if (bat.isObject())
            {
                if (bat.hasProperty ("gauge"))
                {
                    const int g = (int) bat["gauge"];
                    s.batteryPercent = (g >= 0 && g <= 100) ? g : -1;
                }
                if (bat.hasProperty ("lifetime"))
                {
                    const int m = (int) bat["lifetime"];
                    s.batteryMinutes = m > 0 ? m : -1;   // Minuten
                }
                if (bat.hasProperty ("type"))
                    s.batteryType = bat["type"].toString().toStdString();
            }
            if (tx.hasProperty ("type"))  s.txModel = tx["type"].toString().toStdString();
            if (tx.hasProperty ("mute"))  { s.afMuted = (bool) tx["mute"]; s.updateMuteSummary(); }
            changed.insert (i);
        }
    }
}

void SennheiserClient::run()
{
    juce::DatagramSocket sock (false);
    if (! sock.bindToPort (0))
    {
        setOnline (false, juce::String::fromUTF8 ("UDP-Port konnte nicht geöffnet werden"));
        return;
    }

    std::map<int, WirelessChannelState> state;
    const int numChannels = config.numChannels > 0 ? config.numChannels : 4;
    juce::int64 lastSubscribe = 0, lastData = 0;
    char buf[8192];

    while (! threadShouldExit())
    {
        if (config.host.isEmpty())
        {
            setOnline (false, "IP-Adresse eintragen");
            wait (1000);
            continue;
        }

        if (nowMs() - lastSubscribe > 20000)   // Abo läuft 60 s, rechtzeitig erneuern
        {
            const auto [host, udpPort] = splitHostPort (config.host, port);
            for (auto& msg : subscribeMessages (numChannels))
                sock.write (host, udpPort, msg.toRawUTF8(), (int) msg.getNumBytesAsUTF8());
            lastSubscribe = nowMs();
        }

        if (sock.waitUntilReady (true, 250) > 0)
        {
            juce::String sender;
            int senderPort = 0;
            const int n = sock.read (buf, (int) sizeof (buf) - 1, false, sender, senderPort);

            if (n > 0)
            {
                lastData = nowMs();
                markAlive();

                std::set<int> changed;
                applyJson (juce::JSON::parse (juce::String::fromUTF8 (buf, n)), state, changed);

                for (int ch : changed)
                    updateChannel (ch, state[ch]);

                setModel ("EW-DX");
            }
        }

        const bool alive = lastData > 0 && nowMs() - lastData < 10000;
        setOnline (alive, alive ? juce::String ("Verbunden")
                                : "Keine Antwort von " + config.host
                                  + juce::String::fromUTF8 (" (SSC-Zugriff „Legacy/SSCv1“ am Empfänger aktiviert?)"));
    }
}

//==============================================================================
SimulatedClient::SimulatedClient (const WirelessDeviceConfig& c)
    : WirelessClient (c, "Simulated wireless")
{
    startThread();
}

SimulatedClient::~SimulatedClient()
{
    stopThread (2000);
}

void SimulatedClient::run()
{
    juce::Random rnd (42);
    const char* names[] = { "HH01", "BP02", "BP03", "HH04", "BP05", "HH06", "HH07", "BP08" };
    const int freqs[]   = { 470125, 471300, 472475, 473650, 474825, 476000, 477175, 478350 };
    const float batt[]  = { 96.0f, 78.0f, 64.0f, 31.0f, 100.0f, 12.0f, 88.0f, 71.0f };
    float rf[]          = { -47.0f, -51.0f, -55.0f, -60.0f, -49.0f, -66.0f, -52.0f, -54.0f };
    const juce::int64 start = nowMs();

    setModel ("Demo");
    setOnline (true, "Simuliert");

    while (! threadShouldExit())
    {
        const double minutes = (double) (nowMs() - start) / 60000.0;

        for (int i = 0; i < 8; ++i)
        {
            WirelessChannelState s;
            s.name = names[i];
            s.txModel = i % 3 == 0 ? "ULXD2" : "ULXD1";
            s.frequencyKHz = freqs[i];
            s.txOn = i != 4;   // ein Sender ist aus
            s.antenna = rnd.nextBool() ? "AB" : (rnd.nextBool() ? "A" : "B");

            if (i == 6)
            {
                s.afMuted = true;   // Demo: Sender stumm geschaltet
                s.updateMuteSummary();
            }

            if (s.txOn)
            {
                rf[i] = juce::jlimit (-90.0f, -35.0f, rf[i] + (rnd.nextFloat() - 0.5f) * 6.0f
                                                    + (-52.0f - (float) i * 3.0f - rf[i]) * 0.05f);
                if (rnd.nextFloat() < 0.03f)
                    rf[i] -= 15.0f;   // kurzer Einbruch

                s.rfDbm = rf[i];
                s.batteryPercent = juce::jmax (1, (int) (batt[i] - minutes * 0.2));
                s.batteryBars = juce::jlimit (0, 5, (s.batteryPercent + 19) / 20);
                s.batteryMinutes = s.batteryPercent * 5;
                s.audioDb = -60.0f + rnd.nextFloat() * 40.0f;
            }

            updateChannel (i + 1, s);
        }

        markAlive();
        wait (250);
    }
}

//==============================================================================
SimulatedPsmClient::SimulatedPsmClient (const WirelessDeviceConfig& c)
    : WirelessClient (c, "Simulated PSM")
{
    startThread();
}

SimulatedPsmClient::~SimulatedPsmClient()
{
    stopThread (2000);
}

void SimulatedPsmClient::run()
{
    juce::Random rnd (7);
    const char* names[] = { "IEM Vox", "IEM Git", "IEM Keys", "IEM Drums" };
    const int freqs[]   = { 518200, 519400, 520600, 521800 };
    float level[4]      = { -20.0f, -24.0f, -30.0f, -18.0f };

    setModel ("ADTQ (Demo)");
    setOnline (true, "Simuliert");

    while (! threadShouldExit())
    {
        for (int i = 0; i < 4; ++i)
        {
            WirelessChannelState s;
            s.name = names[i];
            s.txModel = "ADTQ";
            s.frequencyKHz = freqs[i];
            s.isIemTransmitter = true;
            s.rfOn = i != 1;                // Demo: ein Sender mit RF aus
            s.txOn = s.rfOn;
            s.rfMuted = ! s.rfOn;
            s.updateMuteSummary();

            level[i] = juce::jlimit (-60.0f, -3.0f, level[i] + (rnd.nextFloat() - 0.5f) * 10.0f + (-20.0f - level[i]) * 0.1f);
            s.inputLeftDb = level[i];
            s.inputRightDb = level[i] - 2.0f + rnd.nextFloat() * 4.0f;
            updateChannel (i + 1, s);
        }

        markAlive();
        wait (250);
    }
}

//==============================================================================
WirelessManager::WirelessManager()
{
    startTimerHz (4);
}

WirelessManager::~WirelessManager()
{
    stopTimer();
    for (auto& c : clients)
        c->signalThreadShouldExit();
    clients.clear();
}

juce::String WirelessManager::typeName (WirelessDeviceConfig::Type t)
{
    switch (t)
    {
        case WirelessDeviceConfig::Type::shure:          return "Shure";
        case WirelessDeviceConfig::Type::shurePsm:       return "Shure AD PSM";
        case WirelessDeviceConfig::Type::sennheiserEwdx: return "Sennheiser EW-DX";
        case WirelessDeviceConfig::Type::simulated:      return "Demo";
        case WirelessDeviceConfig::Type::simulatedPsm:   return "Demo PSM";
    }
    return {};
}

int WirelessManager::defaultChannelCount (const WirelessDeviceConfig& c)
{
    if (c.numChannels > 0)
        return c.numChannels;

    if (c.type == WirelessDeviceConfig::Type::simulated)
        return 8;
    return 4;
}

std::unique_ptr<WirelessClient> WirelessManager::createClient (const WirelessDeviceConfig& c)
{
    switch (c.type)
    {
        case WirelessDeviceConfig::Type::shure:
        case WirelessDeviceConfig::Type::shurePsm:       return std::make_unique<ShureClient> (c);
        case WirelessDeviceConfig::Type::sennheiserEwdx: return std::make_unique<SennheiserClient> (c);
        case WirelessDeviceConfig::Type::simulated:      return std::make_unique<SimulatedClient> (c);
        case WirelessDeviceConfig::Type::simulatedPsm:   return std::make_unique<SimulatedPsmClient> (c);
    }
    return {};
}

void WirelessManager::setDevices (const std::vector<WirelessDeviceConfig>& devices)
{
    std::vector<WirelessDeviceConfig> wanted (devices);

    if (demoEnabled)
    {
        WirelessDeviceConfig demo;
        demo.id = demoDeviceId;
        demo.type = WirelessDeviceConfig::Type::simulated;
        demo.label = "Demo";
        wanted.push_back (demo);

        WirelessDeviceConfig psm;
        psm.id = demoPsmDeviceId;
        psm.type = WirelessDeviceConfig::Type::simulatedPsm;
        psm.label = "Demo";
        wanted.push_back (psm);
    }

    configs = wanted;
    std::vector<std::unique_ptr<WirelessClient>> next;

    for (auto& d : wanted)
    {
        if (! d.enabled)
            continue;   // abgeschaltet: keine Verbindung

        std::unique_ptr<WirelessClient> keep;

        for (auto& c : clients)
        {
            if (c != nullptr && c->config.id == d.id && c->config.type == d.type
                && c->config.host == d.host && c->config.numChannels == d.numChannels)
            {
                keep = std::move (c);
                break;
            }
        }

        if (keep == nullptr)
            keep = createClient (d);
        else if (keep->config.label != d.label)
            keep = createClient (d);   // Label steckt in der const-Konfiguration

        next.push_back (std::move (keep));
    }

    // Übrige (entfernte/geänderte) Clients beenden
    for (auto& c : clients)
        if (c != nullptr)
            c->signalThreadShouldExit();

    clients = std::move (next);
    timerCallback();
}

void WirelessManager::setDemoDevice (bool enabled)
{
    demoEnabled = enabled;
}

void WirelessManager::timerCallback()
{
    std::set<juce::String> alive;

    for (auto& c : clients)
    {
        auto snap = c->snapshot();
        alive.insert (c->config.id);

        for (auto& [ch, s] : snap)
        {
            const auto key = c->config.id + ":" + juce::String (ch);
            auto& h = histories[key];
            h.push_back (c->isOnline() && s.txOn ? s.signalNormalised() : 0.0f);
            while ((int) h.size() > historyLength)
                h.pop_front();

            // Audiopegel vom Gerät: Empfänger-Ausgang bzw. Eingang des In-Ear-Senders (lauterer Kanal)
            float db = s.isIemTransmitter ? std::fmax (std::isnan (s.inputLeftDb) ? -120.0f : s.inputLeftDb,
                                                       std::isnan (s.inputRightDb) ? -120.0f : s.inputRightDb)
                                          : (std::isnan (s.audioDb) ? -120.0f : s.audioDb);
            if (! c->isOnline() || (! s.txOn && ! s.isIemTransmitter) || s.afMuted)
                db = -120.0f;
            auto& a = audioHistories[key];
            a.push_back (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f));
            while ((int) a.size() > historyLength)
                a.pop_front();
        }

        cache[c->config.id] = std::move (snap);
    }

    for (auto it = cache.begin(); it != cache.end();)
        it = alive.count (it->first) > 0 ? std::next (it) : cache.erase (it);
}

std::vector<WirelessManager::ChannelRef> WirelessManager::listChannels() const
{
    std::vector<ChannelRef> refs;

    for (auto& cfg : configs)
    {
        const auto name = typeName (cfg.type) + " " + (cfg.label.isNotEmpty() ? cfg.label : cfg.host);

        std::set<int> chans;
        if (auto it = cache.find (cfg.id); it != cache.end())
            for (auto& [ch, s] : it->second)
                chans.insert (ch);

        if (chans.empty())
            for (int i = 1; i <= defaultChannelCount (cfg); ++i)
                chans.insert (i);

        for (int ch : chans)
        {
            ChannelRef r;
            r.deviceId = cfg.id;
            r.channel = ch;
            r.label = name + juce::String::fromUTF8 (" · Kanal ") + juce::String (ch);

            WirelessChannelState s;
            const bool known = getChannel (cfg.id, ch, s);
            r.iem = cfg.isPsm() || (known && s.isIemTransmitter);
            if (known && ! s.name.empty())
                r.label << "  (" << juce::String (s.name) << ")";
            if (isDisabled (cfg.id, ch))
                r.label << juce::String::fromUTF8 ("  – deaktiviert");

            refs.push_back (r);
        }
    }

    return refs;
}

bool WirelessManager::isDisabled (const juce::String& deviceId, int channel) const
{
    for (auto& cfg : configs)
        if (cfg.id == deviceId)
            return ! cfg.enabled || cfg.disabledChannels.count (channel) > 0;
    return false;
}

const std::deque<float>* WirelessManager::getAudioHistory (const juce::String& deviceId, int channel) const
{
    auto it = audioHistories.find (deviceId + ":" + juce::String (channel));
    return it != audioHistories.end() ? &it->second : nullptr;
}

juce::String WirelessManager::getRawLog (const juce::String& deviceId) const
{
    for (auto& c : clients)
        if (c->config.id == deviceId)
            return c->getRawLog();
    return {};
}

bool WirelessManager::getChannel (const juce::String& deviceId, int channel, WirelessChannelState& out) const
{
    auto it = cache.find (deviceId);
    if (it == cache.end())
        return false;

    auto c = it->second.find (channel);
    if (c == it->second.end())
        return false;

    out = c->second;
    return true;
}

const std::deque<float>* WirelessManager::getHistory (const juce::String& deviceId, int channel) const
{
    auto it = histories.find (deviceId + ":" + juce::String (channel));
    return it != histories.end() ? &it->second : nullptr;
}

WirelessManager::DeviceStatus WirelessManager::getStatus (const juce::String& deviceId) const
{
    DeviceStatus st;

    for (auto& cfg : configs)
    {
        if (cfg.id == deviceId && ! cfg.enabled)
        {
            st.exists = true;
            st.text = "deaktiviert";
            return st;
        }
    }

    for (auto& c : clients)
    {
        if (c->config.id == deviceId)
        {
            st.exists = true;
            st.online = c->isOnline();
            st.text = c->getStatusText();
            st.model = c->getModel();
        }
    }

    return st;
}
