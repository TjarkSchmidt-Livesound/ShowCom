#pragma once

#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

/**
    Spitznamen beim Namens-Alarm mitberücksichtigen: Steht "Tjark" unter "Mein Name" und hat die
    Person Tjark den Spitznamen "Tjarki", löst auch "Tjarki" den Alarm aus.
*/
namespace Nicknames
{
    /** Kommagetrennte Liste aufteilen und säubern. */
    inline juce::StringArray split (const juce::String& list)
    {
        juce::StringArray a;
        a.addTokens (list, ",;\n", "");
        a.trim();
        a.removeEmptyStrings();
        return a;
    }

    /** people: (Name, Spitznamen kommagetrennt). Liefert die erweiterte Schlüsselwortliste. */
    inline juce::String expandKeywords (const juce::String& keywords,
                                        const std::vector<std::pair<juce::String, juce::String>>& people)
    {
        auto words = split (keywords);
        const auto original = words;

        for (auto& [name, nicks] : people)
        {
            auto all = split (nicks);
            all.insert (0, name.trim());

            bool matches = false;
            for (auto& w : all)
                matches = matches || original.contains (w, true);

            if (matches)
                for (auto& w : all)
                    if (w.isNotEmpty() && ! words.contains (w, true))
                        words.add (w);
        }

        return words.joinIntoString (", ");
    }
}
