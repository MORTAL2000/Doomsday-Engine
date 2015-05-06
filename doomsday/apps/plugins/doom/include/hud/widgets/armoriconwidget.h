/** @file armoriconwidget.h  GUI widget for -.
 *
 * @authors Copyright © 2005-2014 Jaakko Keränen <jaakko.keranen@iki.fi>
 * @authors Copyright © 2005-2015 Daniel Swanson <danij@dengine.net>
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
 * General Public License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA</small>
 */

#if defined(__JHEXEN__) || defined(__JHERETIC__)
#   define LIBCOMMON_UI_ARMORICONWIDGET_H
#endif

#ifndef LIBCOMMON_UI_ARMORICONWIDGET_H
#define LIBCOMMON_UI_ARMORICONWIDGET_H

#include "hud/hudwidget.h"

/**
 * @ingroup ui
 */
class guidata_armoricon_t : public HudWidget
{
public:
    guidata_armoricon_t(de::dint player, int sprite1, int sprite2);
    virtual ~guidata_armoricon_t();

    void reset();

    void tick(timespan_t elapsed);
    void updateGeometry();
    void draw(de::Vector2i const &offset = de::Vector2i()) const;

private:
    de::dint currentSprite = 0;
    int armorSprite1       = 0;
    int armorSprite2       = 0;
};

#endif  // LIBCOMMON_UI_ARMORICONWIDGET_H