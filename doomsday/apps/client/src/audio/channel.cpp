/** @file channel.cpp  Logical sound playback channel.
 *
 * @authors Copyright © 2003-2013 Jaakko Keränen <jaakko.keranen@iki.fi>
 * @authors Copyright © 2006-2015 Daniel Swanson <danij@dengine.net>
 *
 * @par License
 * GPL: http://www.gnu.org/licenses/gpl.html
 *
 * <small>This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details. You should have received a copy of the GNU
 * General Public License along with this program; if not, see:
 * http://www.gnu.org/licenses</small>
 */

#include "audio/channel.h"

#include "audio/system.h"
#include <QList>

// Debug visual headers:
#include "audio/samplecache.h"
#include "gl/gl_main.h"
#include "api_fontrender.h"
#include "render/rend_font.h"
#include "ui/ui_main.h"
#include "def_main.h"           // ::defs
#include <de/concurrency.h>

using namespace de;

namespace audio {

DENG2_PIMPL(Channels)
{
    QList<Sound/*Channel*/ *> all;

    Instance(Public *i) : Base(i) 
    {}

    ~Instance()
    {
        clearAll();
    }

    void clearAll()
    {
        all.clear();
        notifyRemapped();
    }

    void notifyRemapped()
    {
        DENG2_FOR_PUBLIC_AUDIENCE2(Remapped, i)
        {
            i->channelsRemapped(self);
        }
    }

    DENG2_PIMPL_AUDIENCE(Remapped)
};

DENG2_AUDIENCE_METHOD(Channels, Remapped)

Channels::Channels() : d(new Instance(this))
{}

dint Channels::count() const
{
    return d->all.count();
}

dint Channels::countPlaying(dint soundId) const
{
    DENG2_ASSERT( System::get().sfxIsAvailable() );  // sanity check

    dint count = 0;
    forAll([&soundId, &count] (Sound/*Channel*/ &ch)
    {
        if(ch.isPlaying())
        {
            sfxbuffer_t const &sbuf = ch.buffer();
            if(sbuf.sample && sbuf.sample->soundId == soundId)
            {
                count += 1;
            }
        }
        return LoopContinue;
    });
    return count;
}

Sound/*Channel*/ &Channels::add(Sound &sound)
{
    if(!d->all.contains(&sound))
    {
        /// @todo Log sound configuration, update lookup tables for buffer configs, etc...
        d->all << &sound;

        d->notifyRemapped();
    }
    return sound;
}

Sound/*Channel*/ *Channels::tryFindVacant(bool use3D, dint bytes, dint rate, dint soundId) const
{
    for(Sound/*Channel*/ *ch : d->all)
    {
        if(!ch->isPlaying()) continue;

        sfxbuffer_t const &sbuf = ch->buffer();
        if(   use3D != ((sbuf.flags & SFXBF_3D) != 0)
           || sbuf.bytes != bytes
           || sbuf.rate  != rate)
            continue;

        // What about the sample?
        if(soundId > 0)
        {
            if(!sbuf.sample || sbuf.sample->soundId != soundId)
                continue;
        }
        else if(soundId == 0)
        {
            // We're trying to find a channel with no sample already loaded.
            if(sbuf.sample)
                continue;
        }

        // This is perfect, take this!
        return ch;
    }

    return nullptr;  // None suitable.
}

LoopResult Channels::forAll(std::function<LoopResult (Sound/*Channel*/ &)> func) const
{
    for(Sound/*Channel*/ *ch : d->all)
    {
        if(auto result = func(*ch)) return result;
    }
    return LoopContinue;
}

}  // namespace audio

using namespace audio;

// Debug visual: -----------------------------------------------------------------

dint showSoundInfo;
//byte refMonitor;

void UI_AudioChannelDrawer()
{
    if(!::showSoundInfo) return;

    DENG_ASSERT_IN_MAIN_THREAD();
    DENG_ASSERT_GL_CONTEXT_ACTIVE();

    // Go into screen projection mode.
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, DENG_GAMEVIEW_WIDTH, DENG_GAMEVIEW_HEIGHT, 0, -1, 1);

    glEnable(GL_TEXTURE_2D);

    FR_SetFont(fontFixed);
    FR_LoadDefaultAttrib();
    FR_SetColorAndAlpha(1, 1, 0, 1);

    dint const lh = FR_SingleLineHeight("Q");
    if(!audio::System::get().sfxIsAvailable())
    {
        FR_DrawTextXY("Sfx disabled", 0, 0);
        glDisable(GL_TEXTURE_2D);
        return;
    }

    /*
    if(::refMonitor)
        FR_DrawTextXY("!", 0, 0);
    */

    // Sample cache information.
    duint cachesize, ccnt;
    audio::System::get().sampleCache().info(&cachesize, &ccnt);
    char buf[200]; sprintf(buf, "Cached:%i (%i)", cachesize, ccnt);

    FR_SetColor(1, 1, 1);
    FR_DrawTextXY(buf, 10, 0);

    // Print a line of info about each channel.
    dint idx = 0;
    audio::System::get().channels().forAll([&lh, &idx] (Sound/*Channel*/ &ch)
    {
        if(ch.isPlaying())
        {
            FR_SetColor(1, 1, 1);
        }
        else
        {
            FR_SetColor(1, 1, 0);
        }

        char buf[200];
        sprintf(buf, "%02i: %c%c%c v=%3.1f f=%3.3f st=%i et=%u mobj=%i",
                idx,
                !(ch.flags() & SFXCF_NO_ORIGIN     ) ? 'O' : '.',
                !(ch.flags() & SFXCF_NO_ATTENUATION) ? 'A' : '.',
                ch.emitter() ? 'E' : '.',
                ch.volume(), ch.frequency(), ch.startTime(),
                ch.hasBuffer() ? ch.buffer().endTime : 0,
                ch.emitter()   ? ch.emitter()->thinker.id : 0);
        FR_DrawTextXY(buf, 5, lh * (1 + idx * 2));

        if(ch.hasBuffer())
        {
            sfxbuffer_t const &sbuf = ch.buffer();

            sprintf(buf, "    %c%c%c%c id=%03i/%-8s ln=%05i b=%i rt=%2i bs=%05i (C%05i/W%05i)",
                    (sbuf.flags & SFXBF_3D     ) ? '3' : '.',
                    (sbuf.flags & SFXBF_PLAYING) ? 'P' : '.',
                    (sbuf.flags & SFXBF_REPEAT ) ? 'R' : '.',
                    (sbuf.flags & SFXBF_RELOAD ) ? 'L' : '.',
                    sbuf.sample ? sbuf.sample->soundId : 0,
                    sbuf.sample ? ::defs.sounds[sbuf.sample->soundId].gets("id") : "",
                    sbuf.sample ? sbuf.sample->size : 0,
                    sbuf.bytes, sbuf.rate / 1000, sbuf.length,
                    sbuf.cursor, sbuf.written);
            FR_DrawTextXY(buf, 5, lh * (2 + idx * 2));
        }

        idx += 1;
        return LoopContinue;
    });

    glDisable(GL_TEXTURE_2D);

    // Back to the original.
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}
