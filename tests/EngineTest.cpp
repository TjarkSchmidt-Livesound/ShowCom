// Integrationstest der Audio-Engine mit einem simulierten 8-Kanal-Interface.
// Prüft Kanal-Zuordnung, FIFO, Sprach-Schnitt, Zeitstempel und WAV-Mitschnitt.

#include "AudioEngine.h"
#include <cstdio>

namespace
{
    constexpr double fakeRate = 48000.0;
    constexpr int fakeBlock = 256;
    constexpr int fakeInputs = 8;
    constexpr int talkChannel = 2;   // "Eingang 3"

    // Pegelmuster auf Eingang 3: (Startzeit s, Dauer s)
    const std::vector<std::pair<double, double>> bursts { { 1.0, 1.5 }, { 4.0, 1.0 } };

    class FakeDevice : public juce::AudioIODevice, private juce::Thread
    {
    public:
        FakeDevice() : AudioIODevice ("Fake 8in", "Fake"), Thread ("fake audio") {}
        ~FakeDevice() override { close(); }

        juce::StringArray getOutputChannelNames() override { return {}; }
        juce::StringArray getInputChannelNames() override
        {
            juce::StringArray s;
            for (int i = 0; i < fakeInputs; ++i) s.add ("In " + juce::String (i + 1));
            return s;
        }
        juce::Array<double> getAvailableSampleRates() override  { return { fakeRate }; }
        juce::Array<int> getAvailableBufferSizes() override     { return { fakeBlock }; }
        int getDefaultBufferSize() override                     { return fakeBlock; }

        juce::String open (const juce::BigInteger& ins, const juce::BigInteger&, double, int) override
        {
            active = ins;
            active.setRange (fakeInputs, 64, false);
            opened = true;
            return {};
        }
        void close() override                     { stop(); opened = false; }
        bool isOpen() override                    { return opened; }
        void start (juce::AudioIODeviceCallback* cb) override
        {
            callback = cb;
            callback->audioDeviceAboutToStart (this);
            startThread (juce::Thread::Priority::highest);
        }
        void stop() override
        {
            stopThread (2000);
            if (callback != nullptr) { callback->audioDeviceStopped(); callback = nullptr; }
        }
        bool isPlaying() override                 { return callback != nullptr; }
        juce::String getLastError() override      { return {}; }
        int getCurrentBufferSizeSamples() override{ return fakeBlock; }
        double getCurrentSampleRate() override    { return fakeRate; }
        int getCurrentBitDepth() override         { return 24; }
        juce::BigInteger getActiveOutputChannels() const override { return {}; }
        juce::BigInteger getActiveInputChannels() const override  { return active; }
        int getOutputLatencyInSamples() override  { return 0; }
        int getInputLatencyInSamples() override   { return 0; }

    private:
        void run() override
        {
            juce::AudioBuffer<float> buf (fakeInputs, fakeBlock);
            juce::int64 pos = 0;
            const double start = juce::Time::getMillisecondCounterHiRes();

            while (! threadShouldExit())
            {
                buf.clear();
                for (int s = 0; s < fakeBlock; ++s)
                {
                    const double t = (double) (pos + s) / fakeRate;
                    float v = 0.00005f * std::sin ((float) t * 1234.0f);   // Rauschteppich ~ -86 dB
                    for (auto& [b0, len] : bursts)
                        if (t >= b0 && t < b0 + len)
                            v = 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * 300.0f * (float) t);
                    buf.setSample (talkChannel, s, v);
                    buf.setSample (0, s, 0.3f * std::sin ((float) t * 2000.0f));   // nicht zugewiesen
                }

                // Nur aktive Kanäle übergeben, wie ein echter Treiber
                std::vector<const float*> ins;
                for (int ch = 0; ch < fakeInputs; ++ch)
                    if (active[ch]) ins.push_back (buf.getReadPointer (ch));

                callback->audioDeviceIOCallbackWithContext (ins.data(), (int) ins.size(), nullptr, 0, fakeBlock, {});
                pos += fakeBlock;

                // Echtzeit simulieren
                const double due = start + 1000.0 * (double) pos / fakeRate;
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (due > now) juce::Thread::sleep ((int) (due - now));
            }
        }

        juce::AudioIODeviceCallback* callback = nullptr;
        juce::BigInteger active;
        bool opened = false;
    };

    class FakeType : public juce::AudioIODeviceType
    {
    public:
        FakeType() : AudioIODeviceType ("Fake") {}
        void scanForDevices() override {}
        juce::StringArray getDeviceNames (bool wantInputs) const override { return wantInputs ? juce::StringArray ("Fake 8in") : juce::StringArray(); }
        int getDefaultDeviceIndex (bool) const override { return 0; }
        int getIndexOfDevice (juce::AudioIODevice*, bool asInput) const override { return asInput ? 0 : -1; }
        bool hasSeparateInputsAndOutputs() const override { return true; }
        juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override { return new FakeDevice(); }
    };

    int failures = 0;
    void check (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok) ++failures;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    AudioEngine engine;
    engine.getDeviceManager().addAudioDeviceType (std::make_unique<FakeType>());
    engine.getDeviceManager().setCurrentAudioDeviceType ("Fake", true);
    engine.initialise ({});

    // Nur Eingang 1 aktiv: die Engine muss Eingang 3 für die Zuweisung selbst einschalten
    {
        auto setup = engine.getDeviceManager().getAudioDeviceSetup();
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setBit (0);
        engine.getDeviceManager().setAudioDeviceSetup (setup, true);
    }

    std::vector<AudioEngine::Segment> segments;
    juce::CriticalSection segLock;
    engine.onSegment = [&] (AudioEngine::Segment&& s) { const juce::ScopedLock sl (segLock); segments.push_back (std::move (s)); };

    AudioEngine::Assignment a;
    a.userId = "anna";
    a.fileStem = "Anna";
    a.channel = talkChannel;
    a.thresholdDb = -40.0f;
    engine.setAssignments ({ a });

    check (engine.isChannelActive (talkChannel), "zugewiesener Eingang wurde automatisch aktiviert");

    // Session starten, sobald das Gerät läuft (Zeitbasis der Bursts = Gerätestart; Toleranz unten)
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tbs_engine_test");
    folder.deleteRecursively();
    juce::String err;
    check (engine.startSession (folder, true, err), "Session startet");

    bool sawSpeaking = false;
    for (int i = 0; i < 65; ++i)   // 6.5 s
    {
        juce::Thread::sleep (100);
        sawSpeaking = sawSpeaking || engine.isSpeaking (talkChannel);
    }

    const float level = engine.getLevelDb (0);
    engine.stopSession();

    check (sawSpeaking, "Sprech-Anzeige wurde aktiv");
    check (level > -20.0f, "Pegelanzeige für nicht zugewiesenen Eingang funktioniert");
    check (engine.getOverflowCount() == 0, "keine FIFO-Überläufe");

    {
        const juce::ScopedLock sl (segLock);
        check (segments.size() == 2, "genau zwei Sprach-Abschnitte erkannt");

        if (segments.size() == 2)
        {
            for (size_t i = 0; i < 2; ++i)
            {
                const auto& s = segments[i];
                const double len = (double) s.audio.size() / s.sampleRate;
                std::printf ("      Abschnitt %d: Start %.2f s, Länge %.2f s, user=%s\n",
                             (int) i + 1, s.sessionOffsetSec, len, s.userId.toRawUTF8());
            }

            // Session-Start liegt kurz nach Gerätestart -> Offsets etwas kleiner als die Burst-Zeiten
            check (segments[0].userId == "anna", "Abschnitt gehört zur richtigen Person");
            check (std::abs (segments[0].audio.size() / 48000.0 - 1.95) < 0.25, "Länge Abschnitt 1 plausibel (Burst + Pre-Roll + Ausklang)");
            check (std::abs ((segments[1].sessionOffsetSec - segments[0].sessionOffsetSec) - 3.0) < 0.05, "Abstand der Abschnitte stimmt (3,0 s)");
        }
    }

    auto wavs = folder.findChildFiles (juce::File::findFiles, false, "*.wav");
    check (wavs.size() == 1, "eine WAV-Datei geschrieben");

    if (wavs.size() == 1)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (wavs[0].createInputStream().release(), true));
        check (r != nullptr && r->numChannels == 1 && r->bitsPerSample == 24, "WAV ist mono, 24 Bit");
        if (r != nullptr)
        {
            const double secs = (double) r->lengthInSamples / r->sampleRate;
            std::printf ("      %s: %.2f s\n", wavs[0].getFileName().toRawUTF8(), secs);
            check (secs > 6.0 && secs < 7.0, "WAV-Länge entspricht der Session-Dauer");

            // Im WAV muss bei Abschnitt 2 tatsächlich Signal liegen
            juce::AudioBuffer<float> b (1, 4800);
            const double at = segments.size() == 2 ? segments[1].sessionOffsetSec + 0.5 : 4.0;
            r->read (&b, 0, 4800, (juce::int64) (at * r->sampleRate), true, false);
            check (b.getMagnitude (0, 0, 4800) > 0.2f, "Zeitstempel zeigt auf die richtige Stelle in der WAV");
        }
    }

    folder.deleteRecursively();
    std::printf (failures == 0 ? "Engine test passed.\n" : "Engine test FAILED.\n");
    return failures == 0 ? 0 : 1;
}
