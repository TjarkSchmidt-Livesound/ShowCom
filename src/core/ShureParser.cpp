#include "ShureParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace
{
    bool isNumber (const std::string& s)
    {
        return ! s.empty() && std::all_of (s.begin(), s.end(), [] (char c) { return std::isdigit ((unsigned char) c) != 0; });
    }

    int toInt (const std::string& s, int fallback = -1)
    {
        return isNumber (s) ? std::atoi (s.c_str()) : fallback;
    }

    std::string trim (const std::string& s)
    {
        const auto a = s.find_first_not_of (" \t\r\n");
        if (a == std::string::npos)
            return {};
        const auto b = s.find_last_not_of (" \t\r\n");
        return s.substr (a, b - a + 1);
    }

    std::string upper (std::string s)
    {
        for (auto& c : s) c = (char) std::toupper ((unsigned char) c);
        return s;
    }

    /** Zerlegt "REP 1 CHAN_NAME {Lead Vox   }" in Tokens; {…} bleibt ein Token. */
    std::vector<std::string> tokenise (const std::string& body)
    {
        std::vector<std::string> tokens;
        size_t i = 0;

        while (i < body.size())
        {
            while (i < body.size() && body[i] == ' ') ++i;
            if (i >= body.size()) break;

            if (body[i] == '{')
            {
                const auto end = body.find ('}', i + 1);
                const auto stop = end == std::string::npos ? body.size() : end;
                tokens.push_back (trim (body.substr (i + 1, stop - i - 1)));
                i = stop + 1;
            }
            else
            {
                const auto end = body.find (' ', i);
                const auto stop = end == std::string::npos ? body.size() : end;
                tokens.push_back (body.substr (i, stop - i));
                i = stop;
            }
        }

        return tokens;
    }

    bool isUnknown (const std::string& v)
    {
        const auto u = upper (v);
        return u.empty() || u == "UNKN" || u == "UNKNOWN" || u == "NONE";
    }
}

std::vector<std::string> ShureParser::initialCommands()
{
    // Meter alle 250 ms: flüssiger Pegel- und HF-Verlauf (kleinster Wert der Geräte: 100 ms)
    return { "< GET MODEL >", "< GET 0 ALL >", "< SET 0 METER_RATE 00250 >" };
}

void ShureParser::detectFamily (const std::string& text)
{
    const auto u = upper (text);

    if (u.find ("ADTD") != std::string::npos || u.find ("ADTQ") != std::string::npos || u.find ("PSM") != std::string::npos)
        family = Family::adPsm;
    else if (u.find ("SLXD") != std::string::npos)
        family = Family::slxd;
    else if (u.find ("AD4") != std::string::npos || u.find ("ADX") != std::string::npos
             || u.rfind ("AD1", 0) == 0 || u.rfind ("AD2", 0) == 0)
        family = Family::axientDigital;
    else if (u.find ("ULXD") != std::string::npos || u.find ("QLXD") != std::string::npos)
        family = Family::ulxd;
}

std::set<int> ShureParser::feed (const std::string& bytes)
{
    std::set<int> changed;
    buffer += bytes;

    for (;;)
    {
        const auto start = buffer.find ('<');
        if (start == std::string::npos) { buffer.clear(); break; }

        const auto end = buffer.find ('>', start + 1);
        if (end == std::string::npos)
        {
            buffer.erase (0, start);
            if (buffer.size() > 4096) buffer.clear();   // Müll, nicht ewig puffern
            break;
        }

        const auto tokens = tokenise (buffer.substr (start + 1, end - start - 1));
        buffer.erase (0, end + 1);

        if (! tokens.empty())
            handleMessage (tokens, changed);
    }

    return changed;
}

void ShureParser::handleMessage (const std::vector<std::string>& t, std::set<int>& changed)
{
    const auto kind = upper (t[0]);

    if (kind == "REP" && t.size() >= 3 && upper (t[1]) == "MODEL")
    {
        model = t[2];
        detectFamily (model);
        return;
    }

    if ((kind != "REP" && kind != "SAMPLE") || t.size() < 3 || ! isNumber (t[1]))
        return;

    const int ch = toInt (t[1]);
    if (ch <= 0)
        return;

    auto& s = channels[ch];
    const auto key = upper (t[2]);
    std::vector<std::string> v (t.begin() + 3, t.end());
    const auto first = v.empty() ? std::string() : v[0];

    if (kind == "SAMPLE")
    {
        if (key == "ALL")
        {
            handleSample (ch, v, s);
            changed.insert (ch);
        }
        return;
    }

    auto upd = [&] { s.afMuted = txAfMute[ch] || rxAfMute[ch]; s.updateMuteSummary(); };

    // --- Mute: AF (Audio) und RF (Träger) getrennt -------------------------------
    if (key == "TX_MUTE_STATUS" || key == "TX_MUTE" || key == "TX_MUTE_MODE_STATUS" || key == "TX_MUTE_BUTTON_STATUS")
    {
        const auto u = upper (first);
        // Axient Digital: TX_MUTE_MODE_STATUS ON = Sender sendet Audio, MUTE = stumm.
        // QLX-D/SLX-D: TX_MUTE_STATUS ON = stumm.
        txAfMute[ch] = key == "TX_MUTE_MODE_STATUS" ? (u == "MUTE" || u == "MUTED")
                                                     : (u == "ON" || u == "MUTE" || u == "MUTED" || u == "PRESSED");
        upd();
        changed.insert (ch);
        return;
    }
    if (key == "AUDIO_MUTE")
    {
        rxAfMute[ch] = upper (first) == "ON";
        upd();
        changed.insert (ch);
        return;
    }
    if (key == "SLOT_RF_OUTPUT" && v.size() >= 2)
    {
        // "< REP 1 SLOT_RF_OUTPUT 1 RF_MUTE >": ShowLink-Slot des Senders
        slotRfMute[ch][toInt (v[0])] = upper (v[1]) == "RF_MUTE";
        bool any = false;
        for (auto& [slot, m] : slotRfMute[ch])
            any = any || (m && (slotOccupied[ch].count (slot) == 0 || slotOccupied[ch][slot]));
        s.rfMuted = any;
        s.updateMuteSummary();
        changed.insert (ch);
        return;
    }
    if (key == "SLOT_TX_MODEL" && v.size() >= 2)
    {
        slotOccupied[ch][toInt (v[0])] = ! isUnknown (v[1]);
        return;
    }

    // --- In-Ear-Sender (Axient Digital PSM) -------------------------------------
    // Shure hat für ADTD/ADTQ noch keine Befehlsliste veröffentlicht. Erkannt werden die
    // Schlüssel nach dem Muster der Axient-Digital-Familie; Unbekanntes landet in getUnknownKeys().
    if (family == Family::adPsm || transmitterMode)
    {
        s.isIemTransmitter = true;
        const auto u = upper (first);
        const bool rfKey = key.find ("RF") != std::string::npos
                           && (key.find ("OUTPUT") != std::string::npos || key.find ("MUTE") != std::string::npos
                               || key.find ("ENABLE") != std::string::npos || key.find ("TX_STATE") != std::string::npos);
        const auto& val = (key == "SLOT_RF_OUTPUT" || v.size() >= 2) && ! isNumber (v.back()) ? upper (v.back()) : u;

        if (rfKey && ! val.empty() && ! isNumber (val))
        {
            const bool muteKey = key.find ("MUTE") != std::string::npos;
            const bool on = muteKey ? ! (val == "ON" || val == "MUTE" || val == "RF_MUTE")
                                    : (val == "ON" || val == "RF_ON" || val == "ENABLED" || val == "ENABLE");
            s.rfOn = on;
            s.txOn = on;
            s.rfMuted = ! on;
            s.updateMuteSummary();
            changed.insert (ch);
            return;
        }

        if ((key.find ("AUDIO") != std::string::npos || key.find ("INPUT") != std::string::npos)
            && (key.find ("LEVEL") != std::string::npos || key.find ("LVL") != std::string::npos || key.find ("METER") != std::string::npos))
        {
            // Pegel wie bei Axient Digital: gemeldet − 120 = dBFS; optional zwei Werte (L/R)
            const int a = toInt (first);
            const int b = v.size() >= 2 ? toInt (v[1]) : -1;
            const bool right = key.find ("_R") != std::string::npos || key.find ("RIGHT") != std::string::npos;
            if (a >= 0) (right ? s.inputRightDb : s.inputLeftDb) = (float) (a - 120);
            if (b >= 0) s.inputRightDb = (float) (b - 120);
            if (! right && b < 0 && std::isnan (s.inputRightDb)) s.inputRightDb = s.inputLeftDb;
            changed.insert (ch);
            return;
        }
    }

    if (key == "BATT_BARS" || key == "TX_BATT_BARS")
    {
        const int n = toInt (first);
        s.batteryBars = (n >= 0 && n <= 5) ? n : -1;
    }
    else if (key == "BATT_CHARGE" || key == "TX_BATT_CHARGE_PERCENT" || key == "TX_BATT_CHARGE")
    {
        const int n = toInt (first);
        s.batteryPercent = (n >= 0 && n <= 100) ? n : -1;
    }
    else if (key == "BATT_RUN_TIME" || key == "TX_BATT_MINS")
    {
        const int n = toInt (first);
        s.batteryMinutes = (n >= 0 && n <= 65532) ? n : -1;
    }
    else if (key == "BATT_TYPE" || key == "TX_BATT_TYPE")
    {
        s.batteryType = isUnknown (first) ? std::string() : first;
    }
    else if (key == "FREQUENCY")
    {
        s.frequencyKHz = std::max (0, toInt (first, 0));
    }
    else if (key == "CHAN_NAME")
    {
        s.name = first;
    }
    else if (key == "TX_TYPE" || key == "TX_MODEL")
    {
        s.txOn = ! isUnknown (first);
        s.txModel = s.txOn ? first : std::string();
        if (s.txOn && family == Family::unknown)
            detectFamily (first);

        if (! s.txOn)
        {
            s.batteryBars = s.batteryPercent = s.batteryMinutes = -1;
            s.rfDbm = NAN;
        }
    }
    else if (key == "RX_RF_LVL")
    {
        const int n = toInt (first);
        if (n >= 0) s.rfDbm = (float) (n - 128);
    }
    else if (key == "RSSI" && v.size() >= 2)
    {
        // "< REP 1 RSSI 1 083 >": Antenne + Wert, tatsächlich = Wert - 120 dBm
        const int ant = toInt (v[0]), n = toInt (v[1]);
        if (n >= 0 && (ant == 1 || ant == 2))
        {
            auto& both = antennaRssi.try_emplace (ch, std::array<float, 2> { -120.0f, -120.0f }).first->second;
            both[(size_t) ant - 1] = (float) (n - 120);
            s.rfDbm = std::max (both[0], both[1]);   // stärkere Antenne zählt
        }
    }
    else if (key == "RF_ANTENNA")
    {
        s.antenna = first;
    }
    else if (key == "AUDIO_LVL")
    {
        const int n = toInt (first);
        if (n >= 0) s.audioDb = (float) (n - 50);
    }
    else if (key == "AUDIO_LEVEL_PEAK" || key == "AUDIO_LEVEL_RMS")
    {
        const int n = toInt (first);
        if (n >= 0) s.audioDb = (float) (n - 120);
    }
    else
    {
        if (unknownKeys.size() < 200)
            unknownKeys.insert (key);
        return;
    }

    changed.insert (ch);
}

void ShureParser::handleSample (int, const std::vector<std::string>& v, WirelessChannelState& s)
{
    if (family == Family::adPsm || transmitterMode)
    {
        // In-Ear-Sender: Eingangsmeter. Aufbau nicht veröffentlicht – die ersten beiden
        // Zahlenwerte werden als Pegel links/rechts (−120 wie bei Axient Digital) gelesen.
        std::vector<int> nums;
        for (auto& x : v)
            if (isNumber (x)) nums.push_back (toInt (x));
        if (! nums.empty())
        {
            s.isIemTransmitter = true;
            s.inputLeftDb = (float) (nums[0] - 120);
            s.inputRightDb = (float) ((nums.size() > 1 ? nums[1] : nums[0]) - 120);
        }
        return;
    }

    if (v.size() >= 9)
    {
        // Axient Digital: qual audBitmap audPeak audRms rfAntStats rfBitmapA rfRssiA rfBitmapB rfRssiB
        if (family == Family::unknown) family = Family::axientDigital;
        const int peak = toInt (v[2]), rssiA = toInt (v[6]), rssiB = toInt (v[8]);
        if (peak >= 0) s.audioDb = (float) (peak - 120);
        if (rssiA >= 0 || rssiB >= 0) s.rfDbm = (float) (std::max (rssiA, rssiB) - 120);
        s.antenna = v[4];
        return;
    }

    if (v.size() == 3 && ! isNumber (v[0]))
    {
        // ULX-D / QLX-D: Antennen-LEDs, RF (−128), Audio (−50)
        if (family == Family::unknown) family = Family::ulxd;
        const int rf = toInt (v[1]), audio = toInt (v[2]);
        s.antenna = v[0];
        if (rf >= 0)    s.rfDbm = (float) (rf - 128);
        if (audio >= 0) s.audioDb = (float) (audio - 50);
        return;
    }

    if (v.size() == 3)
    {
        // SLX-D: audPeak audRms rfRssi (jeweils −120)
        if (family == Family::unknown) family = Family::slxd;
        const int peak = toInt (v[0]), rssi = toInt (v[2]);
        if (peak >= 0) s.audioDb = (float) (peak - 120);
        if (rssi >= 0) s.rfDbm = (float) (rssi - 120);
    }
}
