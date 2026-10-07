#include "AppController.h"

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
}

AppController::~AppController()
{
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
        people.add (u.role.isNotEmpty() ? u.name + " (" + u.role + ")" : u.name);

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

void AppController::applyState()
{
    const auto& s = state.getSettings();

    std::vector<AudioEngine::Assignment> assignments;
    for (auto& u : state.getUsers())
    {
        if (u.channel < 0)
            continue;

        AudioEngine::Assignment a;
        a.userId = u.id;
        a.channel = u.channel;
        a.thresholdDb = u.thresholdDb;
        assignments.push_back (a);
    }
    engine.setAssignments (assignments);
    wireless.setDevices (s.wirelessDevices);
    engine.setLiveInterval (s.liveText ? 0.8f : 0.0f);

    Transcriber::Options o;
    o.model = s.modelPath.isNotEmpty() ? juce::File (s.modelPath) : juce::File();
    o.language = s.language;
    o.useGpu = s.useGpu;
    o.mode = s.recognitionMode;
    o.prompt = buildPrompt();
    transcriber.setOptions (o);

    if (! keywordsApplied || appliedKeywords != s.keywords || appliedFuzzy != s.fuzzyKeywords)
    {
        transcript.setKeywords (s.keywords, s.fuzzyKeywords);
        appliedKeywords = s.keywords;
        appliedFuzzy = s.fuzzyKeywords;
        keywordsApplied = true;
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
                channel = (int) (i % 6) + 1;

        deviceId = WirelessManager::demoDeviceId;
    }

    if (deviceId.isEmpty() || channel <= 0)
        return info;

    info.assigned = true;
    const auto status = wireless.getStatus (deviceId);
    info.online = status.online;
    info.deviceName = status.model;
    info.known = wireless.getChannel (deviceId, channel, info.state);
    info.history = wireless.getHistory (deviceId, channel);
    return info;
}

void AppController::injectDemoConversation()
{
    demoMode = true;
    wireless.setDemoDevice (true);
    applyState();

    const auto& users = state.getUsers();
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
