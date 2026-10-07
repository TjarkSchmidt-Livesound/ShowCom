#include "AppState.h"

#include <iterator>

namespace
{
    constexpr int avatarSize = 512;

    juce::File settingsFile()   { return AppState::getAppDataDir().getChildFile ("settings.json"); }
}

AppState::AppState()
{
    getAvatarDir().createDirectory();
    getModelsDir().createDirectory();
}

AppState::~AppState() = default;

juce::File AppState::getAppDataDir()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");   // JUCE liefert hier ~/Library
   #endif
    auto dir = base.getChildFile ("ShowCom");
    dir.createDirectory();
    return dir;
}

juce::File AppState::getAvatarDir()     { return getAppDataDir().getChildFile ("avatars"); }
juce::File AppState::getModelsDir()     { return getAppDataDir().getChildFile ("models"); }

juce::Colour AppState::paletteColour (int index)
{
    static const juce::uint32 palette[] = {
        0xff4fc3f7, 0xffffb74d, 0xff81c784, 0xffe57373, 0xffba68c8,
        0xfffff176, 0xff4db6ac, 0xfff06292, 0xff9575cd, 0xffa1887f
    };
    return juce::Colour (palette[(size_t) juce::jmax (0, index) % std::size (palette)]);
}

//==============================================================================
void AppState::load()
{
    const auto json = juce::JSON::parse (settingsFile());

    if (auto* root = json.getDynamicObject())
    {
        if (auto* s = root->getProperty ("settings").getDynamicObject())
        {
            auto str = [s] (const char* key, const juce::String& def)
            {
                return s->hasProperty (key) ? s->getProperty (key).toString() : def;
            };
            auto flag = [s] (const char* key, bool def)
            {
                return s->hasProperty (key) ? (bool) s->getProperty (key) : def;
            };

            settings.modelPath        = str ("modelPath", {});
            settings.language         = str ("language", "de");
            settings.keywords         = str ("keywords", {});
            settings.audioDeviceState = str ("audioDeviceState", {});
            settings.fuzzyKeywords    = flag ("fuzzyKeywords", true);
            settings.flashAlert       = flag ("flashAlert", true);
            settings.useGpu           = flag ("useGpu", true);
            settings.accurateDecoding = flag ("accurateDecoding", false);
        }

        settings.wirelessDevices.clear();

        if (auto* arr = root->getProperty ("wireless").getArray())
        {
            for (auto& v : *arr)
            {
                WirelessDeviceConfig d;
                d.id          = v["id"].toString();
                d.host        = v["host"].toString();
                d.label       = v["label"].toString();
                d.numChannels = v.hasProperty ("channels") ? (int) v["channels"] : 0;
                const auto type = v["type"].toString();
                d.type = type == "sennheiser-ewdx" ? WirelessDeviceConfig::Type::sennheiserEwdx
                                                   : WirelessDeviceConfig::Type::shure;
                if (d.id.isEmpty())
                    d.id = juce::Uuid().toString();
                settings.wirelessDevices.push_back (d);
            }
        }

        users.clear();

        if (auto* arr = root->getProperty ("users").getArray())
        {
            for (auto& v : *arr)
            {
                UserProfile u;
                u.id          = v["id"].toString();
                u.name        = v["name"].toString();
                u.role        = v["role"].toString();
                u.avatarFile  = v["avatar"].toString();
                u.channel     = v.hasProperty ("channel") ? (int) v["channel"] : -1;
                u.thresholdDb = v.hasProperty ("thresholdDb") ? (float) (double) v["thresholdDb"] : -45.0f;
                u.rfDeviceId  = v["rfDevice"].toString();
                u.rfChannel   = v.hasProperty ("rfChannel") ? (int) v["rfChannel"] : 0;
                u.colour      = v.hasProperty ("colour") ? juce::Colour::fromString (v["colour"].toString())
                                                         : paletteColour ((int) users.size());

                if (u.id.isEmpty())
                    u.id = juce::Uuid().toString();

                users.push_back (u);
            }
        }
    }

    avatarCache.clear();
    sendChangeMessage();
}

void AppState::save() const
{
    auto* s = new juce::DynamicObject();
    s->setProperty ("modelPath",        settings.modelPath);
    s->setProperty ("language",         settings.language);
    s->setProperty ("keywords",         settings.keywords);
    s->setProperty ("audioDeviceState", settings.audioDeviceState);
    s->setProperty ("fuzzyKeywords",    settings.fuzzyKeywords);
    s->setProperty ("flashAlert",       settings.flashAlert);
    s->setProperty ("useGpu",           settings.useGpu);
    s->setProperty ("accurateDecoding", settings.accurateDecoding);

    juce::Array<juce::var> arr;

    for (auto& u : users)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id",          u.id);
        o->setProperty ("name",        u.name);
        o->setProperty ("role",        u.role);
        o->setProperty ("avatar",      u.avatarFile);
        o->setProperty ("channel",     u.channel);
        o->setProperty ("thresholdDb", u.thresholdDb);
        o->setProperty ("colour",      u.colour.toString());
        o->setProperty ("rfDevice",    u.rfDeviceId);
        o->setProperty ("rfChannel",   u.rfChannel);
        arr.add (juce::var (o));
    }

    juce::Array<juce::var> wireless;

    for (auto& d : settings.wirelessDevices)
    {
        if (d.type == WirelessDeviceConfig::Type::simulated)
            continue;

        auto* o = new juce::DynamicObject();
        o->setProperty ("id",       d.id);
        o->setProperty ("type",     d.type == WirelessDeviceConfig::Type::sennheiserEwdx ? "sennheiser-ewdx" : "shure");
        o->setProperty ("host",     d.host);
        o->setProperty ("label",    d.label);
        o->setProperty ("channels", d.numChannels);
        wireless.add (juce::var (o));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("settings", juce::var (s));
    root->setProperty ("users", arr);
    root->setProperty ("wireless", wireless);

    // Erst in Temp-Datei schreiben, dann ersetzen: kein kaputtes settings.json bei Absturz
    juce::TemporaryFile tmp (settingsFile());
    if (tmp.getFile().replaceWithText (juce::JSON::toString (juce::var (root))))
        tmp.overwriteTargetFileWithTemporary();
}

void AppState::changed()
{
    save();
    sendChangeMessage();
}

//==============================================================================
const UserProfile* AppState::findUser (const juce::String& id) const
{
    for (auto& u : users)
        if (u.id == id)
            return &u;

    return nullptr;
}

const UserProfile* AppState::findUserForChannel (int channel, const juce::String& excludeId) const
{
    if (channel < 0)
        return nullptr;

    for (auto& u : users)
        if (u.channel == channel && u.id != excludeId)
            return &u;

    return nullptr;
}

juce::String AppState::addUser()
{
    UserProfile u;
    u.id = juce::Uuid().toString();
    u.name = "Person " + juce::String ((int) users.size() + 1);
    u.colour = paletteColour ((int) users.size());

    // ersten freien Eingang vorschlagen
    for (int ch = 0; ch < 64; ++ch)
    {
        if (findUserForChannel (ch) == nullptr)
        {
            u.channel = ch;
            break;
        }
    }

    users.push_back (u);
    changed();
    return u.id;
}

void AppState::updateUser (const UserProfile& user)
{
    for (auto& u : users)
    {
        if (u.id == user.id)
        {
            u = user;
            changed();
            return;
        }
    }
}

void AppState::removeUser (const juce::String& id)
{
    for (auto it = users.begin(); it != users.end(); ++it)
    {
        if (it->id == id)
        {
            if (it->avatarFile.isNotEmpty())
                getAvatarDir().getChildFile (it->avatarFile).deleteFile();

            avatarCache.erase (id);
            users.erase (it);
            changed();
            return;
        }
    }
}

void AppState::moveUser (const juce::String& id, int delta)
{
    for (size_t i = 0; i < users.size(); ++i)
    {
        if (users[i].id == id)
        {
            const auto j = (size_t) juce::jlimit (0, (int) users.size() - 1, (int) i + delta);
            if (j != i)
            {
                std::swap (users[i], users[j]);
                changed();
            }
            return;
        }
    }
}

//==============================================================================
bool AppState::setAvatarFromFile (const juce::String& userId, const juce::File& imageFile)
{
    auto* existing = findUser (userId);
    if (existing == nullptr)
        return false;

    auto img = juce::ImageFileFormat::loadFrom (imageFile);
    if (! img.isValid())
        return false;

    // Mittig quadratisch zuschneiden und skalieren
    const int side = juce::jmin (img.getWidth(), img.getHeight());
    auto square = img.getClippedImage ({ (img.getWidth() - side) / 2, (img.getHeight() - side) / 2, side, side });
    auto scaled = square.rescaled (avatarSize, avatarSize, juce::Graphics::highResamplingQuality);

    // Dateiname mit Zeitstempel, damit kein altes Bild aus irgendeinem Cache auftaucht
    const auto fileName = userId + "_" + juce::String (juce::Time::currentTimeMillis()) + ".png";
    auto target = getAvatarDir().getChildFile (fileName);

    {
        juce::FileOutputStream out (target);
        if (! out.openedOk())
            return false;

        juce::PNGImageFormat png;
        if (! png.writeImageToStream (scaled, out))
            return false;
    }

    auto user = *existing;
    if (user.avatarFile.isNotEmpty())
        getAvatarDir().getChildFile (user.avatarFile).deleteFile();

    user.avatarFile = fileName;
    avatarCache[userId] = scaled;
    updateUser (user);
    return true;
}

void AppState::clearAvatar (const juce::String& userId)
{
    if (auto* existing = findUser (userId))
    {
        auto user = *existing;
        if (user.avatarFile.isNotEmpty())
            getAvatarDir().getChildFile (user.avatarFile).deleteFile();

        user.avatarFile = {};
        avatarCache.erase (userId);
        updateUser (user);
    }
}

juce::Image AppState::getAvatar (const juce::String& userId) const
{
    if (auto it = avatarCache.find (userId); it != avatarCache.end())
        return it->second;

    juce::Image img;

    if (auto* u = findUser (userId); u != nullptr && u->avatarFile.isNotEmpty())
        img = juce::ImageFileFormat::loadFrom (getAvatarDir().getChildFile (u->avatarFile));

    avatarCache[userId] = img;   // auch "kein Bild" cachen
    return img;
}

//==============================================================================
void AppState::updateSettings (const AppSettings& s)
{
    settings = s;
    changed();
}

void AppState::setAudioDeviceState (const juce::String& xml)
{
    if (settings.audioDeviceState != xml)
    {
        settings.audioDeviceState = xml;
        save();
    }
}
