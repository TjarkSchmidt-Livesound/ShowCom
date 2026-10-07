#include "TranscriptModel.h"

const TranscriptEntry* TranscriptModel::findEntry (juce::int64 id) const
{
    // Einträge sind nach id sortiert
    auto it = std::lower_bound (entries.begin(), entries.end(), id,
                                [] (const TranscriptEntry& e, juce::int64 v) { return e.id < v; });

    return it != entries.end() && it->id == id ? &*it : nullptr;
}

const TranscriptEntry& TranscriptModel::add (TranscriptEntry e)
{
    e.id = nextId++;
    e.mentions = matcher.findMatches (e.text.toStdString());

    entries.push_back (std::move (e));
    const auto& added = entries.back();

    if (logStream != nullptr)
    {
        logStream->writeText (formatLine (added) + "\n", false, false, nullptr);
        logStream->flush();
    }

    listeners.call ([&] (Listener& l) { l.transcriptEntryAdded (added); });
    return entries.back();
}

void TranscriptModel::clear()
{
    entries.clear();
    firstEntryOfSession = 0;
    listeners.call ([] (Listener& l) { l.transcriptReset(); });
}

void TranscriptModel::setKeywords (const juce::String& keywords, bool fuzzy)
{
    matcher.setKeywords (keywords.toStdString());
    matcher.setFuzzy (fuzzy);

    for (auto& e : entries)
        e.mentions = matcher.findMatches (e.text.toStdString());

    listeners.call ([] (Listener& l) { l.transcriptReset(); });
}

juce::String TranscriptModel::formatLine (const TranscriptEntry& e)
{
    return "[" + e.time.formatted ("%H:%M:%S") + "] "
         + (e.isMention() ? "(!) " : "")
         + e.speakerName + ": " + e.text;
}

//==============================================================================
void TranscriptModel::startLog (const juce::File& folder)
{
    stopLog();

    logFolder = folder;
    firstEntryOfSession = entries.size();

    auto file = folder.getChildFile ("transcript.txt");
    logStream = std::make_unique<juce::FileOutputStream> (file);

    if (! logStream->openedOk())
    {
        logStream.reset();
        return;
    }

    logStream->writeText ("Talkback-Transkript  " + juce::Time::getCurrentTime().formatted ("%d.%m.%Y %H:%M:%S")
                          + "\n\n", false, false, nullptr);
}

void TranscriptModel::stopLog()
{
    if (logStream == nullptr)
        return;

    logStream.reset();
    writeJson();
    logFolder = juce::File();
}

void TranscriptModel::writeJson() const
{
    juce::Array<juce::var> arr;

    for (size_t i = firstEntryOfSession; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        auto* o = new juce::DynamicObject();
        o->setProperty ("time",        e.time.toISO8601 (true));
        o->setProperty ("offsetSec",   e.sessionOffsetSec);
        o->setProperty ("durationSec", e.durationSec);
        o->setProperty ("userId",      e.userId);
        o->setProperty ("speaker",     e.speakerName);
        o->setProperty ("text",        e.text);
        o->setProperty ("mention",     e.isMention());
        arr.add (juce::var (o));
    }

    logFolder.getChildFile ("transcript.json").replaceWithText (juce::JSON::toString (arr));
}
