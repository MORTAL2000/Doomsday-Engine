/**\file
 *\section License
 * License: GPL + jHeretic/jHexen Exception
 * Online License Link: http://www.gnu.org/licenses/gpl.html
 *
 *\author Copyright © 2003-2007 Jaakko Keränen <jaakko.keranen@iki.fi>
 *\author Copyright © 2006-2007 Daniel Swanson <danij@dengine.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 *
 * In addition, as a special exception, we, the authors of deng
 * give permission to link the code of our release of deng with
 * the libjhexen and/or the libjheretic libraries (or with modified
 * versions of it that use the same license as the libjhexen or
 * libjheretic libraries), and distribute the linked executables.
 * You must obey the GNU General Public License in all respects for
 * all of the code used other than “libjhexen or libjheretic”. If
 * you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you
 * do not wish to do so, delete this exception statement from your version.
 */

/**
 * h_event.h:
 */

#ifndef __H_EVENT__
#define __H_EVENT__

#ifndef __JHERETIC__
#  error "Using jHeretic headers without __JHERETIC__"
#endif

#include "h_type.h"

typedef enum {
    GA_NONE,
    GA_LOADLEVEL,
    GA_NEWGAME,
    GA_LOADGAME,
    GA_SAVEGAME,
    GA_COMPLETED,
    GA_VICTORY,
    GA_WORLDDONE,
    GA_SCREENSHOT
} gameaction_t;

extern gameaction_t gameaction;

/*
//
// Button/action code definitions.
//
typedef enum
{
    // Press "Fire".
    //BT_ATTACK		= 1,
    // Use button, to open doors, activate switches.
    //BT_USE			= 2,

    // Flag: game events, not really buttons.
    //BT_SPECIAL		= 128,
    //BT_SPECIALMASK	= 3,

	// Center player look angle (pitch back to zero).
	//BT_LOOKCENTER	= 64,

    // Flag, weapon change pending.
    // If true, the next 3 bits hold weapon num.
    //BT_CHANGE		= 4,
    // The 3bit weapon mask and shift, convenience.
    //BT_WEAPONMASK	= (8+16+32+64),
    //BT_WEAPONSHIFT	= 3,

	//BT_JUMP			= 8,
    //BT_SPEED        = 16,

    // Pause the game.
    //BTS_PAUSE		= 1,
    // Save the game at each console.
    //BTS_SAVEGAME	= 2,

    // Savegame slot numbers
    //  occupy the second byte of buttons.
    //BTS_SAVEMASK	= (4+8+16),
    //BTS_SAVESHIFT 	= 2,

	// Special weapon change flags.
	//BTS_NEXTWEAPON	= 4,
	//BTS_PREVWEAPON	= 8,

} buttoncode_t;*/

#endif
