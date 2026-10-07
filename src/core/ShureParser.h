#pragma once

#include "WirelessState.h"

#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

/**
    Parser für das Shure-ASCII-Protokoll (TCP-Port 2202), wie es ULX-D, QLX-D, SLX-D
    und Axient Digital sprechen:

        < REP 1 BATT_BARS 004 >          ULX-D / QLX-D
        < REP 1 TX_BATT_BARS 004 >       SLX-D / Axient Digital
        < REP 1 CHAN_NAME {Lead Vox} >
        < SAMPLE 1 ALL XB 105 093 >      Meter (ULX-D: Antennen, RF, Audio)

    Unterschiede zwischen den Serien (Offsets, Schlüsselnamen) werden hier aufgefangen.
    Reines C++17 ohne Abhängigkeiten.
*/
class ShureParser
{
public:
    enum class Family { unknown, ulxd, slxd, axientDigital };

    /** Füttert empfangene Bytes; gibt die Kanäle zurück, die sich geändert haben. */
    std::set<int> feed (const std::string& bytes);

    const std::map<int, WirelessChannelState>& getChannels() const   { return channels; }
    Family getFamily() const                                           { return family; }
    const std::string& getModel() const                                { return model; }

    /** Befehle beim Verbindungsaufbau. */
    static std::vector<std::string> initialCommands();
    /** Befehl für die regelmäßige Aktualisierung. */
    static std::string refreshCommand()    { return "< GET 0 ALL >"; }

private:
    void handleMessage (const std::vector<std::string>& tokens, std::set<int>& changed);
    void handleSample (int ch, const std::vector<std::string>& v, WirelessChannelState& s);
    void detectFamily (const std::string& text);

    std::string buffer;
    std::map<int, WirelessChannelState> channels;
    std::map<int, std::array<float, 2>> antennaRssi;   // SLX-D/AD: RSSI je Antenne
    Family family = Family::unknown;
    std::string model;
};
