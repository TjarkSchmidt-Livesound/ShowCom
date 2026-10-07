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
    setListening (false);
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
double AudioEngine::listeningSecondsNow() const
{
    if (! listening.load())
        return 0.0;

    return (double) (juce::Time::currentTimeMillis() - listenStartMs.load()) / 1000.0;
}

double AudioEngine::getListeningSeconds() const
{
    return listeningSecondsNow();
}

void AudioEngine::setListening (bool shouldListen)
{
    if (shouldListen == listening.load())
        return;

    const juce::ScopedLock sl (processLock);

    if (shouldListen)
    {
        listenStartTime = juce::Time::getCurrentTime();
        listenStartMs.store (listenStartTime.toMilliseconds());

        for (auto& [ch, state] : states)
        {
            state->segmenter.reset();
            state->streamBaseSec = 0.0;
        }

        listening.store (true);
    }
    else
    {
        // Laufende Abschnitte noch abschließen, damit nichts verloren geht
        for (auto& [ch, state] : states)
        {
            auto* st = state.get();
            st->segmenter.flush ([this, st] (SpeechSegmenter::Segment&& seg) { emitSegment (*st, std::move (seg)); });
        }

        listening.store (false);
    }
}

//==============================================================================
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
        prepareState (*st, listeningSecondsNow());
        next[a.channel] = std::move (st);
    }

    // Was übrig bleibt, wurde entfernt oder umgehängt
    for (auto& [ch, old] : states)
    {
        auto* st = old.get();
        st->segmenter.flush ([this, st] (SpeechSegmenter::Segment&& s) { emitSegment (*st, std::move (s)); });
    }

    states = std::move (next);
    assignmentsDirty = false;
}

void AudioEngine::maybeEmitLive (ChannelState& state)
{
    const float interval = liveIntervalSec.load();
    auto& seg = state.segmenter;

    if (interval <= 0.0f || ! listening.load() || ! seg.isInSpeech() || preparedSampleRate <= 0.0)
        return;

    const auto& audio = seg.getCurrentAudio();
    const auto step = (size_t) (interval * preparedSampleRate);

    if (state.liveUtterance != seg.getUtteranceId())
    {
        state.liveUtterance = seg.getUtteranceId();
        state.liveSamples = 0;
    }

    // Erst ab ~0,8 s Sprache, danach im eingestellten Takt
    if (audio.size() < step || audio.size() < state.liveSamples + step)
        return;

    state.liveSamples = audio.size();

    SpeechSegmenter::Segment s;
    s.audio = audio;
    s.startSample = seg.getCurrentStart();
    s.utteranceId = seg.getUtteranceId();
    emitSegment (state, std::move (s), true);
}

void AudioEngine::emitSegment (ChannelState& state, SpeechSegmenter::Segment&& seg, bool partial)
{
    if (! listening.load() || ! onSegment || preparedSampleRate <= 0.0)
        return;

    Segment out;
    out.userId = state.assignment.userId;
    out.channel = state.assignment.channel;
    out.sampleRate = preparedSampleRate;
    out.sessionOffsetSec = state.streamBaseSec + (double) seg.startSample / preparedSampleRate;
    out.wallClock = listenStartTime + juce::RelativeTime::seconds (out.sessionOffsetSec);
    out.partial = partial;
    out.utteranceId = seg.utteranceId;
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
            // Samplerate geändert: laufende Abschnitte abschließen und neu vorbereiten
            for (auto& [ch, state] : states)
            {
                auto* st = state.get();
                st->segmenter.flush ([this, st] (SpeechSegmenter::Segment&& s) { emitSegment (*st, std::move (s)); });
            }

            preparedSampleRate = sr;

            for (auto& [ch, state] : states)
                prepareState (*state, listeningSecondsNow());
        }

        if (assignmentsDirty)
            rebuildChannelStates();

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
            }

            if (st != nullptr)
                maybeEmitLive (*st);

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
