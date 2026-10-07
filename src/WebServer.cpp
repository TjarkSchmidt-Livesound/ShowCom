#include "WebServer.h"
#include "KeywordMatcher.h"
#include "BoardLayout.h"

#include <BinaryData.h>

namespace
{
    constexpr int maxRequestBytes = 64 * 1024;

    juce::String reasonPhrase (int status)
    {
        switch (status)
        {
            case 200: return "OK";
            case 204: return "No Content";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 413: return "Payload Too Large";
            default:  return "Error";
        }
    }

    juce::String binaryText (const char* data, int size)
    {
        return juce::String::fromUTF8 (data, size);
    }

    bool isValidDeviceId (const juce::String& id)
    {
        return id.length() >= 4 && id.length() <= 16 && id.containsOnly ("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
    }

    juce::var rfJson (const WebServer::Person& p)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("assigned", p.rfAssigned);
        o->setProperty ("online", p.rfOnline);
        o->setProperty ("known", p.rfKnown);

        if (p.rfKnown)
        {
            const auto& s = p.rf;
            o->setProperty ("txOn", s.txOn);
            o->setProperty ("name", juce::String (s.name));
            o->setProperty ("model", juce::String (s.txModel));
            o->setProperty ("battery", s.batteryPercentEstimate());
            o->setProperty ("minutes", s.batteryMinutes);
            o->setProperty ("freq", s.frequencyKHz);
            o->setProperty ("antenna", juce::String (s.antenna));
            o->setProperty ("muted", s.muted);
            if (! std::isnan (s.rfDbm))   o->setProperty ("dbm", juce::roundToInt (s.rfDbm));
            if (s.rfQualityPercent >= 0)  o->setProperty ("quality", s.rfQualityPercent);
            o->setProperty ("signal", s.signalNormalised());
        }

        juce::Array<juce::var> h;
        for (float v : p.history)
            h.add (juce::roundToInt (v * 100.0f));   // 0..100, spart Bytes
        o->setProperty ("history", h);

        return juce::var (o);
    }
}

//==============================================================================
WebServer::WebServer()
    : juce::Thread ("ShowCom Web")
{
    snapshot = std::make_shared<Snapshot>();
}

WebServer::~WebServer()
{
    configure (false, 0);
}

void WebServer::setDevicesFile (const juce::File& f)
{
    const juce::ScopedLock sl (devicesLock);
    devicesFile = f;
    loadDevices();
}

void WebServer::configure (bool enabled, int port)
{
    if (enabled && running.load() && port == currentPort.load())
        return;

    // Stoppen
    if (isThreadRunning() || listener != nullptr)
    {
        signalThreadShouldExit();
        if (listener != nullptr)
            listener->close();
        stopThread (3000);
        listener.reset();
    }

    if (pool != nullptr)
    {
        pool->removeAllJobs (true, 3000);
        pool.reset();
    }

    running.store (false);
    currentPort.store (0);

    if (! enabled)
    {
        const juce::ScopedLock sl (statusLock);
        status = "Aus";
        return;
    }

    listener = std::make_unique<juce::StreamingSocket>();

    if (! listener->createListener (port))
    {
        listener.reset();
        const juce::ScopedLock sl (statusLock);
        status = "Port " + juce::String (port) + juce::String::fromUTF8 (" ist belegt – anderen Port wählen.");
        return;
    }

    pool = std::make_unique<juce::ThreadPool> (juce::ThreadPoolOptions{}.withThreadName ("ShowCom Web Request")
                                                                         .withNumberOfThreads (4));
    currentPort.store (port);
    running.store (true);

    {
        const juce::ScopedLock sl (statusLock);
        status = juce::String::fromUTF8 ("Läuft");
    }

    startThread();
}

juce::String WebServer::getStatusText() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

juce::StringArray WebServer::getUrls() const
{
    juce::StringArray urls;
    if (! running.load())
        return urls;

    for (auto& ip : juce::IPAddress::getAllAddresses (false))
        if (ip != juce::IPAddress::local() && ! ip.toString().startsWith ("169.254"))
            urls.add ("http://" + ip.toString() + ":" + juce::String (currentPort.load()));

    return urls;
}

void WebServer::publish (std::shared_ptr<const Snapshot> s)
{
    const juce::ScopedLock sl (snapshotLock);
    snapshot = std::move (s);
}

//==============================================================================
void WebServer::run()
{
    while (! threadShouldExit() && listener != nullptr)
    {
        std::unique_ptr<juce::StreamingSocket> client (listener->waitForNextConnection());
        if (client == nullptr)
        {
            if (threadShouldExit())
                break;
            wait (50);
            continue;
        }

        auto shared = std::make_shared<std::unique_ptr<juce::StreamingSocket>> (std::move (client));
        pool->addJob ([this, shared] { serve (std::move (*shared)); });
    }
}

void WebServer::serve (std::unique_ptr<juce::StreamingSocket> socket)
{
    // Anfrage lesen: Kopf bis \r\n\r\n, dann ggf. Body nach Content-Length
    juce::MemoryBlock data;
    char buf[4096];
    int headerEnd = -1;
    const auto deadline = juce::Time::getMillisecondCounter() + 3000;

    while (headerEnd < 0 && juce::Time::getMillisecondCounter() < deadline && (int) data.getSize() < maxRequestBytes)
    {
        if (socket->waitUntilReady (true, 200) <= 0)
            continue;

        const int n = socket->read (buf, (int) sizeof (buf), false);
        if (n <= 0)
            return;

        data.append (buf, (size_t) n);
        const auto text = juce::String::fromUTF8 ((const char*) data.getData(), (int) data.getSize());
        headerEnd = text.indexOf ("\r\n\r\n");
    }

    if (headerEnd < 0)
        return;

    const auto all = juce::String::fromUTF8 ((const char*) data.getData(), (int) data.getSize());
    const auto head = all.substring (0, headerEnd);
    auto lines = juce::StringArray::fromLines (head);
    const auto requestLine = juce::StringArray::fromTokens (lines[0], " ", "");
    const auto method = requestLine[0];
    const auto target = requestLine[1];

    int contentLength = 0;
    for (auto& l : lines)
        if (l.startsWithIgnoreCase ("content-length:"))
            contentLength = l.fromFirstOccurrenceOf (":", false, false).trim().getIntValue();

    // Body (in Bytes; der Kopf ist ASCII)
    const int bodyStart = (int) head.getNumBytesAsUTF8() + 4;
    while ((int) data.getSize() - bodyStart < contentLength && contentLength < maxRequestBytes
           && juce::Time::getMillisecondCounter() < deadline)
    {
        if (socket->waitUntilReady (true, 200) <= 0)
            continue;
        const int n = socket->read (buf, (int) sizeof (buf), false);
        if (n <= 0)
            break;
        data.append (buf, (size_t) n);
    }

    juce::String body;
    if (contentLength > 0 && (int) data.getSize() >= bodyStart)
        body = juce::String::fromUTF8 ((const char*) data.getData() + bodyStart,
                                       juce::jmin (contentLength, (int) data.getSize() - bodyStart));

    const auto res = contentLength >= maxRequestBytes ? textResponse ("too large", 413, "text/plain")
                                                      : handle (method, target, body);

    juce::String header;
    header << "HTTP/1.1 " << res.status << " " << reasonPhrase (res.status) << "\r\n"
           << "Content-Type: " << res.contentType << "\r\n"
           << "Content-Length: " << (int) res.body.getSize() << "\r\n"
           << (res.cacheable ? "Cache-Control: max-age=86400\r\n" : "Cache-Control: no-store\r\n")
           << "Connection: close\r\n\r\n";

    socket->write (header.toRawUTF8(), (int) header.getNumBytesAsUTF8());
    if (res.body.getSize() > 0)
        socket->write (res.body.getData(), (int) res.body.getSize());

    socket->close();
}

//==============================================================================
WebServer::Response WebServer::textResponse (const juce::String& text, int code, const juce::String& type) const
{
    Response r;
    r.status = code;
    r.contentType = type;
    r.body.append (text.toRawUTF8(), text.getNumBytesAsUTF8());
    return r;
}

WebServer::Response WebServer::jsonResponse (const juce::var& v, int code) const
{
    return textResponse (juce::JSON::toString (v, true), code, "application/json; charset=utf-8");
}

WebServer::Response WebServer::handle (const juce::String& method, const juce::String& target, const juce::String& body)
{
    const auto path = target.upToFirstOccurrenceOf ("?", false, false);
    auto parts = juce::StringArray::fromTokens (path, "/", "");
    parts.removeEmptyStrings();

    // --- Seiten ------------------------------------------------------------------
    if (method == "GET" && (path == "/" || path == "/index.html"))
        return textResponse (binaryText (BinaryData::start_html, BinaryData::start_htmlSize), 200, "text/html; charset=utf-8");

    if (method == "GET" && parts.size() == 2 && parts[0] == "d")
    {
        bool known;
        {
            const juce::ScopedLock sl (devicesLock);
            known = devices.count (parts[1]) > 0;
        }

        if (! known)
            return textResponse (binaryText (BinaryData::start_html, BinaryData::start_htmlSize), 404, "text/html; charset=utf-8");

        return textResponse (binaryText (BinaryData::app_html, BinaryData::app_htmlSize), 200, "text/html; charset=utf-8");
    }

    if (method == "GET" && path == "/icon.png")
    {
        Response r;
        r.contentType = "image/png";
        r.cacheable = true;
        r.body.append (BinaryData::icon_small_png, (size_t) BinaryData::icon_small_pngSize);
        return r;
    }

    if (method == "GET" && parts.size() == 2 && parts[0] == "avatar")
    {
        std::shared_ptr<const Snapshot> snap;
        {
            const juce::ScopedLock sl (snapshotLock);
            snap = snapshot;
        }

        if (auto it = snap->avatars.find (parts[1]); it != snap->avatars.end() && it->second != nullptr)
        {
            Response r;
            r.contentType = "image/png";
            r.cacheable = true;   // URL enthält eine Version
            r.body = *it->second;
            return r;
        }

        return textResponse ("no avatar", 404, "text/plain");
    }

    // --- API -------------------------------------------------------------------------
    if (method == "POST" && path == "/api/register")
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", createDevice());
        return jsonResponse (juce::var (o));
    }

    if (parts.size() == 4 && parts[0] == "api" && parts[1] == "d")
    {
        const auto id = parts[2];
        const auto action = parts[3];

        {
            const juce::ScopedLock sl (devicesLock);
            auto it = devices.find (id);
            if (it == devices.end())
                return jsonResponse (juce::var ("unknown device"), 404);
            it->second.lastSeenMs = juce::Time::currentTimeMillis();
        }

        if (method == "GET" && action == "state")
            return stateJson (id);

        if (method == "POST" && action == "settings")
        {
            const auto json = juce::JSON::parse (body);
            if (! json.isObject())
                return jsonResponse (juce::var ("bad json"), 400);

            const juce::ScopedLock sl (devicesLock);
            auto& d = devices[id];
            if (json.hasProperty ("label"))    d.label = json["label"].toString().substring (0, 40).trim();
            if (json.hasProperty ("keywords")) d.keywords = json["keywords"].toString().substring (0, 200);
            saveDevices();
            return jsonResponse (juce::var (true));
        }

        if (method == "POST" && action == "visibility")
        {
            const auto json = juce::JSON::parse (body);
            if (! json.isObject() || ! json.hasProperty ("user"))
                return jsonResponse (juce::var ("bad json"), 400);

            const juce::ScopedLock sl (devicesLock);
            auto& d = devices[id];
            const auto user = json["user"].toString();
            if ((bool) json["visible"])
                d.hidden.erase (user);
            else
                d.hidden.insert (user);
            saveDevices();
            return jsonResponse (juce::var (true));
        }

        if (method == "POST" && action == "layout")
        {
            // Dashboard anpassen: Reihenfolge und ausgeblendete Personen.
            // Personen anlegen oder löschen geht hier bewusst nicht.
            const auto json = juce::JSON::parse (body);
            if (! json.isObject())
                return jsonResponse (juce::var ("bad json"), 400);

            const juce::ScopedLock sl (devicesLock);
            auto& d = devices[id];
            if (json.hasProperty ("order"))
                d.order = BoardLayout::sanitise (json["order"]);
            if (json.hasProperty ("hidden"))
            {
                d.hidden.clear();
                for (auto& h : BoardLayout::sanitise (json["hidden"]))
                    d.hidden.insert (h);
            }
            saveDevices();
            return jsonResponse (juce::var (true));
        }

        return jsonResponse (juce::var ("unknown action"), 404);
    }

    if (method != "GET" && method != "POST")
        return textResponse ("method not allowed", 405, "text/plain");

    return textResponse ("not found", 404, "text/plain");
}

WebServer::Response WebServer::stateJson (const juce::String& deviceId)
{
    Device device;
    {
        const juce::ScopedLock sl (devicesLock);
        device = devices[deviceId];
    }

    std::shared_ptr<const Snapshot> snap;
    {
        const juce::ScopedLock sl (snapshotLock);
        snap = snapshot;
    }

    // Jedes Gerät hat seinen eigenen Alarm-Namen
    KeywordMatcher matcher;
    matcher.setKeywords (device.keywords.toStdString());

    auto entryJson = [&matcher] (const Entry& e, bool live)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", e.id);
        o->setProperty ("user", e.userId);
        o->setProperty ("time", e.time);
        o->setProperty ("text", e.text);
        o->setProperty ("utterance", e.utterance);
        o->setProperty ("live", live);

        juce::Array<juce::var> ranges;
        for (auto& m : matcher.findMatches (e.text.toStdString()))
            ranges.add (juce::Array<juce::var> { (int) m.charStart, (int) m.charLength });
        o->setProperty ("mention", ! ranges.isEmpty());
        o->setProperty ("ranges", ranges);
        return juce::var (o);
    };

    juce::Array<juce::var> persons, entries, live, hidden;

    juce::StringArray ids;
    for (auto& p : snap->persons)
        ids.add (p.id);

    for (int index : BoardLayout::order (ids, device.order))
    {
        const auto& p = snap->persons[(size_t) index];
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", p.id);
        o->setProperty ("name", p.name);
        o->setProperty ("role", p.role);
        o->setProperty ("colour", p.colour);
        o->setProperty ("avatar", p.avatarKey.isNotEmpty() ? "/avatar/" + p.id + "?v=" + p.avatarKey : juce::String());
        o->setProperty ("talkback", p.talkback);
        o->setProperty ("cutout", p.cutout);
        o->setProperty ("speaking", p.speaking);
        o->setProperty ("visible", device.hidden.count (p.id) == 0);
        o->setProperty ("rf", rfJson (p));
        persons.add (juce::var (o));
    }

    for (auto& e : snap->entries)
        entries.add (entryJson (e, false));

    for (auto& e : snap->live)
        live.add (entryJson (e, true));

    for (auto& h : device.hidden)
        hidden.add (h);

    auto* dev = new juce::DynamicObject();
    dev->setProperty ("id", device.id);
    dev->setProperty ("label", device.label);
    dev->setProperty ("keywords", device.keywords);

    auto* root = new juce::DynamicObject();
    root->setProperty ("device", juce::var (dev));
    root->setProperty ("listening", snap->listening);
    root->setProperty ("persons", persons);
    root->setProperty ("entries", entries);
    root->setProperty ("live", live);
    return jsonResponse (juce::var (root));
}

//==============================================================================
juce::String WebServer::createDevice()
{
    static const char* alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // ohne 0/O, 1/I
    juce::Random rnd;

    const juce::ScopedLock sl (devicesLock);

    juce::String id;
    do
    {
        id.clear();
        for (int i = 0; i < 6; ++i)
            id += juce::String::charToString ((juce::juce_wchar) alphabet[rnd.nextInt (32)]);
    }
    while (devices.count (id) > 0);

    Device d;
    d.id = id;
    d.label = juce::String::fromUTF8 ("Gerät ") + juce::String ((int) devices.size() + 1);
    d.lastSeenMs = juce::Time::currentTimeMillis();
    devices[id] = d;
    saveDevices();
    return id;
}

std::vector<WebServer::Device> WebServer::getDevices() const
{
    const juce::ScopedLock sl (devicesLock);
    std::vector<Device> v;
    for (auto& [id, d] : devices)
        v.push_back (d);
    return v;
}

void WebServer::removeDevice (const juce::String& id)
{
    const juce::ScopedLock sl (devicesLock);
    devices.erase (id);
    saveDevices();
}

void WebServer::loadDevices()
{
    devices.clear();
    const auto json = juce::JSON::parse (devicesFile);

    if (auto* arr = json.getArray())
    {
        for (auto& v : *arr)
        {
            Device d;
            d.id = v["id"].toString();
            if (! isValidDeviceId (d.id))
                continue;

            d.label = v["label"].toString();
            d.keywords = v["keywords"].toString();
            d.lastSeenMs = (juce::int64) v["lastSeen"];
            if (auto* h = v["hidden"].getArray())
                for (auto& x : *h)
                    d.hidden.insert (x.toString());
            d.order = BoardLayout::sanitise (v["order"]);
            devices[d.id] = d;
        }
    }
}

void WebServer::saveDevices() const
{
    if (devicesFile == juce::File())
        return;

    juce::Array<juce::var> arr;
    for (auto& [id, d] : devices)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", d.id);
        o->setProperty ("label", d.label);
        o->setProperty ("keywords", d.keywords);
        o->setProperty ("lastSeen", d.lastSeenMs);
        juce::Array<juce::var> h;
        for (auto& x : d.hidden)
            h.add (x);
        o->setProperty ("hidden", h);
        juce::Array<juce::var> ord;
        for (auto& x : d.order)
            ord.add (x);
        o->setProperty ("order", ord);
        arr.add (juce::var (o));
    }

    devicesFile.replaceWithText (juce::JSON::toString (arr));
}
