#pragma once

#include <cmath>
#include <string>

/**
    Zustand eines Funkkanals (Empfängerkanal + zugehöriger Sender), herstellerunabhängig.
    -1 bzw. NAN bedeutet "unbekannt".
*/
struct WirelessChannelState
{
    std::string name;            // Kanalname am Empfänger
    std::string txModel;         // z. B. "ULXD2", "SLXD1", "SKM", "SK"
    std::string batteryType;     // z. B. "SB900", "Battery", "Rechargeable"
    std::string antenna;         // aktive Antenne(n), z. B. "AB", "A"
    int frequencyKHz = 0;

    int batteryPercent = -1;     // 0..100
    int batteryBars = -1;        // 0..5
    int batteryMinutes = -1;     // Restlaufzeit

    float rfDbm = NAN;           // HF-Pegel (Shure)
    int rfQualityPercent = -1;   // Signalqualität (Sennheiser RSQI)
    float audioDb = NAN;         // Audiopegel am Empfänger (dBFS)

    bool txOn = false;           // Sender eingeschaltet / verbunden
    bool muted = false;

    /** Batterie in Prozent, notfalls aus den Balken geschätzt. */
    int batteryPercentEstimate() const
    {
        if (batteryPercent >= 0) return batteryPercent;
        if (batteryBars >= 0)    return batteryBars * 20;
        return -1;
    }

    /** Einheitliche Signalstärke 0..1 für Grafiken. */
    float signalNormalised() const
    {
        if (! std::isnan (rfDbm))
            return std::fmax (0.0f, std::fmin (1.0f, (rfDbm + 100.0f) / 70.0f));   // -100..-30 dBm
        if (rfQualityPercent >= 0)
            return (float) rfQualityPercent / 100.0f;
        return 0.0f;
    }
};
