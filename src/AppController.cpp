#include "AppController.h"

#include <BinaryData.h>
#include "Nicknames.h"

AppController::AppController()
{
    state.load();

    transcriber.onResult = [this] (const Transcriber::Result& r)
    {
        if (r.text.isEmpty())
        {
            const auto& live = transcript.getLive();
            if (auto it = live.find (r.userId); it != live.end() && it->second.utteranceId <= r.utteranceId)
                transcript.clearLive (r.userId);
            return;
        }

        TranscriptEntry e;
        e.userId = r.userId;
        e.time = r.wallClock;
        e.sessionOffsetSec = r.sessionOffsetSec;
        e.durationSec = r.durationSec;
        e.text = r.text;
        e.utteranceId = r.utteranceId;

        if (auto* u = state.findUser (r.userId))
            e.speakerName = u->name;
        else
            e.speakerName = "Unbekannt";

        if (r.partial)
        {
            // Ein später Zwischenstand darf ein schon fertiges Endergebnis nicht überschreiben
            for (auto it = transcript.getEntries().rbegin(); it != transcript.getEntries().rend(); ++it)
                if (it->userId == r.userId)
                {
                    if (it->utteranceId >= r.utteranceId)
                        return;
                    break;
                }

            transcript.setLive (std::move (e));
        }
        else
        {
            transcript.add (std::move (e));
        }
    };

    // Läuft auf dem Verarbeitungs-Thread der Engine; enqueue ist thread-sicher.
    engine.onSegment = [this] (AudioEngine::Segment&& s)
    {
        Transcriber::Job job;
        job.userId = s.userId;
        job.audio = std::move (s.audio);
        job.sampleRate = s.sampleRate;
        job.sessionOffsetSec = s.sessionOffsetSec;
        job.wallClock = s.wallClock;
        job.partial = s.partial;
        job.utteranceId = s.utteranceId;
        transcriber.enqueue (std::move (job));
    };

    const auto error = engine.initialise (state.getSettings().audioDeviceState);
    if (error.isNotEmpty())
        DBG ("Audio init: " << error);

    engine.getDeviceManager().addChangeListener (this);
    state.addChangeListener (this);
    applyState();

    // Von Anfang an zuhören: Es soll nichts verloren gehen, auch wenn niemand auf "Start" drückt.
    setListening (true);

    web.onCompanionCommand = [weak = juce::WeakReference<AppController> (this)] (const juce::String& cmd)
    {
        juce::MessageManager::callAsync ([weak, cmd]
        {
            if (auto* self = weak.get())
                self->handleCompanionCommand (cmd);
        });
    };

    web.setDevicesFile (AppState::getAppDataDir().getChildFile ("web_devices.json"));
    web.configure (state.getSettings().webEnabled, state.getSettings().webPort);
    startTimerHz (4);
}

AppController::~AppController()
{
    stopTimer();
    web.configure (false, 0);
    engine.setListening (false);
    state.removeChangeListener (this);
    engine.getDeviceManager().removeChangeListener (this);
    state.setAudioDeviceState (engine.createStateXml());
}

//==============================================================================
void AppController::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.getDeviceManager())
    {
        state.setAudioDeviceState (engine.createStateXml());
        applyState();   // Kanal-Zuordnung nach Gerätewechsel erneut anwenden

        if (onAudioDeviceChanged)
            onAudioDeviceChanged();

        return;
    }

    applyState();
}

juce::String AppController::buildPrompt() const
{
    const auto& s = state.getSettings();
    const bool german = s.language == "de" || s.language == "auto";

    juce::StringArray people;
    for (auto& u : state.getUsers())
    {
        auto who = u.nickname.isNotEmpty() ? u.name + " / " + Nicknames::split (u.nickname).joinIntoString (" / ") : u.name;
        people.add (u.role.isNotEmpty() ? who + " (" + u.role + ")" : who);
    }

    juce::StringArray keywords;
    keywords.addTokens (s.keywords, ",;\n", "");
    keywords.trim();
    keywords.removeEmptyStrings();

    // Der Prompt gibt Whisper Kontext: Thema, Schreibweise von Namen und Fachwörtern.
    // Whisper nutzt davon maximal ~220 Tokens – deshalb knapp halten.
    juce::String prompt = german
        ? juce::String::fromUTF8 ("Talkback bei einer Live-Veranstaltung: FOH, Monitor, Wedge, In-Ear, Soundcheck, "
                                  "Line-Check, Funkstrecke, Handsender, Taschensender, Bühne, Backline, Changeover.")
        : juce::String ("Talkback at a live show: FOH, monitors, wedge, in-ear, soundcheck, line check, "
                        "wireless, handheld, bodypack, stage, backline, changeover.");

    if (! people.isEmpty())
        prompt << (german ? " Beteiligte: " : " Crew: ") << people.joinIntoString (", ") << ".";

    if (! keywords.isEmpty())
        prompt << (german ? " Namen: " : " Names: ") << keywords.joinIntoString (", ") << ".";

    juce::StringArray vocab;
    vocab.addTokens (s.vocabulary, ",;\n", "");
    vocab.trim();
    vocab.removeEmptyStrings();
    while (vocab.size() > 40)
        vocab.remove (vocab.size() - 1);

    if (! vocab.isEmpty())
        prompt << (german ? juce::String::fromUTF8 (" Begriffe: ") : juce::String (" Terms: ")) << vocab.joinIntoString (", ") << ".";

    return prompt;
}

juce::StringArray AppController::buildHotwords() const
{
    // Parakeet bevorzugt diese Begriffe: Namen der Crew, Alarm-Namen, eigene Begriffe
    // und englische Bühnenbegriffe, die im deutschen Satz sonst gern falsch geschrieben werden.
    const auto& s = state.getSettings();
    juce::StringArray words;

    for (auto& u : state.getUsers())
    {
        words.add (u.name);
        words.addArray (Nicknames::split (u.nickname));
        if (u.role.isNotEmpty())
            words.add (u.role);
    }

    juce::StringArray more;
    more.addTokens (s.keywords + "," + s.vocabulary, ",;\n", "");
    more.trim();
    words.addArray (more);

    for (auto* w : { "Talkback", "Wedge", "In-Ear", "FOH", "Line Check", "Soundcheck", "Changeover", "Backline", "Handsender" })
        words.add (w);

    words.trim();
    words.removeEmptyStrings();
    words.removeDuplicates (true);
    while (words.size() > 80)
        words.remove (words.size() - 1);
    return words;
}

juce::File AppController::getVadModelFile()
{
    // Silero VAD (630 KB) steckt im Programm und wird beim ersten Start in den Modell-Ordner gelegt
    auto f = AppState::getModelsDir().getChildFile ("silero_vad.onnx");
    if (f.getSize() != (juce::int64) BinaryData::silero_vad_onnxSize)
    {
        f.getParentDirectory().createDirectory();
        f.replaceWithData (BinaryData::silero_vad_onnx, (size_t) BinaryData::silero_vad_onnxSize);
    }
    return f;
}

void AppController::applyState()
{
    const auto& s = state.getSettings();

    std::vector<AudioEngine::Assignment> assignments;
    for (auto& u : state.getUsers())
    {
        if (! AppState::logsTalkback (u))
            continue;   // nur Funküberwachung: kein Audio

        AudioEngine::Assignment a;
        a.userId = u.id;
        a.channel = u.channel;
        a.thresholdDb = u.thresholdDb;
        assignments.push_back (a);
    }
    engine.setAssignments (assignments);
    wireless.setDevices (s.wirelessDevices);
    web.configure (s.webEnabled, s.webPort);
    engine.setLiveInterval (s.liveText ? 0.8f : 0.0f);

    Transcriber::Options o;
    o.model = s.modelPath.isNotEmpty() ? juce::File (s.modelPath) : juce::File();
    o.vadModel = getVadModelFile();
    o.language = s.language;
    o.useGpu = s.useGpu;
    o.mode = s.recognitionMode;
    o.prompt = buildPrompt();
    o.hotwords = buildHotwords();
    transcriber.setOptions (o);

    // Spitznamen: steht "Tjark" unter "Mein Name", lösen auch Tjarks Spitznamen den Alarm aus
    std::vector<std::pair<juce::String, juce::String>> people;
    for (auto& u : state.getUsers())
        people.push_back ({ u.name, u.nickname });
    const auto effective = Nicknames::expandKeywords (s.keywords, people);

    if (! keywordsApplied || appliedKeywords != effective || appliedFuzzy != s.fuzzyKeywords)
    {
        transcript.setKeywords (effective, s.fuzzyKeywords);
        appliedKeywords = effective;
        appliedFuzzy = s.fuzzyKeywords;
        keywordsApplied = true;
        mentionScanPos = 0;   // Erwähnungen neu zählen
        mentionCount = 0;
    }
}

//==============================================================================
void AppController::setListening (bool shouldListen)
{
    if (shouldListen == engine.isListening())
        return;

    engine.setListening (shouldListen);

    if (onListeningChanged)
        onListeningChanged();
}

void AppController::injectTestMention()
{
    const auto& s = state.getSettings();

    juce::StringArray keywords;
    keywords.addTokens (s.keywords, ",;\n", "");
    keywords.trim();
    keywords.removeEmptyStrings();

    TranscriptEntry e;
    e.time = juce::Time::getCurrentTime();
    e.speakerName = "Test";

    if (! state.getUsers().empty())
    {
        e.userId = state.getUsers().front().id;
        e.speakerName = state.getUsers().front().name;
    }

    e.text = keywords.isEmpty() ? juce::String ("Test: Bitte trage zuerst deinen Namen in den Einstellungen ein.")
                                : keywords[0] + juce::String::fromUTF8 (", kannst du mal kurz zur Bühne kommen? (Testalarm)");
    transcript.add (std::move (e));
}

void AppController::handleCompanionCommand (const juce::String& cmd)
{
    if      (cmd == "ack")           { if (acknowledgeAlarm) acknowledgeAlarm(); }
    else if (cmd == "listen")        setListening (true);
    else if (cmd == "pause")         setListening (false);
    else if (cmd == "toggleListen")  setListening (! isListening());
    else if (cmd == "testAlarm")     injectTestMention();
    else if (cmd == "popup")         { if (toggleAlarmPopup) toggleAlarmPopup(); }
}

void AppController::timerCallback()
{
    if (! web.isRunning())
        return;

    const auto& s = state.getSettings();
    auto snap = std::make_shared<WebServer::Snapshot>();
    snap->listening = engine.isListening();
    snap->companionEnabled = s.companionEnabled;
    snap->companionControl = s.companionControl;
    snap->companionKey = s.companionKey;
    snap->hostKeywords = appliedKeywords;
    snap->appVersion = juce::JUCEApplicationBase::getInstance() != nullptr
                         ? juce::JUCEApplicationBase::getInstance()->getApplicationVersion() : juce::String();
    if (getAlarmInfo)
        snap->alarm = getAlarmInfo();

    {
        const auto& all = transcript.getEntries();
        if (mentionScanPos > all.size())
            mentionScanPos = 0, mentionCount = 0;   // Protokoll geleert
        for (; mentionScanPos < all.size(); ++mentionScanPos)
            if (all[mentionScanPos].isMention())
                ++mentionCount;
        snap->mentionCount = mentionCount;
    }

    for (auto& u : state.getUsers())
    {
        WebServer::Person p;
        p.id = u.id;
        p.name = u.name;
        p.role = u.role;
        p.nickname = u.nickname;
        p.colour = "#" + u.colour.toDisplayString (false).toLowerCase();
        p.talkback = u.talkbackLog;
        p.favorite = u.favorite;
        p.speaking = AppState::logsTalkback (u) && engine.isSpeaking (u.channel);

        const auto rf = getRfInfo (u);
        p.rfAssigned = rf.assigned;
        p.rfOnline = rf.online;
        p.rfKnown = rf.known;
        p.rfDisabled = rf.disabled;
        p.rf = rf.state;
        if (rf.history != nullptr)
        {
            const int n = juce::jmin ((int) rf.history->size(), 120);   // 30 s
            p.history.assign (rf.history->end() - n, rf.history->end());
        }
        // Pegel direkt von der Funkstrecke (gewählt oder Person ohne Talkback-Eingang)
        if (rf.audioHistory != nullptr && (u.levelFromWireless || ! AppState::logsTalkback (u)))
        {
            const int n = juce::jmin ((int) rf.audioHistory->size(), 120);
            p.level.assign (rf.audioHistory->end() - n, rf.audioHistory->end());
        }

        const auto iem = getIemInfo (u);
        p.iemAssigned = iem.assigned;
        p.iemOnline = iem.online;
        p.iemKnown = iem.known;
        p.iemDisabled = iem.disabled;
        p.iem = iem.state;

        // Avatar als PNG, nur neu kodieren, wenn sich das Bild geändert hat
        if (u.avatarFile.isNotEmpty())
        {
            const bool cut = state.isAvatarCutout (u.id);
            const auto key = u.avatarFile + (cut ? "#cut" : "");
            p.cutout = cut;

            auto& cached = webAvatarCache[u.id];
            if (cached.first != key || cached.second == nullptr)
            {
                auto block = std::make_shared<juce::MemoryBlock>();
                juce::MemoryOutputStream out (*block, false);
                juce::PNGImageFormat png;
                const auto img = state.getAvatar (u.id);
                if (img.isValid() && png.writeImageToStream (img, out))
                {
                    out.flush();
                    cached = { key, block };
                }
            }

            if (cached.second != nullptr)
            {
                p.avatarKey = juce::String (key.hashCode64());
                snap->avatars[u.id] = cached.second;
            }
        }

        snap->persons.push_back (std::move (p));
    }

    const auto& entries = transcript.getEntries();
    const size_t first = entries.size() > 80 ? entries.size() - 80 : 0;
    for (size_t i = first; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        snap->entries.push_back ({ e.id, e.userId, e.time.formatted ("%H:%M:%S"), e.text, e.utteranceId });
    }

    for (auto& [uid, e] : transcript.getLive())
        snap->live.push_back ({ 0, uid, e.time.formatted ("%H:%M:%S"), e.text, e.utteranceId });

    web.publish (std::move (snap));
}

AppController::RfInfo AppController::getRfInfo (const UserProfile& u) const
{
    RfInfo info;
    auto deviceId = u.rfDeviceId;
    int channel = u.rfChannel;

    if (deviceId.isEmpty() && demoMode)
    {
        const auto& users = state.getUsers();
        for (size_t i = 0; i < users.size(); ++i)
            if (users[i].id == u.id)
                channel = (int) (i % 8) + 1;

        deviceId = WirelessManager::demoDeviceId;
    }

    juce::ignoreUnused (info);
    return fillRfInfo (deviceId, channel);
}

AppController::RfInfo AppController::fillRfInfo (const juce::String& deviceId, int channel) const
{
    RfInfo info;
    if (deviceId.isEmpty() || channel <= 0)
        return info;

    info.assigned = true;
    info.disabled = wireless.isDisabled (deviceId, channel);
    if (info.disabled)
        return info;

    const auto status = wireless.getStatus (deviceId);
    info.online = status.online;
    info.deviceName = status.model;
    info.known = wireless.getChannel (deviceId, channel, info.state);
    info.history = wireless.getHistory (deviceId, channel);
    info.audioHistory = wireless.getAudioHistory (deviceId, channel);
    return info;
}

AppController::RfInfo AppController::getIemInfo (const UserProfile& u) const
{
    auto deviceId = u.iemDeviceId;
    int channel = u.iemChannel;

    if (deviceId.isEmpty() && demoMode)
    {
        // Demo: Bühnenpersonen bekommen einen In-Ear-Sender
        const auto& users = state.getUsers();
        int n = 0;
        for (size_t i = 0; i < users.size(); ++i)
        {
            const bool stage = ! users[i].talkbackLog || users[i].role.containsIgnoreCase ("stage");
            if (stage && ++n <= 4 && users[i].id == u.id)
            {
                deviceId = WirelessManager::demoPsmDeviceId;
                channel = n;
            }
        }
    }

    return fillRfInfo (deviceId, channel);
}

void AppController::injectDemoConversation()
{
    demoMode = true;
    wireless.setDemoDevice (true);
    applyState();

    std::vector<UserProfile> users;
    for (auto& u : state.getUsers())
        if (u.talkbackLog)
            users.push_back (u);
    if (users.empty())
        return;

    juce::StringArray keywords;
    keywords.addTokens (state.getSettings().keywords, ",;\n", "");
    keywords.trim();
    keywords.removeEmptyStrings();
    const auto me = keywords.isEmpty() ? juce::String ("Tjark") : keywords[0];

    const char* lines[] = {
        "Check, check, eins zwei. Hört mich jemand auf dem Talkback?",
        "Ja, laut und deutlich. Kannst du auf Wedge drei etwas mehr Gitarre geben?",
        "Mach ich. Wie ist es jetzt?",
        "Perfekt, danke. Sänger möchte bei Song vier weniger Hall im Ear.",
        "%NAME%, kannst du kurz zum Monitorplatz kommen? Der Funkempfänger von Mikro sieben hat Aussetzer.",
        "Bin unterwegs. Licht, gebt mir bitte kurz Arbeitslicht auf der Seitenbühne.",
        "Arbeitslicht ist an. Noch fünf Minuten bis Einlass, bitte alle bereit machen.",
        "Changeover nach Song sechs: Akustikgitarre steht links neben dem Drumriser.",
    };

    auto t = juce::Time::getCurrentTime() - juce::RelativeTime::minutes (6);
    int i = 0;

    for (auto* l : lines)
    {
        const auto& u = users[(size_t) (i % (int) users.size())];
        TranscriptEntry e;
        e.userId = u.id;
        e.speakerName = u.name;
        e.time = t;
        e.durationSec = 3.0;
        e.text = juce::String::fromUTF8 (l).replace ("%NAME%", me);
        transcript.add (std::move (e));

        t = t + juce::RelativeTime::seconds (25 + 17 * (i % 3));
        ++i;
    }

    // Eine Person spricht gerade: Live-Text
    if (users.size() > 2)
    {
        TranscriptEntry e;
        e.userId = users[2].id;
        e.speakerName = users[2].name;
        e.time = juce::Time::getCurrentTime();
        e.utteranceId = 1000;
        e.text = juce::String::fromUTF8 ("Ich bräuchte gleich noch einen zweiten Handsender für die");
        transcript.setLive (std::move (e));
    }
}
