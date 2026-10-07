#pragma once

#include <juce_core/juce_core.h>
#include "KeywordMatcher.h"

/** Ein transkribierter Sprach-Abschnitt. */
struct TranscriptEntry
{
    juce::int64 id = 0;
    juce::String userId;
    juce::String speakerName;     // Name zum Zeitpunkt der Aufnahme
    juce::Time time;
    double sessionOffsetSec = 0;  // Position in der WAV-Datei der Person
    double durationSec = 0;
    juce::String text;
    std::vector<KeywordMatcher::Match> mentions;

    bool isMention() const        { return ! mentions.empty(); }
};

/**
    Alle Einträge der laufenden Anzeige (nur im Arbeitsspeicher, keine Dateien).
    Nur Message-Thread.
*/
class TranscriptModel
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void transcriptEntryAdded (const TranscriptEntry&) = 0;
        virtual void transcriptReset() = 0;   // geleert oder Erwähnungen neu berechnet
    };

    void addListener (Listener* l)          { listeners.add (l); }
    void removeListener (Listener* l)       { listeners.remove (l); }

    const std::vector<TranscriptEntry>& getEntries() const { return entries; }
    const TranscriptEntry* findEntry (juce::int64 id) const;

    /** Fügt hinzu und sucht Erwähnungen. */
    const TranscriptEntry& add (TranscriptEntry e);
    void clear();

    void setKeywords (const juce::String& keywords, bool fuzzy);

    static juce::String formatLine (const TranscriptEntry& e);

private:
    std::vector<TranscriptEntry> entries;
    juce::ListenerList<Listener> listeners;
    KeywordMatcher matcher;
    juce::int64 nextId = 1;

};
