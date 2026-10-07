#pragma once

#include <juce_core/juce_core.h>
#include <vector>

/**
    Reihenfolge eines Dashboards: die gespeicherte Liste von Personen-IDs zuerst (in dieser
    Reihenfolge), alle übrigen danach in der Reihenfolge des Hosts. So tauchen neu angelegte
    Personen automatisch hinten auf und gelöschte verschwinden einfach.
*/
namespace BoardLayout
{
    /** Liefert Indizes in ids in Anzeigereihenfolge. */
    inline std::vector<int> order (const juce::StringArray& ids, const juce::StringArray& savedOrder)
    {
        std::vector<int> result;
        std::vector<bool> used ((size_t) ids.size(), false);

        for (auto& id : savedOrder)
        {
            const int i = ids.indexOf (id);
            if (i >= 0 && ! used[(size_t) i])
            {
                used[(size_t) i] = true;
                result.push_back (i);
            }
        }

        for (int i = 0; i < ids.size(); ++i)
            if (! used[(size_t) i])
                result.push_back (i);

        return result;
    }

    /** Bereinigt eine eingehende Liste (nur sinnvolle IDs, keine Duplikate, begrenzt). */
    inline juce::StringArray sanitise (const juce::var& v)
    {
        juce::StringArray out;
        if (auto* arr = v.getArray())
            for (auto& x : *arr)
            {
                auto s = x.toString().trim();
                if (s.isNotEmpty() && s.length() <= 64 && ! out.contains (s) && out.size() < 256)
                    out.add (s);
            }
        return out;
    }
}
