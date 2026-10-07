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

    listeners.call ([&] (Listener& l) { l.transcriptEntryAdded (added); });
    return entries.back();
}

void TranscriptModel::clear()
{
    entries.clear();
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
