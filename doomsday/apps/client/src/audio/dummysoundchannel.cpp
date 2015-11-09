/** @file dummysoundchannel.cpp  Dummy audio::Channel for simulating sound playback.
 *
 * @authors Copyright © 2003-2015 Jaakko Keränen <jaakko.keranen@iki.fi>
 * @authors Copyright © 2006-2015 Daniel Swanson <danij@dengine.net>
 *
 * @par License
 * LGPL: http://www.gnu.org/licenses/lgpl.html
 *
 * <small>This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser
 * General Public License for more details. You should have received a copy of
 * the GNU Lesser General Public License along with this program; if not, see:
 * http://www.gnu.org/licenses</small>
 */

#include "audio/dummysoundchannel.h"

#include "audio/sound.h"
#include "clientapp.h"
#include <de/Log>
#include <de/Observers>
#include <de/timer.h>
#include <QList>
#include <QtAlgorithms>

using namespace de;

namespace audio {

DENG2_PIMPL_NOREF(DummySoundChannel)
, DENG2_OBSERVES(System, FrameEnds)
{
    bool noUpdate    = false;  ///< @c true if skipping updates (when stopped, before deletion).

    PlayingMode playingMode = { NotPlaying };
    dint startTime   = 0;      ///< When playback last started (Ticks).
    duint endTime    = 0;      ///< When playback last ended if not looping (Milliseconds).

    Positioning positioning = { StereoPositioning };
    dfloat frequency = 1;      ///< {0..1} Frequency/pitch adjustment factor.
    dfloat volume    = 1;      ///< {0..1} Volume adjustment factor.

    ::audio::Sound *sound = nullptr;    ///< Logical Sound currently being played (if any, not owned).

    struct Buffer
    {
        sfxsample_t *data   = nullptr;
        bool needReloadData = false;

        dint sampleBytes = 1;      ///< Bytes per sample (1 or 2).
        dint sampleRate  = 11025;  ///< Number of samples per second.

        duint milliseconds(dfloat frequency) const
        {
            if(!data) return 0;
            return 1000 * data->numSamples / sampleRate * frequency;
        }

        void unload()
        {
            data = nullptr;
            needReloadData = false;
        }

        void load(sfxsample_t *sample)
        {
            data = sample;
            needReloadData = false;
            // Now the buffer is ready for playing.
        }

        void reloadIfNeeded()
        {
            if(needReloadData)
            {
                DENG2_ASSERT(data);
                load(data);
            }
        }
    } buffer;

    Instance()
    {
        // We want notification when the frame ends in order to flush deferred property writes.
        ClientApp::audioSystem().audienceForFrameEnds() += this;
    }

    ~Instance()
    {
        // Cancel frame notifications.
        ClientApp::audioSystem().audienceForFrameEnds() -= this;
    }

    inline ::audio::Sound &getSound() const
    {
        DENG2_ASSERT(sound != nullptr);
        return *sound;
    }

    /**
     * Writes deferred Listener and/or Environment changes to the audio driver.
     *
     * @param force  Usually updates are only necessary during playback. Use @c true to
     * override this check and write the changes regardless.
     */
    void writeDeferredProperties(bool force = false)
    {
        // Disabled?
        if(noUpdate) return;

        // Updates are only necessary during playback.
        if(playingMode != NotPlaying || force)
        {
            // When tracking an emitter we need the latest origin coordinates.
            getSound().updateOriginFromEmitter();
        }
    }

    void systemFrameEnds(System &)
    {
        writeDeferredProperties();
    }
};

DummySoundChannel::DummySoundChannel()
    : SoundChannel()
    , d(new Instance)
{}

PlayingMode DummySoundChannel::mode() const
{
    return d->playingMode;
}

void DummySoundChannel::play(PlayingMode mode)
{
    if(isPlaying()) return;

    if(mode == NotPlaying) return;

    d->buffer.reloadIfNeeded();
    // Playing is quite impossible without a loaded sample.
    if(!d->buffer.data)
    {
        throw Error("DummySoundChannel", "No sample is bound");
    }

    // Updating the channel should resume (presumably).
    d->noUpdate = false;

    // Flush deferred property value changes to the assigned data buffer.
    d->writeDeferredProperties(true/*force*/);

    // Playback begins!
    d->playingMode = mode;

    // Remember the current time.
    d->startTime = Timer_Ticks();

    // Predict when the first/only playback cycle will end (in milliseconds).
    d->endTime = Timer_RealMilliseconds() + d->buffer.milliseconds(d->frequency);
}

void DummySoundChannel::stop()
{
    // Playback ends forthwith!
    d->playingMode = NotPlaying;
    d->buffer.needReloadData = true;  // If subsequently started again.
}

bool DummySoundChannel::isPaused() const
{
    return false;  // Never...
}

void DummySoundChannel::pause()
{
    // Never paused...
}

void DummySoundChannel::resume()
{
    // Never paused...
}

void DummySoundChannel::suspend()
{
    if(!isPlaying()) return;
    d->noUpdate = true;
}

Channel &DummySoundChannel::setFrequency(dfloat newFrequency)
{
    d->frequency = newFrequency;
    return *this;
}

Channel &DummySoundChannel::setPositioning(Positioning newPositioning)
{
    d->positioning = newPositioning;
    return *this;
}

Channel &DummySoundChannel::setVolume(dfloat newVolume)
{
    d->volume = newVolume;
    return *this;
}

dfloat DummySoundChannel::frequency() const
{
    return d->frequency;
}

Positioning DummySoundChannel::positioning() const
{
    return d->positioning;
}

dfloat DummySoundChannel::volume() const
{
    return d->volume;
}

::audio::Sound *DummySoundChannel::sound() const
{
    return isPlaying() ? &d->getSound() : nullptr;
}

void DummySoundChannel::update()
{
    /**
     * Playback of non-looping sounds must stop when the first playback cycle ends.
     *
     * @note This test fails if the game has been running for about 50 days, since the
     * millisecond counter overflows. It only affects sounds that are playing while the
     * overflow happens, though.
     */
    if(isPlaying() && !isPlayingLooped() && Timer_RealMilliseconds() >= d->endTime)
    {
        stop();
    }
}

void DummySoundChannel::reset()
{
    stop();
    d->buffer.unload();
}

void DummySoundChannel::bindSample(sfxsample_t const &sample)
{
    stop();

    // Do we need to (re)configure the data buffer?
    if(   d->buffer.sampleBytes != sample.bytesPer
       || d->buffer.sampleRate  != sample.rate)
    {
        DENG2_ASSERT(!isPlaying());
        d->buffer.unload();
        d->buffer.sampleBytes = sample.bytesPer;
        d->buffer.sampleRate  = sample.rate;
    }

    // Don't reload if a sample with the same sound ID is already loaded.
    if(!d->buffer.data || d->buffer.data->effectId != sample.effectId)
    {
        d->buffer.load(&const_cast<sfxsample_t &>(sample));
    }
}

dint DummySoundChannel::bytes() const
{
    return d->buffer.sampleBytes;
}

dint DummySoundChannel::rate() const
{
    return d->buffer.sampleRate;
}

dint DummySoundChannel::startTime() const
{
    return d->startTime;
}

duint DummySoundChannel::endTime() const
{
    return d->endTime;
}

void DummySoundChannel::updateEnvironment()
{
    // Not supported.
}

}  // namespace audio