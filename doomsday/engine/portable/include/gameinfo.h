/**\file
 *\section License
 * License: GPL
 * Online License Link: http://www.gnu.org/licenses/gpl.html
 *
 *\author Copyright © 2010 Daniel Swanson <danij@dengine.net>
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
 */

#ifndef LIBDENG_GAMEINFO_H
#define LIBDENG_GAMEINFO_H

#include "de_base.h"
#include "m_string.h"
#include "sys_reslocator.h"

/**
 * Game Resource Record.
 * Used to record information about a resource (e.g., a file name).
 *
 * @ingroup core
 */
typedef struct {
    /// Class of resource.
    resourceclass_t rclass;

    /// List of known potential names. Seperated with a semicolon.
    ddstring_t names;

    /// Vector of resource identify keys (e.g., file or lump names), used for identification purposes.
    ddstring_t** identityKeys;

    /// Path to this resource if found. Set during resource location.
    ddstring_t path;
} gameresource_record_t;

typedef struct {
    gameresource_record_t** records;
    size_t numRecords;
} gameresource_recordset_t;

/**
 * Game Info.
 * Used to record top-level game configurations registered by the loaded game logic module(s).
 *
 * @ingroup core
 */
typedef struct {
//private:
    /// Unique identifier of the plugin which registered this game.
    pluginid_t _pluginId;

    /// Unique identifier string (e.g., "doom1-ultimate").
    ddstring_t _identityKey;

    /// Formatted default title suitable for printing (e.g., "The Ultimate DOOM").
    ddstring_t _title;

    /// Formatted default author suitable for printing (e.g., "id Software").
    ddstring_t _author;

    /// The base directory for all data-class resources.
    ddstring_t _dataPath;

    /// The base directory for all defs-class resources.
    ddstring_t _defsPath;

    /// Name of the main/top-level definition file (e.g., "jdoom.ded").
    ddstring_t _mainDef;

    /// Command-line selection flags.
    ddstring_t* _cmdlineFlag, *_cmdlineFlag2;

    /// Lists of relative search paths to use when locating file resources. Determined automatically at creation time.
    ddstring_t _searchPathLists[NUM_RESOURCE_CLASSES];

    /// Vector of records for required game resources (e.g., doomu.wad).
    gameresource_recordset_t _requiredResources[NUM_RESOURCE_CLASSES];
} gameinfo_t;

/**
 * Create a new GameInfo.
 *
 * @param pluginId      Unique identifier of the plugin to associate with this game.
 * @param identityKey   Unique game mode key/identifier, 16 chars max (e.g., "doom1-ultimate").
 * @param dataPath      The base directory for all data-class resources.
 * @param defsPath      The base directory for all defs-class resources.
 * @param mainDef       The main/top-level definition file. Can be @c NULL.
 * @param title         Default game title.
 * @param author        Default game author.
 * @param cmdlineFlag   Command-line game selection override argument (e.g., "ultimate"). Can be @c NULL.
 * @param cmdlineFlag2  Alternative override. Can be @c NULL.
 */
gameinfo_t* P_CreateGameInfo(pluginid_t pluginId, const char* identityKey, const char* dataPath,
    const char* defsPath, const ddstring_t* mainDef, const char* title, const char* author,
    const ddstring_t* cmdlineFlag, const ddstring_t* cmdlineFlag2);

void P_DestroyGameInfo(gameinfo_t* info);

/**
 * Add a new resource to the list of resources.
 *
 * \note Resource registration order defines the order in which resources of each type are loaded.
 *
 * @param class         Class of resource being added.
 * @param rni           Namespace to associate the resource with.
 * @param name          Potential resource name.
 */
gameresource_record_t* GameInfo_AddResource(gameinfo_t* info, resourceclass_t rclass,
    resourcenamespaceid_t rni, const ddstring_t* name);

/**
 * Add a new file path to the list of resource-locator search paths.
 */
boolean GameInfo_AddResourceSearchPath(gameinfo_t* info, resourcenamespaceid_t rni, const char* newPath, boolean append);

/**
 * Clear resource-locator search paths for all namespaces.
 */
void GameInfo_ClearResourceSearchPaths(gameinfo_t* info);

/**
 * Clear resource-locator search paths for a specific resource namespace.
 */
void GameInfo_ClearResourceSearchPaths2(gameinfo_t* info, resourcenamespaceid_t rni);

/**
 * @return              Ptr to a string containing the resource search path list.
 */
const ddstring_t* GameInfo_ResourceSearchPaths(gameinfo_t* info, resourcenamespaceid_t rni);

/**
 * Accessor methods.
 */
/// @return             Unique plugin identifier attributed to that which registered this.
pluginid_t GameInfo_PluginId(gameinfo_t* info);

/// @return             Ptr to a string containing the identity key.
const ddstring_t* GameInfo_IdentityKey(gameinfo_t* info);

/// @return             Ptr to a string containing the default title.
const ddstring_t* GameInfo_Title(gameinfo_t* info);

/// @return             Ptr to a string containing the default author.
const ddstring_t* GameInfo_Author(gameinfo_t* info);

/// @return             Ptr to a string containing the name of the main definition file.
const ddstring_t* GameInfo_MainDef(gameinfo_t* info);

/// @return             Ptr to a string containing command line (name) flag.
const ddstring_t* GameInfo_CmdlineFlag(gameinfo_t* info);

/// @return             Ptr to a string containing command line (name) flag2.
const ddstring_t* GameInfo_CmdlineFlag2(gameinfo_t* info);

/// @return             Ptr to a vector of required resource records.
gameresource_record_t* const* GameInfo_Resources(gameinfo_t* info, resourcenamespaceid_t rni, size_t* count);

/**
 * \note Unless caller is the resource locator then you probably shouldn't be calling.
 * This is the base data path and shouldn't be used directly for resource location.
 * @return              Ptr to a string containing the base data-class resource directory.
 */
const ddstring_t* GameInfo_DataPath(gameinfo_t* info);

/**
 * \note Unless caller is the resource locator then you probably shouldn't be calling.
 * This is the base defs path and shouldn't be used directly for resource location.
 * @return              Ptr to a string containing the base defs-class resource directory.
 */
const ddstring_t* GameInfo_DefsPath(gameinfo_t* info);

#endif /* LIBDENG_GAMEINFO_H */
