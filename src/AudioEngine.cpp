#include "AudioEngine.h"

AudioEngine::AudioEngine()
    : juce::Thread ("ShowCom Audio Processing")
{
    for (auto& p : peaks)     p.store (0.0f);
    for (auto& l : levelsDb)  l.store (-120.0f);
    for (auto& s : speaking)  s.store (false);
    smoothedLevel.fill (0.0f);
    scratch.resize (8192);
}

AudioEngine::~AudioEngine()
{
    stopSession();
    deviceManager.removeAudioCallback (this);
    stopThread (3000);
    deviceManager.closeAudioDevice();
}

juce::String AudioEngine::initialise (const juce::String& savedStateXml)
{
    std::unique_ptr<juce::XmlElement> xml;
    if (savedStateXml.isNotEmpty())
        xml = juce::parseXML (savedStateXml);

    // Nur Eingänge: Wir geben nie etwas aus, damit nichts versehentlich auf der PA landet.
    auto error = deviceManager.initialise (maxChannels, 0, xml.get(), true);

    deviceManager.addAudioCallback (this);
    startThread (juce::Thread::Priority::high);
    return error;
}

juce::String AudioEngine::createStateXml() const
{
    if (auto xml = deviceManager.createStateXml())
        return xml->toString();

    return {};
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const juce::ScopedLock sl (processLock);

    const double sr = device->getCurrentSampleRate();
    const auto active = device->getActiveInputChannels();
    const int capacity = juce::nextPowerOfTwo ((int) (sr * 2.0));   // ~2 s Puffer

    int n = 0;
    for (int ch = 0; ch < maxChannels; ++ch)
    {
        if (! active[ch])
            continue;

        auto& f = fifos[(size_t) ch];
        if ((int) f.buffer.size() != capacity)
        {
            f.buffer.assign ((size_t) capacity, 0.0f);
            f.fifo.setTotalSize (capacity);
        }
        f.fifo.reset();

        activeInputMap[(size_t) n++] = ch;
    }

    numActiveInputs.store (n);
    deviceSampleRate.store (sr);
}

void AudioEngine::audioDeviceStopped()
{
    const juce::ScopedLock sl (processLock);
    numActiveInputs.store (0);
}

void AudioEngine::audioDeviceError (const juce::String& errorMessage)
{
    DBG ("Audio device error: " << errorMessage);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* input, int numInputChannels,
                                                    float* const* output, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const int n = juce::jmin (numInputChannels, numActiveInputs.load (std::memory_order_acquire));

    for (int i = 0; i < n; ++i)
    {
        const float* data = input[i];
        if (data == nullptr)
            continue;

        const int ch = activeInputMap[(size_t) i];
        auto& f = fifos[(size_t) ch];

        float peak = 0.0f;
        for (int s = 0; s < numSamples; ++s)
            peak = juce::jmax (peak, std::abs (data[s]));

        if (peak > peaks[(size_t) ch].load (std::memory_order_relaxed))
            peaks[(size_t) ch].store (peak, std::memory_order_relaxed);

        const auto scope = f.fifo.write (numSamples);

        if (scope.blockSize1 > 0)
            std::copy (data, data + scope.blockSize1, f.buffer.data() + scope.startIndex1);
        if (scope.blockSize2 > 0)
            std::copy (data + scope.blockSize1, data + scope.blockSize1 + scope.blockSize2, f.buffer.data() + scope.startIndex2);

        if (scope.blockSize1 + scope.blockSize2 < numSamples)
            overflowCount.fetch_add (1, std::memory_order_relaxed);
    }

    // Ausgänge immer stumm
    for (int i = 0; i < numOutputChannels; ++i)
        if (output[i] != nullptr)
            juce::FloatVectorOperations::clear (output[i], numSamples);
}

//==============================================================================
void AudioEngine::setAssignments (const std::vector<Assignment>& newAssignments)
{
    {
        const juce::ScopedLock sl (processLock);
        pendingAssignments = newAssignments;
        assignmentsDirty = true;
    }

    // Zugewiesene Eingänge am Interface aktivieren (nur wenn nötig, ein Neustart des Treibers knackt evtl.)
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        auto wanted = setup.useDefaultInputChannels ? device->getActiveInputChannels() : setup.inputChannels;
        const int available = device->getInputChannelNames().size();
        bool changed = false;

        for (auto& a : newAssignments)
        {
            if (a.channel >= 0 && a.channel < available && ! wanted[a.channel])
            {
                wanted.setBit (a.channel);
                changed = true;
            }
        }

        if (changed)
        {
            setup.inputChannels = wanted;
            setup.useDefaultInputChannels = false;
            deviceManager.setAudioDeviceSetup (setup, true);
        }
    }
}

//==============================================================================
double AudioEngine::sessionSecondsNow() const
{
    if (! sessionRunning.load())
        return 0.0;

    return (double) (juce::Time::currentTimeMillis() - sessionStartMs.load()) / 1000.0;
}

double AudioEngine::getSessionSeconds() const
{
    return sessionSecondsNow();
}

bool AudioEngine::startSession (const juce::File& folder, bool shouldRecordAudio, juce::String& errorOut)
{
    if (sessionRunning.load())
        return true;

    if (! folder.createDirectory())
    {
        errorOut = "Ordner konnte nicht angelegt werden: " + folder.getFullPathName();
        return false;
    }

    const juce::ScopedLock sl (processLock);

    sessionFolder = folder;
    recordAudio = shouldRecordAudio;
    filePart = 1;
    sessionStartTime = juce::Time::getCurrentTime();
    sessionStartMs.store (sessionStartTime.toMilliseconds());
    sessionRunning.store (true);

    for (auto& [ch, state] : states)
    {
        state->segmenter.reset();
        state->streamBaseSec = 0.0;
        state->lastFlushSec = 0.0;

        if (recordAudio)
            openWriter (*state, filePart);
    }

    return true;
}

void AudioEngine::stopSession()
{
    if (! sessionRunning.load())
        return;

    const juce::ScopedLock sl (processLock);

    for (auto& [ch, state] : states)
    {
        auto* st = state.get();
        st->segmenter.flush ([this, st] (SpeechSegmenter::Segment&& s) { emitSegment (*st, std::move (s)); });
        st->writer.reset();   // schließt die Datei und schreibt den WAV-Header
    }

    sessionRunning.store (false);
}

//==============================================================================
void AudioEngine::openWriter (ChannelState& state, int part)
{
    state.writer.reset();

    if (preparedSampleRate <= 0.0)
        return;

    auto stem = juce::String::formatted ("%02d_", state.assignment.channel + 1)
              + juce::File::createLegalFileName (state.assignment.fileStem);
    if (part > 1)
        stem << "_teil" << part;

    const auto file = sessionFolder.getChildFile (stem + ".wav").getNonexistentSibling();

    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return;

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (preparedSampleRate)
                             .withNumChannels (1)
                             .withBitsPerSample (24);

    state.writer = wav.createWriterFor (stream, options);
}

void AudioEngine::prepareState (ChannelState& state, double baseSec)
{
    SpeechSegmenter::Config cfg;
    cfg.sampleRate = preparedSampleRate > 0.0 ? preparedSampleRate : 48000.0;
    cfg.thresholdDb = state.assignment.thresholdDb;
    state.segmenter.prepare (cfg);
    state.streamBaseSec = baseSec;
}

void AudioEngine::rebuildChannelStates()
{
    std::map<int, std::unique_ptr<ChannelState>> next;

    for (auto& a : pendingAssignments)
    {
        if (a.channel < 0 || a.channel >= maxChannels || next.count (a.channel) > 0)
            continue;

        auto it = states.find (a.channel);

        if (it != states.end() && it->second->assignment.userId == a.userId)
        {
            auto st = std::move (it->second);
            states.erase (it);
            st->assignment = a;
            st->segmenter.setThresholdDb (a.thresholdDb);
            next[a.channel] = std::move (st);
            continue;
        }

        auto st = std::make_unique<ChannelState>();
        st->assignment = a;
        prepareState (*st, sessionSecondsNow());

        if (sessionRunning.load() && recordAudio)
            openWriter (*st, filePart);

        next[a.channel] = std::move (st);
    }

    // Was übrig bleibt, wurde entfernt oder umgehängt
    for (auto& [ch, old] : states)
    {
        auto* st = old.get();
        st->segmenter.flush ([this, st] (SpeechSegmenter::Segment&& s) { emitSegment (*st, std::move (s)); });
        st->writer.reset();
    }

    states = std::move (next);
    assignmentsDirty = false;
}

void AudioEngine::emitSegment (ChannelState& state, SpeechSegmenter::Segment&& seg)
{
    if (! sessionRunning.load() || ! onSegment || preparedSampleRate <= 0.0)
        return;

    Segment out;
    out.userId = state.assignment.userId;
    out.channel = state.assignment.channel;
    out.sampleRate = preparedSampleRate;
    out.sessionOffsetSec = state.streamBaseSec + (double) seg.startSample / preparedSampleRate;
    out.wallClock = sessionStartTime + juce::RelativeTime::seconds (out.sessionOffsetSec);
    out.audio = std::move (seg.audio);

    onSegment (std::move (out));
}

//==============================================================================
void AudioEngine::run()
{
    while (! threadShouldExit())
    {
        wait (10);

        const juce::ScopedLock sl (processLock);

        const double sr = deviceSampleRate.load();
        const int numActive = numActiveInputs.load();

        if (sr > 0.0 && std::abs (sr - preparedSampleRate) > 0.5)
        {
            // Samplerate geändert: laufende Abschnitte abschließen, neu vorbereiten,
            // ggf. neue WAV-Datei beginnen (eine WAV kann nur eine Samplerate haben).
            for (auto& [ch, state] : states)
            {
                auto* st = state.get();
                st->segmenter.flush ([this, st] (SpeechSegmenter::Segment&& s) { emitSegment (*st, std::move (s)); });
            }

            const bool hadRate = preparedSampleRate > 0.0;
            preparedSampleRate = sr;

            if (sessionRunning.load() && recordAudio && hadRate)
                ++filePart;

            for (auto& [ch, state] : states)
            {
                prepareState (*state, sessionSecondsNow());
                if (sessionRunning.load() && recordAudio)
                    openWriter (*state, filePart);
            }
        }

        if (assignmentsDirty)
            rebuildChannelStates();

        const double now = sessionSecondsNow();
        std::array<bool, maxChannels> seen {};

        for (int i = 0; i < numActive; ++i)
        {
            const int ch = activeInputMap[(size_t) i];
            seen[(size_t) ch] = true;
            auto& f = fifos[(size_t) ch];

            auto it = states.find (ch);
            ChannelState* st = it != states.end() ? it->second.get() : nullptr;

            for (int ready = f.fifo.getNumReady(); ready > 0; ready = f.fifo.getNumReady())
            {
                const int num = juce::jmin (ready, (int) scratch.size());
                {
                    const auto scope = f.fifo.read (num);
                    if (scope.blockSize1 > 0)
                        std::copy (f.buffer.data() + scope.startIndex1, f.buffer.data() + scope.startIndex1 + scope.blockSize1, scratch.data());
                    if (scope.blockSize2 > 0)
                        std::copy (f.buffer.data() + scope.startIndex2, f.buffer.data() + scope.startIndex2 + scope.blockSize2, scratch.data() + scope.blockSize1);
                }

                if (st == nullptr)
                    continue;

                st->segmenter.process (scratch.data(), num,
                                       [this, st] (SpeechSegmenter::Segment&& s) { emitSegment (*st, std::move (s)); });

                if (st->writer != nullptr)
                {
                    const float* chans[] = { scratch.data() };
                    st->writer->writeFromFloatArrays (chans, 1, num);
                }
            }

            if (st != nullptr && st->writer != nullptr && now - st->lastFlushSec > 5.0)
            {
                st->writer->flush();   // Header aktualisieren: Datei bleibt bei Absturz lesbar
                st->lastFlushSec = now;
            }

            speaking[(size_t) ch].store (st != nullptr && st->segmenter.isInSpeech());
        }

        // Pegelanzeige: Spitzenwert mit Abfall (~40 dB/s)
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            const float peak = seen[(size_t) ch] ? peaks[(size_t) ch].exchange (0.0f) : 0.0f;
            auto& lvl = smoothedLevel[(size_t) ch];
            lvl = juce::jmax (peak, lvl * 0.955f);
            levelsDb[(size_t) ch].store (juce::Decibels::gainToDecibels (lvl, -120.0f));

            if (! seen[(size_t) ch])
                speaking[(size_t) ch].store (false);
        }
    }
}

//==============================================================================
float AudioEngine::getLevelDb (int channel) const
{
    if (channel < 0 || channel >= maxChannels)
        return -120.0f;

    return levelsDb[(size_t) channel].load();
}

bool AudioEngine::isSpeaking (int channel) const
{
    if (channel < 0 || channel >= maxChannels)
        return false;

    return speaking[(size_t) channel].load();
}

juce::StringArray AudioEngine::getInputChannelNames() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return d->getInputChannelNames();

    return {};
}

bool AudioEngine::isChannelActive (int channel) const
{
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return d->getActiveInputChannels()[channel];

    return false;
}

juce::String AudioEngine::getDeviceDescription() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return d->getTypeName() + ": " + d->getName() + juce::String::fromUTF8 ("  ·  ")
             + juce::String (d->getCurrentSampleRate() / 1000.0, 1) + " kHz";

    return "Kein Audio-Interface";
}
