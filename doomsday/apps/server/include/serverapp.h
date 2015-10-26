/** @file serverapp.h  The server application.
 *
 * @authors Copyright © 2013-2015 Jaakko Keränen <jaakko.keranen@iki.fi>
 * @authors Copyright © 2013-2015 Daniel Swanson <danij@dengine.net>
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

#ifndef SERVERAPP_H
#define SERVERAPP_H

#include <de/TextApp>
#include <doomsday/doomsdayapp.h>
#include <doomsday/Games>
#include "serversystem.h"
#include "ui/infine/infinesystem.h"
#include "audio/system.h"
#include "resource/resourcesystem.h"
#include "world/worldsystem.h"

/**
 * The server application.
 */
class ServerApp : public de::TextApp, public DoomsdayApp
{
public:
    ServerApp(int &argc, char **argv);
    ~ServerApp();

    /**
     * Sets up all the subsystems of the application. Must be called before the
     * event loop is started.
     */
    void initialize();

public:
    static ServerApp &app();
    static ServerSystem &serverSystem();
    static InFineSystem &infineSystem();
    static ::audio::System &audioSystem();
    static ResourceSystem &resourceSystem();
    static WorldSystem &worldSystem();

private:
    DENG2_PRIVATE(d)
};

#endif  // SERVERAPP_H