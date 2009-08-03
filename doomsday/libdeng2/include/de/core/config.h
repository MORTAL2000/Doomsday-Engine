/*
 * The Doomsday Engine Project -- libdeng2
 *
 * Copyright (c) 2009 Jaakko Keränen <jaakko.keranen@iki.fi>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
 
#ifndef LIBDENG2_CONFIG_H
#define LIBDENG2_CONFIG_H

#include "../Process"
#include "../String"

namespace de
{
    /**
     * Stores the configuration of everything. The application owns a Config.
     * The default configuration is produced by executing the .de scripts
     * in the config directories. The resulting namespace is serialized for 
     * storage, and is restored from the serialized version directly before the
     * config scripts are run.
     */
    class Config
    {
    public:
        Config();
        
        /**
         * Returns the configuration namespace.
         */
        Record& names();
        
    private:
        /// Directory where to read configuration files. Defaults to /config.
        String configPath_;
        
        /// The configuration namespace.
        Process config_;
    };
}

#endif /* LIBDENG2_CONFIG_H */
