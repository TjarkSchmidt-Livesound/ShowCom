#include "SpeechSegmenter.h"

#include <algorithm>
#include <cmath>

SpeechSegmenter::SpeechSegmenter()
{
    prepare (config);
}

void SpeechSegmenter::prepare (const Config& c)
{
    config = c;

    auto framesFor = [this] (double ms) { return std::max (1, (int) std::lround (ms / config.frameMs)); };

    frameSize       = std::max (16, (int) std::lround (config.sampleRate * config.frameMs / 1000.0));
    preRollFrames   = framesFor (config.preRollMs);
    hangoverFrames  = framesFor (config.hangoverMs);
    minSpeechFrames = framesFor (config.minSpeechMs);
    maxFrames       = framesFor (config.maxSegmentSec * 1000.0);

    frame.assign ((size_t) frameSize, 0.0f);
    current.reserve ((size_t) frameSize * (size_t) maxFrames);
    reset();
}

void SpeechSegmenter::reset()
{
    frameFill = 0;
    preRoll.clear();
    current.clear();
    currentStart = 0;
    streamPos = 0;
    speechFrames = silentFrames = segmentFrames = 0;
    inSpeech = false;
    lastFrameDb = -120.0f;
}

void SpeechSegmenter::process (const float* samples, int numSamples, const Callback& onSegment)
{
    int i = 0;

    while (i < numSamples)
    {
        const int n = std::min (numSamples - i, frameSize - frameFill);
        std::copy (samples + i, samples + i + n, frame.begin() + frameFill);
        frameFill += n;
        i += n;

        if (frameFill == frameSize)
        {
            processFrame (onSegment);
            frameFill = 0;
        }
    }
}

void SpeechSegmenter::processFrame (const Callback& onSegment)
{
    double sum = 0.0;
    for (float s : frame)
        sum += (double) s * s;

    const double rms = std::sqrt (sum / frameSize);
    lastFrameDb = rms > 1.0e-6 ? (float) (20.0 * std::log10 (rms)) : -120.0f;
    const bool loud = lastFrameDb >= config.thresholdDb;

    const int64_t frameStart = streamPos;
    streamPos += frameSize;

    if (! inSpeech)
    {
        if (loud)
        {
            ++utteranceId;
            inSpeech = true;
            speechFrames = 1;
            silentFrames = 0;
            segmentFrames = (int) preRoll.size() + 1;
            current.clear();
            currentStart = frameStart - (int64_t) preRoll.size() * frameSize;

            for (auto& f : preRoll)
                current.insert (current.end(), f.begin(), f.end());

            preRoll.clear();
            current.insert (current.end(), frame.begin(), frame.end());
        }
        else
        {
            preRoll.push_back (frame);
            if ((int) preRoll.size() > preRollFrames)
                preRoll.pop_front();
        }

        return;
    }

    current.insert (current.end(), frame.begin(), frame.end());
    ++segmentFrames;

    if (loud) { ++speechFrames; silentFrames = 0; }
    else      { ++silentFrames; }

    if (silentFrames >= hangoverFrames)
    {
        // Nachlauf größtenteils abschneiden, aber ~250 ms Ausklang behalten (leise Endsilben!)
        const int trimFrames = std::max (0, silentFrames - std::max (1, hangoverFrames * 3 / 8));
        current.resize (current.size() - (size_t) trimFrames * (size_t) frameSize);
        emit (onSegment);
        inSpeech = false;
        return;
    }

    if (segmentFrames >= maxFrames)
    {
        // Langer Monolog: Abschnitt schließen und nahtlos einen neuen beginnen
        emit (onSegment);
        ++utteranceId;
        inSpeech = true;
        speechFrames = 0;
        silentFrames = 0;
        segmentFrames = 0;
        current.clear();
        currentStart = streamPos;
    }
}

void SpeechSegmenter::emit (const Callback& onSegment)
{
    if (speechFrames >= minSpeechFrames && ! current.empty())
    {
        Segment s;
        s.audio = current;   // Kopie, damit 'current' seine Kapazität behält
        s.startSample = currentStart;
        s.utteranceId = utteranceId;
        onSegment (std::move (s));
    }

    current.clear();
    speechFrames = silentFrames = segmentFrames = 0;
}

void SpeechSegmenter::flush (const Callback& onSegment)
{
    if (inSpeech)
    {
        if (frameFill > 0)
            current.insert (current.end(), frame.begin(), frame.begin() + frameFill);

        emit (onSegment);
    }

    reset();
}
