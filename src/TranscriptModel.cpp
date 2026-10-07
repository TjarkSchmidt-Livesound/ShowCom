#include "TranscriptModel.h"

const TranscriptEntry* TranscriptModel::findEntry (juce::int64 id) const
{
    // Einträge sind nach id sortiert
    auto it = std::lower_bound (entries.begin(), entries.end(), id,
                                [] (const TranscriptEntry& e, juce::int64 v) { return e.id < v; });

    return it != entries.end() && it->id == id ? &*it : nullptr;
}

void TranscriptModel::setLive (TranscriptEntry e)
{
    e.mentions = matcher.findMatches (e.text.toStdString());
    const auto userId = e.userId;
    live[userId] = std::move (e);
    listeners.call ([&] (Listener& l) { l.transcriptLiveChanged (userId); });
}

void TranscriptModel::clearLive (const juce::String& userId)
{
    if (live.erase (userId) > 0)
        listeners.call ([&] (Listener& l) { l.transcriptLiveChanged (userId); });
}

const TranscriptEntry& TranscriptModel::add (TranscriptEntry e)
{
    // Endergebnis ersetzt den Live-Text derselben Ansage
    if (auto it = live.find (e.userId); it != live.end() && it->second.utteranceId <= e.utteranceId)
    {
        live.erase (it);
        const auto uid = e.userId;
        listeners.call ([&] (Listener& l) { l.transcriptLiveChanged (uid); });
    }

    e.id = nextId++;
    e.mentions = matcher.findMatches (e.text.toStdString());

    entries.push_back (std::move (e));
    const auto& added = entries.back();

    listeners.call ([&] (Listener& l) { l.transcriptEntryAdded (added); });
    return entries.back();
}

void TranscriptModel::clear()
{
    entries.clear();
    live.clear();
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
