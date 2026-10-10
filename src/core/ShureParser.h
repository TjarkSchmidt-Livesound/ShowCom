#pragma once

#include "WirelessState.h"

#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

/**
    Parser für das Shure-ASCII-Protokoll (TCP-Port 2202), wie es QLX-D, SLX-D, SLX-D+
    und Axient Digital (inkl. ADX) sprechen – und, so weit bekannt, Axient Digital PSM:

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
    enum class Family { unknown, ulxd, slxd, axientDigital, adPsm };

    /** Füttert empfangene Bytes; gibt die Kanäle zurück, die sich geändert haben. */
    std::set<int> feed (const std::string& bytes);

    const std::map<int, WirelessChannelState>& getChannels() const   { return channels; }
    Family getFamily() const                                           { return family; }
    const std::string& getModel() const                                { return model; }

    /** Befehle beim Verbindungsaufbau (alle Kanäle und zusätzlich einzeln – nicht jede Serie kennt Kanal 0). */
    static std::vector<std::string> initialCommands (int numChannels = 4);

    /** Hat das Gerät schon mit etwas Gültigem geantwortet? */
    bool hasAnswered() const                                           { return answered; }
    int getErrorCount() const                                          { return errors; }

    /** Gerät als In-Ear-Sender behandeln (Axient Digital PSM), auch bevor das Modell gemeldet ist. */
    void setTransmitterMode (bool b)                                   { transmitterMode = b; if (b) family = Family::adPsm; }

    /** Schlüssel, die der Parser nicht kennt (zur Diagnose, z. B. bei neuen Geräten). */
    const std::set<std::string>& getUnknownKeys() const                { return unknownKeys; }
    /** Befehl für die regelmäßige Aktualisierung. */
    static std::string refreshCommand()    { return "< GET 0 ALL >"; }

private:
    void handleMessage (const std::vector<std::string>& tokens, std::set<int>& changed);
    void handleSample (int ch, const std::vector<std::string>& v, WirelessChannelState& s);
    void detectFamily (const std::string& text);

    std::string buffer;
    std::map<int, WirelessChannelState> channels;
    std::map<int, std::array<float, 2>> antennaRssi;   // SLX-D/AD: RSSI je Antenne
    std::map<int, std::map<int, bool>> slotRfMute;      // AD: ShowLink-Slot -> RF stumm
    std::map<int, std::map<int, bool>> slotOccupied;    // AD: Slot mit Sender
    std::map<int, bool> txAfMute, rxAfMute;
    std::set<std::string> unknownKeys;
    bool transmitterMode = false;
    bool answered = false;
    int errors = 0;
    Family family = Family::unknown;
    std::string model;
};
