/**
 * @file p_saveg.c
 * Common game-save state management.
 *
 * @authors Copyright &copy; 2003-2013 Jaakko Keränen <jaakko.keranen@iki.fi>
 * @authors Copyright &copy; 2005-2013 Daniel Swanson <danij@dengine.net>
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

#include <lzss.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "common.h"
#include "g_common.h"
#include "p_saveg.h"
#include "d_net.h"
#include "dmu_lib.h"
#include "fi_lib.h"
#include "p_map.h"
#include "p_mapsetup.h"
#include "p_player.h"
#include "p_inventory.h"
#include "am_map.h"
#include "p_tick.h"
#include "p_actor.h"
#include "p_ceiling.h"
#include "p_door.h"
#include "p_floor.h"
#include "p_plat.h"
#include "p_scroll.h"
#include "p_switch.h"
#include "hu_log.h"
#if __JHERETIC__ || __JHEXEN__
#include "hu_inventory.h"
#endif
#include "r_common.h"
#include "api_materialarchive.h"
#include "p_savedef.h"

#define MAX_HUB_MAPS        99

#define FF_FULLBRIGHT       0x8000 ///< Used to be a flag in thing->frame.
#define FF_FRAMEMASK        0x7fff

typedef struct playerheader_s {
    int             numPowers;
    int             numKeys;
    int             numFrags;
    int             numWeapons;
    int             numAmmoTypes;
    int             numPSprites;
#if __JDOOM64__ || __JHERETIC__ || __JHEXEN__
    int             numInvItemTypes;
#endif
#if __JHEXEN__
    int             numArmorTypes;
#endif
} playerheader_t;

// Thinker Save flags
#define TSF_SERVERONLY      0x01 ///< Only saved by servers.

typedef struct thinkerinfo_s {
    thinkerclass_t  thinkclass;
    thinkfunc_t         function;
    int             flags;
    void          (*Write) ();
    int           (*Read) ();
    size_t          size;
} thinkerinfo_t;

typedef enum sectorclass_e {
    sc_normal,
    sc_ploff,                   ///< plane offset
#if !__JHEXEN__
    sc_xg1,
#endif
    NUM_SECTORCLASSES
} sectorclass_t;

typedef enum lineclass_e {
    lc_normal,
#if !__JHEXEN__
    lc_xg1,
#endif
    NUM_LINECLASSES
} lineclass_t;

static boolean SV_RecogniseState(const char* path, SaveInfo* info);

static void SV_WriteMobj(const mobj_t* mobj);
static int SV_ReadMobj(thinker_t* th);
static void SV_WriteCeiling(const ceiling_t* ceiling);
static int SV_ReadCeiling(ceiling_t* ceiling);
static void SV_WriteDoor(const door_t* door);
static int SV_ReadDoor(door_t* door);
static void SV_WriteFloor(const floor_t* floor);
static int SV_ReadFloor(floor_t* floor);
static void SV_WritePlat(const plat_t* plat);
static int SV_ReadPlat(plat_t* plat);
static void SV_WriteMaterialChanger(const materialchanger_t* mchanger);
static int SV_ReadMaterialChanger(materialchanger_t* mchanger);

#if __JHEXEN__
static void SV_WriteLight(const light_t* light);
static int SV_ReadLight(light_t* light);
static void SV_WritePhase(const phase_t* phase);
static int SV_ReadPhase(phase_t* phase);
static void SV_WriteScript(const acs_t* script);
static int SV_ReadScript(acs_t* script);
static void SV_WriteDoorPoly(const polydoor_t* polydoor);
static int SV_ReadDoorPoly(polydoor_t* polydoor);
static void SV_WriteMovePoly(const polyevent_t* movepoly);
static int SV_ReadMovePoly(polyevent_t* movepoly);
static void SV_WriteRotatePoly(const polyevent_t* rotatepoly);
static int SV_ReadRotatePoly(polyevent_t* rotatepoly);
static void SV_WritePillar(const pillar_t* pillar);
static int SV_ReadPillar(pillar_t* pillar);
static void SV_WriteFloorWaggle(const waggle_t* floorwaggle);
static int SV_ReadFloorWaggle(waggle_t* floorwaggle);
#else
static void SV_WriteFlash(const lightflash_t* flash);
static int SV_ReadFlash(lightflash_t* flash);
static void SV_WriteStrobe(const strobe_t* strobe);
static int SV_ReadStrobe(strobe_t* strobe);
static void SV_WriteGlow(const glow_t* glow);
static int SV_ReadGlow(glow_t* glow);
# if __JDOOM__ || __JDOOM64__
static void SV_WriteFlicker(const fireflicker_t* flicker);
static int SV_ReadFlicker(fireflicker_t* flicker);
# endif

# if __JDOOM64__
static void SV_WriteBlink(const lightblink_t* flicker);
static int SV_ReadBlink(lightblink_t* flicker);
# endif
#endif
static void SV_WriteScroll(const scroll_t* scroll);
static int SV_ReadScroll(scroll_t* scroll);

#if __JHEXEN__
static void unarchiveMap(const Str* path);
#else
static void unarchiveMap(void);
#endif

static boolean inited = false;

static int cvarLastSlot; // -1 = Not yet loaded/saved in this game session.
static int cvarQuickSlot; // -1 = Not yet chosen/determined.

static SaveInfo** saveInfo;
static SaveInfo* autoSaveInfo;
#if __JHEXEN__
static SaveInfo* baseSaveInfo;
#endif
static SaveInfo* nullSaveInfo;

#if __JHEXEN__
static int mapVersion;
#endif
static const saveheader_t* hdr;

static playerheader_t playerHeader;
static boolean playerHeaderOK;
static mobj_t** thingArchive;
static uint thingArchiveSize;
static int saveToRealPlayerNum[MAXPLAYERS];
#if __JHEXEN__
static targetplraddress_t* targetPlayerAddrs;
static byte* saveBuffer;
static boolean savingPlayers;
#else
static int numSoundTargets;
#endif

static MaterialArchive* materialArchive;

static thinkerinfo_t thinkerInfo[] = {
    {
      TC_MOBJ,
      (thinkfunc_t) P_MobjThinker,
      TSF_SERVERONLY,
      SV_WriteMobj,
      SV_ReadMobj,
      sizeof(mobj_t)
    },
#if !__JHEXEN__
    {
      TC_XGMOVER,
      (thinkfunc_t) XS_PlaneMover,
      0,
      SV_WriteXGPlaneMover,
      SV_ReadXGPlaneMover,
      sizeof(xgplanemover_t)
    },
#endif
    {
      TC_CEILING,
      T_MoveCeiling,
      0,
      SV_WriteCeiling,
      SV_ReadCeiling,
      sizeof(ceiling_t)
    },
    {
      TC_DOOR,
      T_Door,
      0,
      SV_WriteDoor,
      SV_ReadDoor,
      sizeof(door_t)
    },
    {
      TC_FLOOR,
      T_MoveFloor,
      0,
      SV_WriteFloor,
      SV_ReadFloor,
      sizeof(floor_t)
    },
    {
      TC_PLAT,
      T_PlatRaise,
      0,
      SV_WritePlat,
      SV_ReadPlat,
      sizeof(plat_t)
    },
#if __JHEXEN__
    {
     TC_INTERPRET_ACS,
     (thinkfunc_t) T_InterpretACS,
     0,
     SV_WriteScript,
     SV_ReadScript,
     sizeof(acs_t)
    },
    {
     TC_FLOOR_WAGGLE,
     (thinkfunc_t) T_FloorWaggle,
     0,
     SV_WriteFloorWaggle,
     SV_ReadFloorWaggle,
     sizeof(waggle_t)
    },
    {
     TC_LIGHT,
     (thinkfunc_t) T_Light,
     0,
     SV_WriteLight,
     SV_ReadLight,
     sizeof(light_t)
    },
    {
     TC_PHASE,
     (thinkfunc_t) T_Phase,
     0,
     SV_WritePhase,
     SV_ReadPhase,
     sizeof(phase_t)
    },
    {
     TC_BUILD_PILLAR,
     (thinkfunc_t) T_BuildPillar,
     0,
     SV_WritePillar,
     SV_ReadPillar,
     sizeof(pillar_t)
    },
    {
     TC_ROTATE_POLY,
     T_RotatePoly,
     0,
     SV_WriteRotatePoly,
     SV_ReadRotatePoly,
     sizeof(polyevent_t)
    },
    {
     TC_MOVE_POLY,
     T_MovePoly,
     0,
     SV_WriteMovePoly,
     SV_ReadMovePoly,
     sizeof(polyevent_t)
    },
    {
     TC_POLY_DOOR,
     T_PolyDoor,
     0,
     SV_WriteDoorPoly,
     SV_ReadDoorPoly,
     sizeof(polydoor_t)
    },
#else
    {
      TC_FLASH,
      (thinkfunc_t) T_LightFlash,
      0,
      SV_WriteFlash,
      SV_ReadFlash,
      sizeof(lightflash_t)
    },
    {
      TC_STROBE,
      (thinkfunc_t) T_StrobeFlash,
      0,
      SV_WriteStrobe,
      SV_ReadStrobe,
      sizeof(strobe_t)
    },
    {
      TC_GLOW,
      (thinkfunc_t) T_Glow,
      0,
      SV_WriteGlow,
      SV_ReadGlow,
      sizeof(glow_t)
    },
# if __JDOOM__ || __JDOOM64__
    {
      TC_FLICKER,
      (thinkfunc_t) T_FireFlicker,
      0,
      SV_WriteFlicker,
      SV_ReadFlicker,
      sizeof(fireflicker_t)
    },
# endif
# if __JDOOM64__
    {
      TC_BLINK,
      (thinkfunc_t) T_LightBlink,
      0,
      SV_WriteBlink,
      SV_ReadBlink,
      sizeof(lightblink_t)
    },
# endif
#endif
    {
      TC_MATERIALCHANGER,
      T_MaterialChanger,
      0,
      SV_WriteMaterialChanger,
      SV_ReadMaterialChanger,
      sizeof(materialchanger_t)
    },
    {
        TC_SCROLL,
        (thinkfunc_t) T_Scroll,
        0,
        SV_WriteScroll,
        SV_ReadScroll,
        sizeof(scroll_t)
    },
    { TC_NULL, NULL, 0, NULL, NULL, 0 }
};

static void errorIfNotInited(const char* callerName)
{
    if(inited) return;
    Con_Error("%s: Saved game module is not presently initialized.", callerName);
    // Unreachable. Prevents static analysers from getting rather confused, poor things.
    exit(1);
}

/**
 * Compose the (possibly relative) path to the game-save associated
 * with the logical save @a slot.
 *
 * @param slot  Logical save slot identifier.
 * @param map   If @c >= 0 include this logical map index in the composed path.
 * @return  The composed path if reachable (else a zero-length string).
 */
static AutoStr* composeGameSavePathForSlot2(int slot, int map)
{
    AutoStr* path = AutoStr_NewStd();
    assert(inited);

    // A valid slot?
    if(!SV_IsValidSlot(slot)) return path;

    // Do we have a valid path?
    if(!F_MakePath(SV_SavePath())) return path;

    // Compose the full game-save path and filename.
    if(map >= 0)
    {
        Str_Appendf(path, "%s" SAVEGAMENAME "%i%02i." SAVEGAMEEXTENSION, SV_SavePath(), slot, map);
    }
    else
    {
        Str_Appendf(path, "%s" SAVEGAMENAME "%i." SAVEGAMEEXTENSION, SV_SavePath(), slot);
    }
    F_TranslatePath(path, path);
    return path;
}

static AutoStr* composeGameSavePathForSlot(int slot)
{
    return composeGameSavePathForSlot2(slot, -1);
}

#if !__JHEXEN__
/**
 * Compose the (possibly relative) path to the game-save associated
 * with @a gameId.
 *
 * @param gameId  Unique game identifier.
 * @return  File path to the reachable save directory. If the game-save path
 *          is unreachable then a zero-length string is returned instead.
 */
static AutoStr* composeGameSavePathForClientGameId(uint gameId)
{
    AutoStr* path = AutoStr_NewStd();
    // Do we have a valid path?
    if(!F_MakePath(SV_ClientSavePath())) return path; // return zero-length string.
    // Compose the full game-save path and filename.
    Str_Appendf(path, "%s" CLIENTSAVEGAMENAME "%08X." SAVEGAMEEXTENSION, SV_ClientSavePath(), gameId);
    F_TranslatePath(path, path);
    return path;
}
#endif

static void clearSaveInfo(void)
{
    if(saveInfo)
    {
        int i;
        for(i = 0; i < NUMSAVESLOTS; ++i)
        {
            SaveInfo* info = saveInfo[i];
            SaveInfo_Delete(info);
        }
        free(saveInfo); saveInfo = NULL;
    }

    if(autoSaveInfo)
    {
        SaveInfo_Delete(autoSaveInfo); autoSaveInfo = NULL;
    }
#if __JHEXEN__
    if(baseSaveInfo)
    {
        SaveInfo_Delete(baseSaveInfo); baseSaveInfo = NULL;
    }
#endif
    if(nullSaveInfo)
    {
        SaveInfo_Delete(nullSaveInfo); nullSaveInfo = NULL;
    }
}

static void updateSaveInfo(const Str* path, SaveInfo* info)
{
    if(!info) return;

    if(!path || Str_IsEmpty(path))
    {
        // The save path cannot be accessed for some reason. Perhaps its a
        // network path? Clear the info for this slot.
        SaveInfo_SetName(info, 0);
        SaveInfo_SetGameId(info, 0);
        return;
    }

    // Is this a recognisable save state?
    if(!SV_RecogniseState(Str_Text(path), info))
    {
        // Clear the info for this slot.
        SaveInfo_SetName(info, 0);
        SaveInfo_SetGameId(info, 0);
        return;
    }

    // Ensure we have a valid name.
    if(Str_IsEmpty(SaveInfo_Name(info)))
    {
        SaveInfo_SetName(info, AutoStr_FromText("UNNAMED"));
    }
}

/// Re-build game-save info by re-scanning the save paths and populating the list.
static void buildSaveInfo(void)
{
    int i;
    assert(inited);

    if(!saveInfo)
    {
        // Not yet been here. We need to allocate and initialize the game-save info list.
        saveInfo = (SaveInfo**) malloc(NUMSAVESLOTS * sizeof(*saveInfo));
        if(!saveInfo)
            Con_Error("buildSaveInfo: Failed on allocation of %lu bytes for game-save info list.",
                      (unsigned long) (NUMSAVESLOTS * sizeof(*saveInfo)));

        // Initialize.
        for(i = 0; i < NUMSAVESLOTS; ++i)
        {
            saveInfo[i] = SaveInfo_New();
        }
        autoSaveInfo = SaveInfo_New();
#if __JHEXEN__
        baseSaveInfo = SaveInfo_New();
#endif
        nullSaveInfo = SaveInfo_New();
    }

    /// Scan the save paths and populate the list.
    /// @todo We should look at all files on the save path and not just those
    /// which match the default game-save file naming convention.
    for(i = 0; i < NUMSAVESLOTS; ++i)
    {
        SaveInfo* info = saveInfo[i];
        updateSaveInfo(composeGameSavePathForSlot(i), info);
    }
    updateSaveInfo(composeGameSavePathForSlot(AUTO_SLOT), autoSaveInfo);
#if __JHEXEN__
    updateSaveInfo(composeGameSavePathForSlot(BASE_SLOT), baseSaveInfo);
#endif
}

/// Given a logical save slot identifier retrieve the assciated game-save info.
static SaveInfo* findSaveInfoForSlot(int slot)
{
    assert(inited);

    if(!SV_IsValidSlot(slot)) return nullSaveInfo;

    // On first call - automatically build and populate game-save info.
    if(!saveInfo)
    {
        buildSaveInfo();
    }

    // Retrieve the info for this slot.
    if(slot == AUTO_SLOT) return autoSaveInfo;
#if __JHEXEN__
    if(slot == BASE_SLOT) return baseSaveInfo;
#endif
    return saveInfo[slot];
}

static void replaceSaveInfo(int slot, SaveInfo* newInfo)
{
    SaveInfo** destAdr;
    assert(SV_IsValidSlot(slot));
    if(slot == AUTO_SLOT)
    {
        destAdr = &autoSaveInfo;
    }
#if __JHEXEN__
    else if(slot == BASE_SLOT)
    {
        destAdr = &baseSaveInfo;
    }
#endif
    else
    {
        destAdr = &saveInfo[slot];
    }
    if(*destAdr) SaveInfo_Delete(*destAdr);
    *destAdr = newInfo;
}

void SV_Register(void)
{
#if !__JHEXEN__
    C_VAR_BYTE("game-save-auto-loadonreborn",   &cfg.loadAutoSaveOnReborn,  0, 0, 1);
#endif
    C_VAR_BYTE("game-save-confirm",             &cfg.confirmQuickGameSave,  0, 0, 1);
    C_VAR_BYTE("game-save-confirm-loadonreborn",&cfg.confirmRebornLoad,     0, 0, 1);
    C_VAR_BYTE("game-save-last-loadonreborn",   &cfg.loadLastSaveOnReborn,  0, 0, 1);
    C_VAR_INT ("game-save-last-slot",           &cvarLastSlot, CVF_NO_MIN|CVF_NO_MAX|CVF_NO_ARCHIVE|CVF_READ_ONLY, 0, 0);
    C_VAR_INT ("game-save-quick-slot",          &cvarQuickSlot, CVF_NO_MAX|CVF_NO_ARCHIVE, -1, 0);

    // Aliases for obsolete cvars:
    C_VAR_BYTE("menu-quick-ask",                &cfg.confirmQuickGameSave, 0, 0, 1);
}

AutoStr* SV_ComposeSlotIdentifier(int slot)
{
    AutoStr* str = AutoStr_NewStd();
    if(slot < 0) return Str_Set(str, "(invalid slot)");
    if(slot == AUTO_SLOT) return Str_Set(str, "<auto>");
#if __JHEXEN__
    if(slot == BASE_SLOT) return Str_Set(str, "<base>");
#endif
    return Str_Appendf(str, "%i", slot);
}

void SV_ClearSlot(int slot)
{
    AutoStr* path;

    errorIfNotInited("SV_ClearSlot");
    if(!SV_IsValidSlot(slot)) return;

    // Announce when clearing save slots (for auto and base slots too if _DEBUG).
#if !_DEBUG
# if __JHEXEN__
    if(slot != AUTO_SLOT && slot != BASE_SLOT)
# else
    if(slot != AUTO_SLOT)
# endif
#endif
    {
        AutoStr* ident = SV_ComposeSlotIdentifier(slot);
        Con_Message("Clearing save slot %s", Str_Text(ident));
    }

    { int i;
    for(i = 0; i < MAX_HUB_MAPS; ++i)
    {
        path = composeGameSavePathForSlot2(slot, i);
        SV_RemoveFile(path);
    }}

    path = composeGameSavePathForSlot(slot);
    SV_RemoveFile(path);

    // Update save info for this slot.
    updateSaveInfo(path, findSaveInfoForSlot(slot));
}

boolean SV_IsValidSlot(int slot)
{
    if(slot == AUTO_SLOT) return true;
#if __JHEXEN__
    if(slot == BASE_SLOT) return true;
#endif
    return (slot >= 0  && slot < NUMSAVESLOTS);
}

boolean SV_IsUserWritableSlot(int slot)
{
    if(slot == AUTO_SLOT) return false;
#if __JHEXEN__
    if(slot == BASE_SLOT) return false;
#endif
    return SV_IsValidSlot(slot);
}

static void SV_SaveInfo_Read(SaveInfo* info)
{
    Reader* svReader = SV_NewReader();
#if __JHEXEN__
    // Read the magic byte to determine the high-level format.
    int magic = Reader_ReadInt32(svReader);
    SV_HxSavePtr()->b -= 4; // Rewind the stream.

    if((!IS_NETWORK_CLIENT && magic != MY_SAVE_MAGIC) ||
       ( IS_NETWORK_CLIENT && magic != MY_CLIENT_SAVE_MAGIC))
    {
        // Perhaps the old v9 format?
        SaveInfo_Read_Hx_v9(info, svReader);
    }
    else
#endif
    {
        SaveInfo_Read(info, svReader);
    }
    Reader_Delete(svReader);
}

static boolean recogniseState(const char* path, SaveInfo* info)
{
#if __JHEXEN__
    byte* saveBuffer;
#endif

    DENG_ASSERT(path);
    DENG_ASSERT(info);

    if(!SV_ExistingFile(path)) return false;

#if __JHEXEN__
    /// @todo Do not buffer the whole file.
    if(M_ReadFile(path, (char**)&saveBuffer))
#else
    if(SV_OpenFile(path, "rp"))
#endif
    {
#if __JHEXEN__
        // Set the save pointer.
        SV_HxSavePtr()->b = saveBuffer;
#endif

        SV_SaveInfo_Read(info);

#if __JHEXEN__
        Z_Free(saveBuffer);
#else
        SV_CloseFile();
#endif

        // Magic must match.
        if(info->header.magic != MY_SAVE_MAGIC &&
           info->header.magic != MY_CLIENT_SAVE_MAGIC) return false;

        /**
         * Check for unsupported versions.
         */
        // A future version?
        if(info->header.version > MY_SAVE_VERSION) return false;

#if __JHEXEN__
        // We are incompatible with v3 saves due to an invalid test used to determine
        // present sidedefs (ver3 format's sidedefs contain chunks of junk data).
        if(info->header.version == 3) return false;
#endif
        return true;
    }
    return false;
}

static boolean SV_RecogniseState(const char* path, SaveInfo* info)
{
    if(path && info)
    {
        if(recogniseState(path, info)) return true;
        // Perhaps an original game save?
#if __JDOOM__
        if(SV_RecogniseState_Dm_v19(path, info)) return true;
#endif
#if __JHERETIC__
        if(SV_RecogniseState_Hr_v13(path, info)) return true;
#endif
    }
    return false;
}

SaveInfo* SV_SaveInfoForSlot(int slot)
{
    errorIfNotInited("SV_SaveInfoForSlot");
    return findSaveInfoForSlot(slot);
}

void SV_UpdateAllSaveInfo(void)
{
    errorIfNotInited("SV_UpdateAllSaveInfo");
    buildSaveInfo();
}

int SV_ParseSlotIdentifier(const char* str)
{
    int slot;

    // Try game-save name match.
    slot = SV_SlotForSaveName(str);
    if(slot >= 0)
    {
        return slot;
    }

    // Try keyword identifiers.
    if(!stricmp(str, "last") || !stricmp(str, "<last>"))
    {
        return Con_GetInteger("game-save-last-slot");
    }
    if(!stricmp(str, "quick") || !stricmp(str, "<quick>"))
    {
        return Con_GetInteger("game-save-quick-slot");
    }
    if(!stricmp(str, "auto") || !stricmp(str, "<auto>"))
    {
        return AUTO_SLOT;
    }

    // Try logical slot identifier.
    if(M_IsStringValidInt(str))
    {
        return atoi(str);
    }

    // Unknown/not found.
    return -1;
}

int SV_SlotForSaveName(const char* name)
{
    int saveSlot = -1;

    errorIfNotInited("SV_SlotForSaveName");

    if(name && name[0])
    {
        int i = 0;
        // On first call - automatically build and populate game-save info.
        if(!saveInfo)
        {
            buildSaveInfo();
        }

        do
        {
            SaveInfo* info = saveInfo[i];
            if(!Str_CompareIgnoreCase(SaveInfo_Name(info), name))
            {
                // This is the one!
                saveSlot = i;
            }
        } while(-1 == saveSlot && ++i < NUMSAVESLOTS);
    }
    return saveSlot;
}

boolean SV_IsSlotUsed(int slot)
{
    errorIfNotInited("SV_IsSlotUsed");
    if(SV_ExistingFile(Str_Text(composeGameSavePathForSlot(slot))))
    {
        SaveInfo* info = SV_SaveInfoForSlot(slot);
        return SaveInfo_IsLoadable(info);
    }
    return false;
}

#if __JHEXEN__
boolean SV_HxHaveMapSaveForSlot(int slot, uint map)
{
    AutoStr* path = composeGameSavePathForSlot2(slot, (int)map+1);
    if(!path || Str_IsEmpty(path)) return false;
    return SV_ExistingFile(Str_Text(path));
}
#endif

void SV_CopySlot(int sourceSlot, int destSlot)
{
    AutoStr* src, *dst;

    errorIfNotInited("SV_CopySlot");

    if(!SV_IsValidSlot(sourceSlot))
    {
#if _DEBUG
        Con_Message("Warning: SV_CopySlot: Source slot %i invalid, save game not copied.", sourceSlot);
#endif
        return;
    }

    if(!SV_IsValidSlot(destSlot))
    {
#if _DEBUG
        Con_Message("Warning: SV_CopySlot: Dest slot %i invalid, save game not copied.", destSlot);
#endif
        return;
    }

    // Clear all save files at destination slot.
    SV_ClearSlot(destSlot);

    { int i;
    for(i = 0; i < MAX_HUB_MAPS; ++i)
    {
        src = composeGameSavePathForSlot2(sourceSlot, i);
        dst = composeGameSavePathForSlot2(destSlot, i);
        SV_CopyFile(src, dst);
    }}

    src = composeGameSavePathForSlot(sourceSlot);
    dst = composeGameSavePathForSlot(destSlot);
    SV_CopyFile(src, dst);

    // Copy saveinfo too.
    replaceSaveInfo(destSlot, SaveInfo_NewCopy(findSaveInfoForSlot(sourceSlot)));
}

#if __JHEXEN__
void SV_HxInitBaseSlot(void)
{
    SV_ClearSlot(BASE_SLOT);
}
#endif

/**
 * @return              Ptr to the thinkerinfo for the given thinker.
 */
static thinkerinfo_t* infoForThinker(thinker_t* th)
{
    thinkerinfo_t*      thInfo = thinkerInfo;

    if(!th)
        return NULL;

    while(thInfo->thinkclass != TC_NULL)
    {
        if(thInfo->function == th->function)
            return thInfo;

        thInfo++;
    }

    return NULL;
}

static int removeThinker(thinker_t* th, void* context)
{
    if(th->function == (thinkfunc_t) P_MobjThinker)
        P_MobjRemove((mobj_t *) th, true);
    else
        Z_Free(th);

    return false; // Continue iteration.
}

typedef struct {
    uint                count;
    boolean             savePlayers;
} countmobjsparams_t;

static int countMobjs(thinker_t* th, void* context)
{
    countmobjsparams_t* params = (countmobjsparams_t*) context;
    mobj_t*             mo = (mobj_t*) th;

    if(!(mo->player && !params->savePlayers))
        params->count++;

    return false; // Continue iteration.
}

/**
 * Must be called before saving or loading any data.
 */
static uint SV_InitThingArchive(boolean load, boolean savePlayers)
{
    countmobjsparams_t  params;

    params.count = 0;
    params.savePlayers = savePlayers;

    if(load)
    {
#if !__JHEXEN__
        if(hdr->version < 5)
            params.count = 1024; // Limit in previous versions.
        else
#endif
            params.count = SV_ReadLong();
    }
    else
    {
        // Count the number of mobjs we'll be writing.
        Thinker_Iterate((thinkfunc_t) P_MobjThinker, countMobjs, &params);
    }

    thingArchive = calloc(params.count, sizeof(mobj_t*));
    return thingArchiveSize = params.count;
}

/**
 * Used by the read code when mobjs are read.
 */
static void SV_SetArchiveThing(mobj_t* mo, int num)
{
#if __JHEXEN__
    if(mapVersion >= 4)
#endif
        num -= 1;

    if(num < 0)
        return;

    if(!thingArchive)
        Con_Error("SV_SetArchiveThing: Thing archive uninitialized.");

    thingArchive[num] = mo;
}

/**
 * Free the thing archive. Called when load is complete.
 */
static void SV_FreeThingArchive(void)
{
    if(thingArchive)
    {
        free(thingArchive);
        thingArchive = NULL;
        thingArchiveSize = 0;
    }
}

/**
 * Called by the write code to get archive numbers.
 * If the mobj is already archived, the existing number is returned.
 * Number zero is not used.
 */
#if __JHEXEN__
int SV_ThingArchiveNum(mobj_t* mo)
#else
unsigned short SV_ThingArchiveNum(mobj_t* mo)
#endif
{
    uint i, firstEmpty = 0;
    boolean found;

    errorIfNotInited("SV_ThingArchiveNum");

    // We only archive valid mobj thinkers.
    if(mo == NULL || ((thinker_t *) mo)->function != (thinkfunc_t) P_MobjThinker)
        return 0;

#if __JHEXEN__
    if(mo->player && !savingPlayers)
        return MOBJ_XX_PLAYER;
#endif

    if(!thingArchive)
        Con_Error("SV_ThingArchiveNum: Thing archive uninitialized.");

    found = false;
    for(i = 0; i < thingArchiveSize; ++i)
    {
        if(!thingArchive[i] && !found)
        {
            firstEmpty = i;
            found = true;
            continue;
        }
        if(thingArchive[i] == mo)
            return i + 1;
    }

    if(!found)
    {
        Con_Error("SV_ThingArchiveNum: Thing archive exhausted!\n");
        return 0; // No number available!
    }

    // OK, place it in an empty pos.
    thingArchive[firstEmpty] = mo;
    return firstEmpty + 1;
}

Material* SV_GetArchiveMaterial(materialarchive_serialid_t serialId, int group)
{
    errorIfNotInited("SV_GetArchiveMaterial");
    assert(materialArchive);
    return MaterialArchive_Find(materialArchive, serialId, group);
}

#if __JHEXEN__
static void SV_FreeTargetPlayerList(void)
{
    targetplraddress_t* p = targetPlayerAddrs, *np;

    while(p != NULL)
    {
        np = p->next;
        free(p);
        p = np;
    }
    targetPlayerAddrs = NULL;
}
#endif

/**
 * Called by the read code to resolve mobj ptrs from archived thing ids
 * after all thinkers have been read and spawned into the map.
 */
mobj_t* SV_GetArchiveThing(int thingid, void *address)
{
    errorIfNotInited("SV_GetArchiveThing");

#if __JHEXEN__
    if(thingid == MOBJ_XX_PLAYER)
    {
        targetplraddress_t *tpa = malloc(sizeof(targetplraddress_t));

        tpa->address = address;

        tpa->next = targetPlayerAddrs;
        targetPlayerAddrs = tpa;

        return NULL;
    }
#endif

    if(!thingArchive)
        Con_Error("SV_GetArchiveThing: Thing archive uninitialized.");

    // Check that the thing archive id is valid.
#if __JHEXEN__
    if(mapVersion < 4)
    {
        // Old format is base 0.
        if(thingid == -1)
            return NULL; // A NULL reference.

        if(thingid < 0 || (uint) thingid > thingArchiveSize - 1)
            return NULL;
    }
    else
#endif
    {   // New format is base 1.
        if(thingid == 0)
            return NULL; // A NULL reference.

        if(thingid < 1 || (uint) thingid > thingArchiveSize)
        {
            Con_Message("SV_GetArchiveThing: Invalid NUM %i??", thingid);
            return NULL;
        }

        thingid -= 1;
    }

    return thingArchive[thingid];
}

static playerheader_t* getPlayerHeader(void)
{
#if _DEBUG
    if(!playerHeaderOK)
        Con_Error("getPlayerHeader: Attempted to read before init!");
#endif
    return &playerHeader;
}

uint SV_GenerateGameId(void)
{
    return Timer_RealMilliseconds() + (mapTime << 24);
}

/**
 * Writes the given player's data (not including the ID number).
 */
static void SV_WritePlayer(int playernum)
{
    int                 i, numPSprites = getPlayerHeader()->numPSprites;
    player_t            temp, *p = &temp;
    ddplayer_t          ddtemp, *dp = &ddtemp;

    // Make a copy of the player.
    memcpy(p, &players[playernum], sizeof(temp));
    memcpy(dp, players[playernum].plr, sizeof(ddtemp));
    temp.plr = &ddtemp;

    // Convert the psprite states.
    for(i = 0; i < numPSprites; ++i)
    {
        pspdef_t       *pspDef = &temp.pSprites[i];

        if(pspDef->state)
        {
            pspDef->state = (state_t *) (pspDef->state - STATES);
        }
    }

    // Version number. Increase when you make changes to the player data
    // segment format.
    SV_WriteByte(6);

#if __JHEXEN__
    // Class.
    SV_WriteByte(cfg.playerClass[playernum]);
#endif

    SV_WriteLong(p->playerState);
#if __JHEXEN__
    SV_WriteLong(p->class_);    // 2nd class...?
#endif
    SV_WriteLong(FLT2FIX(p->viewZ));
    SV_WriteLong(FLT2FIX(p->viewHeight));
    SV_WriteLong(FLT2FIX(p->viewHeightDelta));
#if !__JHEXEN__
    SV_WriteFloat(dp->lookDir);
#endif
    SV_WriteLong(FLT2FIX(p->bob));
#if __JHEXEN__
    SV_WriteLong(p->flyHeight);
    SV_WriteFloat(dp->lookDir);
    SV_WriteLong(p->centering);
#endif
    SV_WriteLong(p->health);

#if __JHEXEN__
    for(i = 0; i < getPlayerHeader()->numArmorTypes; ++i)
    {
        SV_WriteLong(p->armorPoints[i]);
    }
#else
    SV_WriteLong(p->armorPoints);
    SV_WriteLong(p->armorType);
#endif

#if __JDOOM64__ || __JHEXEN__
    for(i = 0; i < getPlayerHeader()->numInvItemTypes; ++i)
    {
        inventoryitemtype_t type = IIT_FIRST + i;

        SV_WriteLong(type);
        SV_WriteLong(P_InventoryCount(playernum, type));
    }
    SV_WriteLong(P_InventoryReadyItem(playernum));
#endif

    for(i = 0; i < getPlayerHeader()->numPowers; ++i)
    {
        SV_WriteLong(p->powers[i]);
    }

#if __JHEXEN__
    SV_WriteLong(p->keys);
#else
    for(i = 0; i < getPlayerHeader()->numKeys; ++i)
    {
        SV_WriteLong(p->keys[i]);
    }
#endif

#if __JHEXEN__
    SV_WriteLong(p->pieces);
#else
    SV_WriteLong(p->backpack);
#endif

    for(i = 0; i < getPlayerHeader()->numFrags; ++i)
    {
        SV_WriteLong(p->frags[i]);
    }

    SV_WriteLong(p->readyWeapon);
    SV_WriteLong(p->pendingWeapon);

    for(i = 0; i < getPlayerHeader()->numWeapons; ++i)
    {
        SV_WriteLong(p->weapons[i].owned);
    }

    for(i = 0; i < getPlayerHeader()->numAmmoTypes; ++i)
    {
        SV_WriteLong(p->ammo[i].owned);
#if !__JHEXEN__
        SV_WriteLong(p->ammo[i].max);
#endif
    }

    SV_WriteLong(p->attackDown);
    SV_WriteLong(p->useDown);

    SV_WriteLong(p->cheats);

    SV_WriteLong(p->refire);

    SV_WriteLong(p->killCount);
    SV_WriteLong(p->itemCount);
    SV_WriteLong(p->secretCount);

    SV_WriteLong(p->damageCount);
    SV_WriteLong(p->bonusCount);
#if __JHEXEN__
    SV_WriteLong(p->poisonCount);
#endif

    SV_WriteLong(dp->extraLight);
    SV_WriteLong(dp->fixedColorMap);
    SV_WriteLong(p->colorMap);

    for(i = 0; i < numPSprites; ++i)
    {
        pspdef_t       *psp = &p->pSprites[i];

        SV_WriteLong(PTR2INT(psp->state));
        SV_WriteLong(psp->tics);
        SV_WriteLong(FLT2FIX(psp->pos[VX]));
        SV_WriteLong(FLT2FIX(psp->pos[VY]));
    }

#if !__JHEXEN__
    SV_WriteLong(p->didSecret);

    // Added in ver 2 with __JDOOM__
    SV_WriteLong(p->flyHeight);
#endif

#if __JHERETIC__
    for(i = 0; i < getPlayerHeader()->numInvItemTypes; ++i)
    {
        inventoryitemtype_t type = IIT_FIRST + i;

        SV_WriteLong(type);
        SV_WriteLong(P_InventoryCount(playernum, type));
    }
    SV_WriteLong(P_InventoryReadyItem(playernum));
    SV_WriteLong(p->chickenPeck);
#endif

#if __JHERETIC__ || __JHEXEN__
    SV_WriteLong(p->morphTics);
#endif

    SV_WriteLong(p->airCounter);

#if __JHEXEN__
    SV_WriteLong(p->jumpTics);
    SV_WriteLong(p->worldTimer);
#elif __JHERETIC__
    SV_WriteLong(p->flameCount);

    // Added in ver 2
    SV_WriteByte(p->class_);
#endif
}

/**
 * Reads a player's data (not including the ID number).
 */
static void SV_ReadPlayer(player_t* p)
{
    int                 i, plrnum = p - players,
                        numPSprites = getPlayerHeader()->numPSprites;
    byte                ver;
    ddplayer_t*         dp = p->plr;

    ver = SV_ReadByte();

#if __JHEXEN__
    cfg.playerClass[plrnum] = SV_ReadByte();

    memset(p, 0, sizeof(*p));   // Force everything NULL,
    p->plr = dp;                // but restore the ddplayer pointer.
#endif

    p->playerState = SV_ReadLong();
#if __JHEXEN__
    p->class_ = SV_ReadLong();        // 2nd class...?
#endif

    p->viewZ = FIX2FLT(SV_ReadLong());
    p->viewHeight = FIX2FLT(SV_ReadLong());
    p->viewHeightDelta = FIX2FLT(SV_ReadLong());
#if !__JHEXEN__
    dp->lookDir = SV_ReadFloat();
#endif
    p->bob = FIX2FLT(SV_ReadLong());
#if __JHEXEN__
    p->flyHeight = SV_ReadLong();
    dp->lookDir = SV_ReadFloat();
    p->centering = SV_ReadLong();
#endif

    p->health = SV_ReadLong();

#if __JHEXEN__
    for(i = 0; i < getPlayerHeader()->numArmorTypes; ++i)
    {
        p->armorPoints[i] = SV_ReadLong();
    }
#else
    p->armorPoints = SV_ReadLong();
    p->armorType = SV_ReadLong();
#endif

#if __JDOOM64__ || __JHEXEN__
    P_InventoryEmpty(plrnum);
    for(i = 0; i < getPlayerHeader()->numInvItemTypes; ++i)
    {
        inventoryitemtype_t type = SV_ReadLong();
        int             j, count = SV_ReadLong();

        for(j = 0; j < count; ++j)
            P_InventoryGive(plrnum, type, true);
    }

    P_InventorySetReadyItem(plrnum, (inventoryitemtype_t) SV_ReadLong());
# if __JHEXEN__
    Hu_InventorySelect(plrnum, P_InventoryReadyItem(plrnum));
    if(ver < 5)
    {
        SV_ReadLong(); // Current inventory item count?
    }
    if(ver < 6)
    /*p->inventorySlotNum =*/ SV_ReadLong();
# endif
#endif

    for(i = 0; i < getPlayerHeader()->numPowers; ++i)
    {
        p->powers[i] = SV_ReadLong();
    }
    if(p->powers[PT_ALLMAP])
        ST_RevealAutomap(plrnum, true);

#if __JHEXEN__
    p->keys = SV_ReadLong();
#else
    for(i = 0; i < getPlayerHeader()->numKeys; ++i)
    {
        p->keys[i] = SV_ReadLong();
    }
#endif

#if __JHEXEN__
    p->pieces = SV_ReadLong();
#else
    p->backpack = SV_ReadLong();
#endif

    for(i = 0; i < getPlayerHeader()->numFrags; ++i)
    {
        p->frags[i] = SV_ReadLong();
    }

    p->readyWeapon = SV_ReadLong();
#if __JHEXEN__
    if(ver < 5)
        p->pendingWeapon = WT_NOCHANGE;
    else
#endif
        p->pendingWeapon = SV_ReadLong();

    for(i = 0; i < getPlayerHeader()->numWeapons; ++i)
    {
        p->weapons[i].owned = (SV_ReadLong()? true : false);
    }

    for(i = 0; i < getPlayerHeader()->numAmmoTypes; ++i)
    {
        p->ammo[i].owned = SV_ReadLong();

#if !__JHEXEN__
        p->ammo[i].max = SV_ReadLong();
#endif
    }

    p->attackDown = SV_ReadLong();
    p->useDown = SV_ReadLong();

    p->cheats = SV_ReadLong();

    p->refire = SV_ReadLong();

    p->killCount = SV_ReadLong();
    p->itemCount = SV_ReadLong();
    p->secretCount = SV_ReadLong();

#if __JHEXEN__
    if(ver <= 1)
    {
        /*p->messageTics =*/ SV_ReadLong();
        /*p->ultimateMessage =*/ SV_ReadLong();
        /*p->yellowMessage =*/ SV_ReadLong();
    }
#endif

    p->damageCount = SV_ReadLong();
    p->bonusCount = SV_ReadLong();
#if __JHEXEN__
    p->poisonCount = SV_ReadLong();
#endif

    dp->extraLight = SV_ReadLong();
    dp->fixedColorMap = SV_ReadLong();
    p->colorMap = SV_ReadLong();

    for(i = 0; i < numPSprites; ++i)
    {
        pspdef_t *psp = &p->pSprites[i];

        psp->state = (state_t*) SV_ReadLong();
        psp->tics = SV_ReadLong();
        psp->pos[VX] = FIX2FLT(SV_ReadLong());
        psp->pos[VY] = FIX2FLT(SV_ReadLong());
    }

#if !__JHEXEN__
    p->didSecret = SV_ReadLong();

# if __JDOOM__ || __JDOOM64__
    if(ver == 2) // nolonger used in >= ver 3
        /*p->messageTics =*/ SV_ReadLong();

    if(ver >= 2)
        p->flyHeight = SV_ReadLong();

# elif __JHERETIC__
    if(ver < 3) // nolonger used in >= ver 3
        /*p->messageTics =*/ SV_ReadLong();

    p->flyHeight = SV_ReadLong();

    P_InventoryEmpty(plrnum);
    for(i = 0; i < getPlayerHeader()->numInvItemTypes; ++i)
    {
        inventoryitemtype_t type = SV_ReadLong();
        int             j, count = SV_ReadLong();

        for(j = 0; j < count; ++j)
            P_InventoryGive(plrnum, type, true);
    }

    P_InventorySetReadyItem(plrnum, (inventoryitemtype_t) SV_ReadLong());
    Hu_InventorySelect(plrnum, P_InventoryReadyItem(plrnum));
    if(ver < 5)
    {
        SV_ReadLong(); // Current inventory item count?
    }
    if(ver < 6)
    /*p->inventorySlotNum =*/ SV_ReadLong();

    p->chickenPeck = SV_ReadLong();
# endif
#endif

#if __JHERETIC__ || __JHEXEN__
    p->morphTics = SV_ReadLong();
#endif

    if(ver >= 2)
        p->airCounter = SV_ReadLong();

#if __JHEXEN__
    p->jumpTics = SV_ReadLong();
    p->worldTimer = SV_ReadLong();
#elif __JHERETIC__
    p->flameCount = SV_ReadLong();

    if(ver >= 2)
        p->class_ = SV_ReadByte();
#endif

#if !__JHEXEN__
    // Will be set when unarc thinker.
    p->plr->mo = NULL;
    p->attacker = NULL;
#endif

    // Demangle it.
    for(i = 0; i < numPSprites; ++i)
        if(p->pSprites[i].state)
        {
            p->pSprites[i].state = &STATES[PTR2INT(p->pSprites[i].state)];
        }

    // Mark the player for fixpos and fixangles.
    dp->flags |= DDPF_FIXORIGIN | DDPF_FIXANGLES | DDPF_FIXMOM;
    p->update |= PSF_REBORN;
}

#if __JHEXEN__
# define MOBJ_SAVEVERSION 8
#elif __JHERETIC__
# define MOBJ_SAVEVERSION 10
#else
# define MOBJ_SAVEVERSION 10
#endif

static void SV_WriteMobj(const mobj_t* original)
{
    mobj_t              temp, *mo = &temp;

    memcpy(mo, original, sizeof(*mo));
    // Mangle it!
    mo->state = (state_t *) (mo->state - STATES);
    if(mo->player)
        mo->player = (player_t *) ((mo->player - players) + 1);

    // Version.
    // JHEXEN
    // 2: Added the 'translucency' byte.
    // 3: Added byte 'vistarget'
    // 4: Added long 'tracer'
    // 4: Added long 'lastenemy'
    // 5: Added flags3
    // 6: Floor material removed.
    //
    // JDOOM || JHERETIC || JDOOM64
    // 4: Added byte 'translucency'
    // 5: Added byte 'vistarget'
    // 5: Added tracer in jDoom
    // 5: Added dropoff fix in jHeretic
    // 5: Added long 'floorclip'
    // 6: Added proper respawn data
    // 6: Added flags 2 in jDoom
    // 6: Added damage
    // 7: Added generator in jHeretic
    // 7: Added flags3
    //
    // JDOOM
    // 9: Revised mapspot flag interpretation
    //
    // JHERETIC
    // 8: Added special3
    // 9: Revised mapspot flag interpretation
    //
    // JHEXEN
    // 7: Removed superfluous info ptr
    // 8: Added 'onMobj'
    SV_WriteByte(MOBJ_SAVEVERSION);

#if !__JHEXEN__
    // A version 2 features: archive number and target.
    SV_WriteShort(SV_ThingArchiveNum((mobj_t*) original));
    SV_WriteShort(SV_ThingArchiveNum(mo->target));

# if __JDOOM__ || __JDOOM64__
    // Ver 5 features: Save tracer (fixes Archvile, Revenant bug)
    SV_WriteShort(SV_ThingArchiveNum(mo->tracer));
# endif
#endif

    SV_WriteShort(SV_ThingArchiveNum(mo->onMobj));

    // Info for drawing: position.
    SV_WriteLong(FLT2FIX(mo->origin[VX]));
    SV_WriteLong(FLT2FIX(mo->origin[VY]));
    SV_WriteLong(FLT2FIX(mo->origin[VZ]));

    //More drawing info: to determine current sprite.
    SV_WriteLong(mo->angle); // Orientation.
    SV_WriteLong(mo->sprite); // Used to find patch_t and flip value.
    SV_WriteLong(mo->frame);

#if !__JHEXEN__
    // The closest interval over all contacted Sectors.
    SV_WriteLong(FLT2FIX(mo->floorZ));
    SV_WriteLong(FLT2FIX(mo->ceilingZ));
#endif

    // For movement checking.
    SV_WriteLong(FLT2FIX(mo->radius));
    SV_WriteLong(FLT2FIX(mo->height));

    // Momentums, used to update position.
    SV_WriteLong(FLT2FIX(mo->mom[MX]));
    SV_WriteLong(FLT2FIX(mo->mom[MY]));
    SV_WriteLong(FLT2FIX(mo->mom[MZ]));

    // If == VALIDCOUNT, already checked.
    SV_WriteLong(mo->valid);

    SV_WriteLong(mo->type);
    SV_WriteLong(mo->tics); // State tic counter.
    SV_WriteLong(PTR2INT(mo->state));

#if __JHEXEN__
    SV_WriteLong(mo->damage);
#endif

    SV_WriteLong(mo->flags);
#if __JHEXEN__
    SV_WriteLong(mo->flags2);
    SV_WriteLong(mo->flags3);

    if(mo->type == MT_KORAX)
        SV_WriteLong(0); // Searching index.
    else
        SV_WriteLong(mo->special1);

    switch(mo->type)
    {
    case MT_LIGHTNING_FLOOR:
    case MT_LIGHTNING_ZAP:
    case MT_HOLY_TAIL:
    case MT_LIGHTNING_CEILING:
        if(mo->flags & MF_CORPSE)
            SV_WriteLong(0);
        else
            SV_WriteLong(SV_ThingArchiveNum(INT2PTR(mobj_t, mo->special2)));
        break;

    default:
        SV_WriteLong(mo->special2);
        break;
    }
#endif
    SV_WriteLong(mo->health);

    // Movement direction, movement generation (zig-zagging).
    SV_WriteLong(mo->moveDir); // 0-7
    SV_WriteLong(mo->moveCount); // When 0, select a new dir.

#if __JHEXEN__
    if(mo->flags & MF_CORPSE)
        SV_WriteLong(0);
    else
        SV_WriteLong((int) SV_ThingArchiveNum(mo->target));
#endif

    // Reaction time: if non 0, don't attack yet.
    // Used by player to freeze a bit after teleporting.
    SV_WriteLong(mo->reactionTime);

    // If >0, the target will be chased no matter what (even if shot).
    SV_WriteLong(mo->threshold);

    // Additional info record for player avatars only (only valid if type
    // == MT_PLAYER).
    SV_WriteLong(PTR2INT(mo->player));

    // Player number last looked for.
    SV_WriteLong(mo->lastLook);

#if !__JHEXEN__
    // For nightmare/multiplayer respawn.
    SV_WriteLong(FLT2FIX(mo->spawnSpot.origin[VX]));
    SV_WriteLong(FLT2FIX(mo->spawnSpot.origin[VY]));
    SV_WriteLong(FLT2FIX(mo->spawnSpot.origin[VZ]));
    SV_WriteLong(mo->spawnSpot.angle);
    SV_WriteLong(mo->spawnSpot.flags);

    SV_WriteLong(mo->intFlags); // $dropoff_fix: internal flags.
    SV_WriteLong(FLT2FIX(mo->dropOffZ)); // $dropoff_fix
    SV_WriteLong(mo->gear); // Used in torque simulation.

    SV_WriteLong(mo->damage);
    SV_WriteLong(mo->flags2);
    SV_WriteLong(mo->flags3);
# ifdef __JHERETIC__
    SV_WriteLong(mo->special1);
    SV_WriteLong(mo->special2);
    SV_WriteLong(mo->special3);
# endif

    SV_WriteByte(mo->translucency);
    SV_WriteByte((byte)(mo->visTarget +1));
#endif

    SV_WriteLong(FLT2FIX(mo->floorClip));
#if __JHEXEN__
    SV_WriteLong(SV_ThingArchiveNum((mobj_t*) original));
    SV_WriteLong(mo->tid);
    SV_WriteLong(mo->special);
    SV_Write(mo->args, sizeof(mo->args));
    SV_WriteByte(mo->translucency);
    SV_WriteByte((byte)(mo->visTarget +1));

    switch(mo->type)
    {
    case MT_BISH_FX:
    case MT_HOLY_FX:
    case MT_DRAGON:
    case MT_THRUSTFLOOR_UP:
    case MT_THRUSTFLOOR_DOWN:
    case MT_MINOTAUR:
    case MT_SORCFX1:
    case MT_MSTAFF_FX2:
    case MT_HOLY_TAIL:
    case MT_LIGHTNING_CEILING:
        if(mo->flags & MF_CORPSE)
            SV_WriteLong(0);
        else
            SV_WriteLong(SV_ThingArchiveNum(mo->tracer));
        break;

    default:
        DENG_ASSERT(mo->tracer == NULL); /// @todo Tracer won't be saved correctly?
        SV_WriteLong(PTR2INT(mo->tracer));
        break;
    }

    SV_WriteLong(PTR2INT(mo->lastEnemy));
#elif __JHERETIC__
    // Ver 7 features: generator
    SV_WriteShort(SV_ThingArchiveNum(mo->generator));
#endif
}

#if !__JDOOM64__
void SV_TranslateLegacyMobjFlags(mobj_t* mo, int ver)
{
#if __JDOOM__ || __JHERETIC__
    if(ver < 6)
    {
        // mobj.flags
# if __JDOOM__
        // switched values for MF_BRIGHTSHADOW <> MF_BRIGHTEXPLODE
        if((mo->flags & MF_BRIGHTEXPLODE) != (mo->flags & MF_BRIGHTSHADOW))
        {
            if(mo->flags & MF_BRIGHTEXPLODE) // previously MF_BRIGHTSHADOW
            {
                mo->flags |= MF_BRIGHTSHADOW;
                mo->flags &= ~MF_BRIGHTEXPLODE;
            }
            else // previously MF_BRIGHTEXPLODE
            {
                mo->flags |= MF_BRIGHTEXPLODE;
                mo->flags &= ~MF_BRIGHTSHADOW;
            }
        } // else they were both on or off so it doesn't matter.
# endif
        // Remove obsoleted flags in earlier save versions.
        mo->flags &= ~MF_V6OBSOLETE;

        // mobj.flags2
# if __JDOOM__
        // jDoom only gained flags2 in ver 6 so all we can do is to
        // apply the values as set in the mobjinfo.
        // Non-persistent flags might screw things up a lot worse otherwise.
        mo->flags2 = mo->info->flags2;
# endif
    }
#endif

#if __JDOOM__ || __JHERETIC__
    if(ver < 9)
    {
        mo->spawnSpot.flags &= ~MASK_UNKNOWN_MSF_FLAGS;
        // Spawn on the floor by default unless the mobjtype flags override.
        mo->spawnSpot.flags |= MSF_Z_FLOOR;
    }
#endif

#if __JHEXEN__
    if(ver < 5)
#else
    if(ver < 7)
#endif
    {
        // flags3 was introduced in a latter version so all we can do is to
        // apply the values as set in the mobjinfo.
        // Non-persistent flags might screw things up a lot worse otherwise.
        mo->flags3 = mo->info->flags3;
    }
}
#endif

static void RestoreMobj(mobj_t *mo, int ver)
{
    mo->info = &MOBJINFO[mo->type];

    P_MobjSetState(mo, PTR2INT(mo->state));
#if __JHEXEN__
    if(mo->flags2 & MF2_DORMANT)
        mo->tics = -1;
#endif

    if(mo->player)
    {
        // The player number translation table is used to find out the
        // *current* (actual) player number of the referenced player.
        int     pNum = saveToRealPlayerNum[PTR2INT(mo->player) - 1];

#if __JHEXEN__
        if(pNum < 0)
        {
            // This saved player does not exist in the current game!
            // This'll make the mobj unarchiver destroy this mobj.
            Z_Free(mo);

            return;  // Don't add this thinker.
        }
#endif

        mo->player = &players[pNum];
        mo->dPlayer = mo->player->plr;
        mo->dPlayer->mo = mo;
        //mo->dPlayer->clAngle = mo->angle; /* $unifiedangles */
        mo->dPlayer->lookDir = 0; /* $unifiedangles */
    }

    mo->visAngle = mo->angle >> 16;

#if !__JHEXEN__
    if(mo->dPlayer && !mo->dPlayer->inGame)
    {
        if(mo->dPlayer)
            mo->dPlayer->mo = NULL;
        P_MobjDestroy(mo);

        return;
    }
#endif

#if !__JDOOM64__
    // Do we need to update this mobj's flag values?
    if(ver < MOBJ_SAVEVERSION)
        SV_TranslateLegacyMobjFlags(mo, ver);
#endif

    P_MobjSetOrigin(mo);
    mo->floorZ   = P_GetDoublep(mo->bspLeaf, DMU_FLOOR_HEIGHT);
    mo->ceilingZ = P_GetDoublep(mo->bspLeaf, DMU_CEILING_HEIGHT);

    return;
}

/**
 * Always returns @c false as a thinker will have already been allocated in
 * the mobj creation process.
 */
static int SV_ReadMobj(thinker_t* th)
{
    int ver;
    mobj_t* mo = (mobj_t*) th;

    ver = SV_ReadByte();

#if !__JHEXEN__
    if(ver >= 2) // Version 2 has mobj archive numbers.
        SV_SetArchiveThing(mo, SV_ReadShort());
#endif

#if !__JHEXEN__
    mo->target = NULL;
    if(ver >= 2)
    {
        mo->target = INT2PTR(mobj_t, SV_ReadShort());
    }
#endif

#if __JDOOM__ || __JDOOM64__
    // Tracer for enemy attacks (updated after all mobjs are loaded).
    mo->tracer = NULL;
    if(ver >= 5)
    {
        mo->tracer = INT2PTR(mobj_t, SV_ReadShort());
    }
#endif

    // mobj this one is on top of (updated after all mobjs are loaded).
    mo->onMobj = NULL;
#if __JHEXEN__
    if(ver >= 8)
#else
    if(ver >= 5)
#endif
    {
        mo->onMobj = INT2PTR(mobj_t, SV_ReadShort());
    }

    // Info for drawing: position.
    mo->origin[VX] = FIX2FLT(SV_ReadLong());
    mo->origin[VY] = FIX2FLT(SV_ReadLong());
    mo->origin[VZ] = FIX2FLT(SV_ReadLong());

    //More drawing info: to determine current sprite.
    mo->angle = SV_ReadLong();  // orientation
    mo->sprite = SV_ReadLong(); // used to find patch_t and flip value
    mo->frame = SV_ReadLong();  // might be ORed with FF_FULLBRIGHT
    if(mo->frame & FF_FULLBRIGHT)
        mo->frame &= FF_FRAMEMASK; // not used anymore.

#if __JHEXEN__
    if(ver < 6)
        SV_ReadLong(); // Used to be floorflat.
#else
    // The closest interval over all contacted Sectors.
    mo->floorZ = FIX2FLT(SV_ReadLong());
    mo->ceilingZ = FIX2FLT(SV_ReadLong());
#endif

    // For movement checking.
    mo->radius = FIX2FLT(SV_ReadLong());
    mo->height = FIX2FLT(SV_ReadLong());

    // Momentums, used to update position.
    mo->mom[MX] = FIX2FLT(SV_ReadLong());
    mo->mom[MY] = FIX2FLT(SV_ReadLong());
    mo->mom[MZ] = FIX2FLT(SV_ReadLong());

    // If == VALIDCOUNT, already checked.
    mo->valid = SV_ReadLong();
    mo->type = SV_ReadLong();
#if __JHEXEN__
    if(ver < 7)
        /*mo->info = (mobjinfo_t *)*/ SV_ReadLong();
#endif
    mo->info = &MOBJINFO[mo->type];

    if(mo->info->flags2 & MF2_FLOATBOB)
        mo->mom[MZ] = 0;

    if(mo->info->flags & MF_SOLID)
        mo->ddFlags |= DDMF_SOLID;
    if(mo->info->flags2 & MF2_DONTDRAW)
        mo->ddFlags |= DDMF_DONTDRAW;

    mo->tics = SV_ReadLong();   // state tic counter
    mo->state = (state_t *) SV_ReadLong();

#if __JHEXEN__
    mo->damage = SV_ReadLong();
#endif

    mo->flags = SV_ReadLong();

#if __JHEXEN__
    mo->flags2 = SV_ReadLong();
    if(ver >= 5)
        mo->flags3 = SV_ReadLong();
    mo->special1 = SV_ReadLong();
    mo->special2 = SV_ReadLong();
#endif

    mo->health = SV_ReadLong();
#if __JHERETIC__
    if(ver < 8)
    {
        // Fix a bunch of kludges in the original Heretic.
        switch(mo->type)
        {
        case MT_MACEFX1:
        case MT_MACEFX2:
        case MT_MACEFX3:
        case MT_HORNRODFX2:
        case MT_HEADFX3:
        case MT_WHIRLWIND:
        case MT_TELEGLITTER:
        case MT_TELEGLITTER2:
            mo->special3 = mo->health;
            if(mo->type == MT_HORNRODFX2 && mo->special3 > 16)
                mo->special3 = 16;
            mo->health = MOBJINFO[mo->type].spawnHealth;
            break;

        default:
            break;
        }
    }
#endif

    // Movement direction, movement generation (zig-zagging).
    mo->moveDir = SV_ReadLong();    // 0-7
    mo->moveCount = SV_ReadLong();  // when 0, select a new dir

#if __JHEXEN__
    mo->target = (mobj_t *) SV_ReadLong();
#endif

    // Reaction time: if non 0, don't attack yet.
    // Used by player to freeze a bit after teleporting.
    mo->reactionTime = SV_ReadLong();

    // If >0, the target will be chased
    // no matter what (even if shot)
    mo->threshold = SV_ReadLong();

    // Additional info record for player avatars only.
    // Only valid if type == MT_PLAYER
    mo->player = (player_t *) SV_ReadLong();

    // Player number last looked for.
    mo->lastLook = SV_ReadLong();

#if __JHEXEN__
    mo->floorClip = FIX2FLT(SV_ReadLong());
    SV_SetArchiveThing(mo, SV_ReadLong());
    mo->tid = SV_ReadLong();
#else
    // For nightmare respawn.
    if(ver >= 6)
    {
        mo->spawnSpot.origin[VX] = FIX2FLT(SV_ReadLong());
        mo->spawnSpot.origin[VY] = FIX2FLT(SV_ReadLong());
        mo->spawnSpot.origin[VZ] = FIX2FLT(SV_ReadLong());
        mo->spawnSpot.angle = SV_ReadLong();
        if(ver < 10)
        /* mo->spawnSpot.type = */ SV_ReadLong();
        mo->spawnSpot.flags = SV_ReadLong();
    }
    else
    {
        mo->spawnSpot.origin[VX] = (float) SV_ReadShort();
        mo->spawnSpot.origin[VY] = (float) SV_ReadShort();
        mo->spawnSpot.origin[VZ] = 0; // Initialize with "something".
        mo->spawnSpot.angle = (angle_t) (ANG45 * (SV_ReadShort() / 45));
        /*mo->spawnSpot.type = (int)*/ SV_ReadShort();
        mo->spawnSpot.flags = (int) SV_ReadShort();
    }

# if __JDOOM__ || __JDOOM64__
    if(ver >= 3)
# elif __JHERETIC__
    if(ver >= 5)
# endif
    {
        mo->intFlags = SV_ReadLong();   // killough $dropoff_fix: internal flags
        mo->dropOffZ = FIX2FLT(SV_ReadLong());   // killough $dropoff_fix
        mo->gear = SV_ReadLong();   // killough used in torque simulation
    }

# if __JDOOM__ || __JDOOM64__
    if(ver >= 6)
    {
        mo->damage = SV_ReadLong();
        mo->flags2 = SV_ReadLong();
    }// Else flags2 will be applied from the defs.
    else
        mo->damage = DDMAXINT; // Use the value set in mo->info->damage

# elif __JHERETIC__
    mo->damage = SV_ReadLong();
    mo->flags2 = SV_ReadLong();
# endif

    if(ver >= 7)
        mo->flags3 = SV_ReadLong();
    // Else flags3 will be applied from the defs.
#endif

#if __JHEXEN__
    mo->special = SV_ReadLong();
    SV_Read(mo->args, 1 * 5);
#elif __JHERETIC__
    mo->special1 = SV_ReadLong();
    mo->special2 = SV_ReadLong();
    if(ver >= 8)
        mo->special3 = SV_ReadLong();
#endif

#if __JHEXEN__
    if(ver >= 2)
#else
    if(ver >= 4)
#endif
        mo->translucency = SV_ReadByte();

#if __JHEXEN__
    if(ver >= 3)
#else
    if(ver >= 5)
#endif
        mo->visTarget = (short) (SV_ReadByte()) -1;

#if __JHEXEN__
    if(ver >= 4)
        mo->tracer = (mobj_t *) SV_ReadLong();

    if(ver >= 4)
        mo->lastEnemy = (mobj_t *) SV_ReadLong();
#else
    if(ver >= 5)
        mo->floorClip = FIX2FLT(SV_ReadLong());
#endif

#if __JHERETIC__
    if(ver >= 7)
        mo->generator = INT2PTR(mobj_t, SV_ReadShort());
    else
        mo->generator = NULL;
#endif

    // Restore! (unmangle)
    RestoreMobj(mo, ver);

    return false;
}

/**
 * Prepare and write the player header info.
 */
static void P_ArchivePlayerHeader(void)
{
    playerheader_t *ph = &playerHeader;

    SV_BeginSegment(ASEG_PLAYER_HEADER);
    SV_WriteByte(2); // version byte

    ph->numPowers = NUM_POWER_TYPES;
    ph->numKeys = NUM_KEY_TYPES;
    ph->numFrags = MAXPLAYERS;
    ph->numWeapons = NUM_WEAPON_TYPES;
    ph->numAmmoTypes = NUM_AMMO_TYPES;
    ph->numPSprites = NUMPSPRITES;
#if __JHERETIC__ || __JHEXEN__ || __JDOOM64__
    ph->numInvItemTypes = NUM_INVENTORYITEM_TYPES;
#endif
#if __JHEXEN__
    ph->numArmorTypes = NUMARMOR;
#endif

    SV_WriteLong(ph->numPowers);
    SV_WriteLong(ph->numKeys);
    SV_WriteLong(ph->numFrags);
    SV_WriteLong(ph->numWeapons);
    SV_WriteLong(ph->numAmmoTypes);
    SV_WriteLong(ph->numPSprites);
#if __JDOOM64__ || __JHERETIC__ || __JHEXEN__
    SV_WriteLong(ph->numInvItemTypes);
#endif
#if __JHEXEN__
    SV_WriteLong(ph->numArmorTypes);
#endif

    playerHeaderOK = true;
}

/**
 * Read archived player header info.
 */
static void P_UnArchivePlayerHeader(void)
{
#if __JHEXEN__
    if(hdr->version >= 4)
#else
    if(hdr->version >= 5)
#endif
    {
        int     ver;

        SV_AssertSegment(ASEG_PLAYER_HEADER);
        ver = SV_ReadByte();

        playerHeader.numPowers = SV_ReadLong();
        playerHeader.numKeys = SV_ReadLong();
        playerHeader.numFrags = SV_ReadLong();
        playerHeader.numWeapons = SV_ReadLong();
        playerHeader.numAmmoTypes = SV_ReadLong();
        playerHeader.numPSprites = SV_ReadLong();
#if __JHERETIC__
        if(ver >= 2)
            playerHeader.numInvItemTypes = SV_ReadLong();
        else
            playerHeader.numInvItemTypes = NUM_INVENTORYITEM_TYPES;
#endif
#if __JHEXEN__ || __JDOOM64__
        playerHeader.numInvItemTypes = SV_ReadLong();
#endif
#if __JHEXEN__
        playerHeader.numArmorTypes = SV_ReadLong();
#endif
    }
    else // The old format didn't save the counts.
    {
#if __JHEXEN__
        playerHeader.numPowers = 9;
        playerHeader.numKeys = 11;
        playerHeader.numFrags = 8;
        playerHeader.numWeapons = 4;
        playerHeader.numAmmoTypes = 2;
        playerHeader.numPSprites = 2;
        playerHeader.numInvItemTypes = 33;
        playerHeader.numArmorTypes = 4;
#elif __JDOOM__ || __JDOOM64__
        playerHeader.numPowers = 6;
        playerHeader.numKeys = 6;
        playerHeader.numFrags = 4; // Why was this only 4?
        playerHeader.numWeapons = 9;
        playerHeader.numAmmoTypes = 4;
        playerHeader.numPSprites = 2;
# if __JDOOM64__
        playerHeader.numInvItemTypes = 3;
# endif
#elif __JHERETIC__
        playerHeader.numPowers = 9;
        playerHeader.numKeys = 3;
        playerHeader.numFrags = 4; // ?
        playerHeader.numWeapons = 8;
        playerHeader.numInvItemTypes = 14;
        playerHeader.numAmmoTypes = 6;
        playerHeader.numPSprites = 2;
#endif
    }
    playerHeaderOK = true;
}

static void P_ArchivePlayers(void)
{
    int                 i;

    SV_BeginSegment(ASEG_PLAYERS);
#if __JHEXEN__
    for(i = 0; i < MAXPLAYERS; ++i)
    {
        SV_WriteByte(players[i].plr->inGame);
    }
#endif

    for(i = 0; i < MAXPLAYERS; ++i)
    {
        if(!players[i].plr->inGame)
            continue;

        SV_WriteLong(Net_GetPlayerID(i));
        SV_WritePlayer(i);
    }
}

static void P_UnArchivePlayers(boolean* infile, boolean* loaded)
{
    ddplayer_t dummyDDPlayer;
    player_t dummyPlayer;
    player_t* player;
    int i, j, pid;

    DENG_ASSERT(infile && loaded);

    // Setup the dummy.
    dummyPlayer.plr = &dummyDDPlayer;

    for(i = 0; i < MAXPLAYERS; ++i)
    {
        loaded[i] = 0;
#if !__JHEXEN__
        infile[i] = hdr->players[i];
#endif
    }

    SV_AssertSegment(ASEG_PLAYERS);
#if __JHEXEN__
    for(i = 0; i < MAXPLAYERS; ++i)
    {
        infile[i] = SV_ReadByte();
    }
#endif

    // Load the players.
    for(i = 0; i < MAXPLAYERS; ++i)
    {
        // By default a saved player translates to nothing.
        saveToRealPlayerNum[i] = -1;

        if(!infile[i]) continue;

        // The ID number will determine which player this actually is.
        pid = SV_ReadLong();
        player = 0;
        for(j = 0; j < MAXPLAYERS; ++j)
        {
            if((IS_NETGAME && Net_GetPlayerID(j) == pid) ||
               (!IS_NETGAME && j == 0))
            {
                // This is our guy.
                player = players + j;
                loaded[j] = true;
                // Later references to the player number 'i' must be translated!
                saveToRealPlayerNum[i] = j;
#if _DEBUG
                Con_Printf("P_UnArchivePlayers: Saved %i is now %i.\n", i, j);
#endif
                break;
            }
        }

        if(!player)
        {
            // We have a missing player. Use a dummy to load the data.
            player = &dummyPlayer;
        }

        // Read the data.
        SV_ReadPlayer(player);
    }

    SV_AssertSegment(ASEG_END);
}

static void SV_WriteSector(Sector *sec)
{
    int i, type;
    float flooroffx = P_GetFloatp(sec, DMU_FLOOR_MATERIAL_OFFSET_X);
    float flooroffy = P_GetFloatp(sec, DMU_FLOOR_MATERIAL_OFFSET_Y);
    float ceiloffx = P_GetFloatp(sec, DMU_CEILING_MATERIAL_OFFSET_X);
    float ceiloffy = P_GetFloatp(sec, DMU_CEILING_MATERIAL_OFFSET_Y);
    byte lightlevel = (byte) (255.f * P_GetFloatp(sec, DMU_LIGHT_LEVEL));
    short floorheight = (short) P_GetIntp(sec, DMU_FLOOR_HEIGHT);
    short ceilingheight = (short) P_GetIntp(sec, DMU_CEILING_HEIGHT);
    short floorFlags = (short) P_GetIntp(sec, DMU_FLOOR_FLAGS);
    short ceilingFlags = (short) P_GetIntp(sec, DMU_CEILING_FLAGS);
    Material* floorMaterial = P_GetPtrp(sec, DMU_FLOOR_MATERIAL);
    Material* ceilingMaterial = P_GetPtrp(sec, DMU_CEILING_MATERIAL);
    xsector_t* xsec = P_ToXSector(sec);
    float rgb[3];

#if !__JHEXEN__
    // Determine type.
    if(xsec->xg)
        type = sc_xg1;
    else
#endif
        if(!FEQUAL(flooroffx, 0) || !FEQUAL(flooroffy, 0) || !FEQUAL(ceiloffx, 0) || !FEQUAL(ceiloffy, 0))
        type = sc_ploff;
    else
        type = sc_normal;

    // Type byte.
    SV_WriteByte(type);

    // Version.
    // 2: Surface colors.
    // 3: Surface flags.
    SV_WriteByte(3); // write a version byte.

    SV_WriteShort(floorheight);
    SV_WriteShort(ceilingheight);
    SV_WriteShort(MaterialArchive_FindUniqueSerialId(materialArchive, floorMaterial));
    SV_WriteShort(MaterialArchive_FindUniqueSerialId(materialArchive, ceilingMaterial));
    SV_WriteShort(floorFlags);
    SV_WriteShort(ceilingFlags);
#if __JHEXEN__
    SV_WriteShort((short) lightlevel);
#else
    SV_WriteByte(lightlevel);
#endif

    P_GetFloatpv(sec, DMU_COLOR, rgb);
    for(i = 0; i < 3; ++i)
        SV_WriteByte((byte)(255.f * rgb[i]));

    P_GetFloatpv(sec, DMU_FLOOR_COLOR, rgb);
    for(i = 0; i < 3; ++i)
        SV_WriteByte((byte)(255.f * rgb[i]));

    P_GetFloatpv(sec, DMU_CEILING_COLOR, rgb);
    for(i = 0; i < 3; ++i)
        SV_WriteByte((byte)(255.f * rgb[i]));

    SV_WriteShort(xsec->special);
    SV_WriteShort(xsec->tag);

#if __JHEXEN__
    SV_WriteShort(xsec->seqType);
#endif

    if(type == sc_ploff
#if !__JHEXEN__
       || type == sc_xg1
#endif
       )
    {
        SV_WriteFloat(flooroffx);
        SV_WriteFloat(flooroffy);
        SV_WriteFloat(ceiloffx);
        SV_WriteFloat(ceiloffy);
    }

#if !__JHEXEN__
    if(xsec->xg)                 // Extended General?
    {
        SV_WriteXGSector(sec);
    }

    // Count the number of sound targets
    if(xsec->soundTarget)
        numSoundTargets++;
#endif
}

/**
 * Reads all versions of archived sectors.
 * Including the old Ver1.
 */
static void SV_ReadSector(Sector* sec)
{
    int i, ver = 1;
    int type = 0;
    Material* floorMaterial = NULL, *ceilingMaterial = NULL;
    byte rgb[3], lightlevel;
    xsector_t* xsec = P_ToXSector(sec);
    int fh, ch;

    // A type byte?
#if __JHEXEN__
    if(mapVersion < 4)
        type = sc_ploff;
    else
#else
    if(hdr->version <= 1)
        type = sc_normal;
    else
#endif
        type = SV_ReadByte();

    // A version byte?
#if __JHEXEN__
    if(mapVersion > 2)
#else
    if(hdr->version > 4)
#endif
        ver = SV_ReadByte();

    fh = SV_ReadShort();
    ch = SV_ReadShort();

    P_SetIntp(sec, DMU_FLOOR_HEIGHT, fh);
    P_SetIntp(sec, DMU_CEILING_HEIGHT, ch);
#if __JHEXEN__
    // Update the "target heights" of the planes.
    P_SetIntp(sec, DMU_FLOOR_TARGET_HEIGHT, fh);
    P_SetIntp(sec, DMU_CEILING_TARGET_HEIGHT, ch);
    // The move speed is not saved; can cause minor problems.
    P_SetIntp(sec, DMU_FLOOR_SPEED, 0);
    P_SetIntp(sec, DMU_CEILING_SPEED, 0);
#endif

#if !__JHEXEN__
    if(hdr->version == 1)
    {
        // The flat numbers are absolute lump indices.
        Uri* uri = Uri_NewWithPath2("Flats:", RC_NULL);
        Uri_SetPath(uri, Str_Text(W_LumpName(SV_ReadShort())));
        floorMaterial = P_ToPtr(DMU_MATERIAL, Materials_ResolveUri(uri));

        Uri_SetPath(uri, Str_Text(W_LumpName(SV_ReadShort())));
        ceilingMaterial = P_ToPtr(DMU_MATERIAL, Materials_ResolveUri(uri));
        Uri_Delete(uri);
    }
    else if(hdr->version >= 4)
#endif
    {
        // The flat numbers are actually archive numbers.
        floorMaterial   = SV_GetArchiveMaterial(SV_ReadShort(), 0);
        ceilingMaterial = SV_GetArchiveMaterial(SV_ReadShort(), 0);
    }

    P_SetPtrp(sec, DMU_FLOOR_MATERIAL,   floorMaterial);
    P_SetPtrp(sec, DMU_CEILING_MATERIAL, ceilingMaterial);

    if(ver >= 3)
    {
        P_SetIntp(sec, DMU_FLOOR_FLAGS,   SV_ReadShort());
        P_SetIntp(sec, DMU_CEILING_FLAGS, SV_ReadShort());
    }

#if __JHEXEN__
    lightlevel = (byte) SV_ReadShort();
#else
    // In Ver1 the light level is a short
    if(hdr->version == 1)
        lightlevel = (byte) SV_ReadShort();
    else
        lightlevel = SV_ReadByte();
#endif
    P_SetFloatp(sec, DMU_LIGHT_LEVEL, (float) lightlevel / 255.f);

#if !__JHEXEN__
    if(hdr->version > 1)
#endif
    {
        SV_Read(rgb, 3);
        for(i = 0; i < 3; ++i)
            P_SetFloatp(sec, DMU_COLOR_RED + i, rgb[i] / 255.f);
    }

    // Ver 2 includes surface colours
    if(ver >= 2)
    {
        SV_Read(rgb, 3);
        for(i = 0; i < 3; ++i)
            P_SetFloatp(sec, DMU_FLOOR_COLOR_RED + i, rgb[i] / 255.f);

        SV_Read(rgb, 3);
        for(i = 0; i < 3; ++i)
            P_SetFloatp(sec, DMU_CEILING_COLOR_RED + i, rgb[i] / 255.f);
    }

    xsec->special = SV_ReadShort();
    /*xsec->tag =*/ SV_ReadShort();

#if __JHEXEN__
    xsec->seqType = SV_ReadShort();
#endif

    if(type == sc_ploff
#if !__JHEXEN__
       || type == sc_xg1
#endif
       )
    {
        P_SetFloatp(sec, DMU_FLOOR_MATERIAL_OFFSET_X, SV_ReadFloat());
        P_SetFloatp(sec, DMU_FLOOR_MATERIAL_OFFSET_Y, SV_ReadFloat());
        P_SetFloatp(sec, DMU_CEILING_MATERIAL_OFFSET_X, SV_ReadFloat());
        P_SetFloatp(sec, DMU_CEILING_MATERIAL_OFFSET_Y, SV_ReadFloat());
    }

#if !__JHEXEN__
    if(type == sc_xg1)
        SV_ReadXGSector(sec);
#endif

#if !__JHEXEN__
    if(hdr->version <= 1)
#endif
    {
        xsec->specialData = 0;
    }

    // We'll restore the sound targets latter on
    xsec->soundTarget = 0;
}

static void SV_WriteLine(Line* li)
{
    uint                i, j;
    float               rgba[4];
    lineclass_t         type;
    xline_t*            xli = P_ToXLine(li);

#if !__JHEXEN__
    if(xli->xg)
        type =  lc_xg1;
    else
#endif
        type = lc_normal;
    SV_WriteByte(type);

    // Version.
    // 2: Per surface texture offsets.
    // 2: Surface colors.
    // 3: "Mapped by player" values.
    // 3: Surface flags.
    // 4: Engine-side line flags.
    SV_WriteByte(4); // Write a version byte

    SV_WriteShort(P_GetIntp(li, DMU_FLAGS));
    SV_WriteShort(xli->flags);

    for(i = 0; i < MAXPLAYERS; ++i)
        SV_WriteByte(xli->mapped[i]);

#if __JHEXEN__
    SV_WriteByte(xli->special);
    SV_WriteByte(xli->arg1);
    SV_WriteByte(xli->arg2);
    SV_WriteByte(xli->arg3);
    SV_WriteByte(xli->arg4);
    SV_WriteByte(xli->arg5);
#else
    SV_WriteShort(xli->special);
    SV_WriteShort(xli->tag);
#endif

    // For each side
    for(i = 0; i < 2; ++i)
    {
        SideDef *si = P_GetPtrp(li, (i? DMU_SIDEDEF1:DMU_SIDEDEF0));
        if(!si)
            continue;

        SV_WriteShort(P_GetIntp(si, DMU_TOP_MATERIAL_OFFSET_X));
        SV_WriteShort(P_GetIntp(si, DMU_TOP_MATERIAL_OFFSET_Y));
        SV_WriteShort(P_GetIntp(si, DMU_MIDDLE_MATERIAL_OFFSET_X));
        SV_WriteShort(P_GetIntp(si, DMU_MIDDLE_MATERIAL_OFFSET_Y));
        SV_WriteShort(P_GetIntp(si, DMU_BOTTOM_MATERIAL_OFFSET_X));
        SV_WriteShort(P_GetIntp(si, DMU_BOTTOM_MATERIAL_OFFSET_Y));

        SV_WriteShort(P_GetIntp(si, DMU_TOP_FLAGS));
        SV_WriteShort(P_GetIntp(si, DMU_MIDDLE_FLAGS));
        SV_WriteShort(P_GetIntp(si, DMU_BOTTOM_FLAGS));

        SV_WriteShort(MaterialArchive_FindUniqueSerialId(materialArchive, P_GetPtrp(si, DMU_TOP_MATERIAL)));
        SV_WriteShort(MaterialArchive_FindUniqueSerialId(materialArchive, P_GetPtrp(si, DMU_BOTTOM_MATERIAL)));
        SV_WriteShort(MaterialArchive_FindUniqueSerialId(materialArchive, P_GetPtrp(si, DMU_MIDDLE_MATERIAL)));

        P_GetFloatpv(si, DMU_TOP_COLOR, rgba);
        for(j = 0; j < 3; ++j)
            SV_WriteByte((byte)(255 * rgba[j]));

        P_GetFloatpv(si, DMU_BOTTOM_COLOR, rgba);
        for(j = 0; j < 3; ++j)
            SV_WriteByte((byte)(255 * rgba[j]));

        P_GetFloatpv(si, DMU_MIDDLE_COLOR, rgba);
        for(j = 0; j < 4; ++j)
            SV_WriteByte((byte)(255 * rgba[j]));

        SV_WriteLong(P_GetIntp(si, DMU_MIDDLE_BLENDMODE));
        SV_WriteShort(P_GetIntp(si, DMU_FLAGS));
    }

#if !__JHEXEN__
    // Extended General?
    if(xli->xg)
    {
        SV_WriteXGLine(li);
    }
#endif
}

/**
 * Reads all versions of archived lines.
 * Including the old Ver1.
 */
static void SV_ReadLine(Line* li)
{
    int i, j;
    lineclass_t type;
    int ver;
    Material* topMaterial = NULL, *bottomMaterial = NULL, *middleMaterial = NULL;
    short flags;
    xline_t* xli = P_ToXLine(li);

    // A type byte?
#if __JHEXEN__
    if(mapVersion < 4)
#else
    if(hdr->version < 2)
#endif
        type = lc_normal;
    else
        type = (int) SV_ReadByte();

    // A version byte?
#if __JHEXEN__
    if(mapVersion < 3)
#else
    if(hdr->version < 5)
#endif
        ver = 1;
    else
        ver = (int) SV_ReadByte();

    if(ver >= 4)
        P_SetIntp(li, DMU_FLAGS, SV_ReadShort());

    flags = SV_ReadShort();

    if(ver < 4)
    {   // Translate old line flags.
        int             ddLineFlags = 0;

        if(flags & 0x0001) // old ML_BLOCKING flag
        {
            ddLineFlags |= DDLF_BLOCKING;
            flags &= ~0x0001;
        }

        if(flags & 0x0004) // old ML_TWOSIDED flag
        {
            flags &= ~0x0004;
        }

        if(flags & 0x0008) // old ML_DONTPEGTOP flag
        {
            ddLineFlags |= DDLF_DONTPEGTOP;
            flags &= ~0x0008;
        }

        if(flags & 0x0010) // old ML_DONTPEGBOTTOM flag
        {
            ddLineFlags |= DDLF_DONTPEGBOTTOM;
            flags &= ~0x0010;
        }

        P_SetIntp(li, DMU_FLAGS, ddLineFlags);
    }

    if(ver < 3)
    {
        if(flags & ML_MAPPED)
        {
            uint lineIDX = P_ToIndex(li);

            // Set line as having been seen by all players..
            memset(xli->mapped, 0, sizeof(xli->mapped));
            for(i = 0; i < MAXPLAYERS; ++i)
                P_SetLineAutomapVisibility(i, lineIDX, true);
        }
    }

    xli->flags = flags;

    if(ver >= 3)
    {
        for(i = 0; i < MAXPLAYERS; ++i)
            xli->mapped[i] = SV_ReadByte();
    }

#if __JHEXEN__
    xli->special = SV_ReadByte();
    xli->arg1 = SV_ReadByte();
    xli->arg2 = SV_ReadByte();
    xli->arg3 = SV_ReadByte();
    xli->arg4 = SV_ReadByte();
    xli->arg5 = SV_ReadByte();
#else
    xli->special = SV_ReadShort();
    /*xli->tag =*/ SV_ReadShort();
#endif

    // For each side
    for(i = 0; i < 2; ++i)
    {
        SideDef* si = P_GetPtrp(li, (i? DMU_SIDEDEF1:DMU_SIDEDEF0));

        if(!si)
            continue;

        // Versions latter than 2 store per surface texture offsets.
        if(ver >= 2)
        {
            float offset[2];

            offset[VX] = (float) SV_ReadShort();
            offset[VY] = (float) SV_ReadShort();
            P_SetFloatpv(si, DMU_TOP_MATERIAL_OFFSET_XY, offset);

            offset[VX] = (float) SV_ReadShort();
            offset[VY] = (float) SV_ReadShort();
            P_SetFloatpv(si, DMU_MIDDLE_MATERIAL_OFFSET_XY, offset);

            offset[VX] = (float) SV_ReadShort();
            offset[VY] = (float) SV_ReadShort();
            P_SetFloatpv(si, DMU_BOTTOM_MATERIAL_OFFSET_XY, offset);
        }
        else
        {
            float offset[2];

            offset[VX] = (float) SV_ReadShort();
            offset[VY] = (float) SV_ReadShort();

            P_SetFloatpv(si, DMU_TOP_MATERIAL_OFFSET_XY,    offset);
            P_SetFloatpv(si, DMU_MIDDLE_MATERIAL_OFFSET_XY, offset);
            P_SetFloatpv(si, DMU_BOTTOM_MATERIAL_OFFSET_XY, offset);
        }

        if(ver >= 3)
        {
            P_SetIntp(si, DMU_TOP_FLAGS,    SV_ReadShort());
            P_SetIntp(si, DMU_MIDDLE_FLAGS, SV_ReadShort());
            P_SetIntp(si, DMU_BOTTOM_FLAGS, SV_ReadShort());
        }

#if !__JHEXEN__
        if(hdr->version >= 4)
#endif
        {
            topMaterial    = SV_GetArchiveMaterial(SV_ReadShort(), 1);
            bottomMaterial = SV_GetArchiveMaterial(SV_ReadShort(), 1);
            middleMaterial = SV_GetArchiveMaterial(SV_ReadShort(), 1);
        }

        P_SetPtrp(si, DMU_TOP_MATERIAL,    topMaterial);
        P_SetPtrp(si, DMU_BOTTOM_MATERIAL, bottomMaterial);
        P_SetPtrp(si, DMU_MIDDLE_MATERIAL, middleMaterial);

        // Ver2 includes surface colours
        if(ver >= 2)
        {
            float rgba[4];

            for(j = 0; j < 3; ++j)
                rgba[j] = (float) SV_ReadByte() / 255.f;
            rgba[3] = 1;
            P_SetFloatpv(si, DMU_TOP_COLOR, rgba);

            for(j = 0; j < 3; ++j)
                rgba[j] = (float) SV_ReadByte() / 255.f;
            rgba[3] = 1;
            P_SetFloatpv(si, DMU_BOTTOM_COLOR, rgba);

            for(j = 0; j < 4; ++j)
                rgba[j] = (float) SV_ReadByte() / 255.f;
            P_SetFloatpv(si, DMU_MIDDLE_COLOR, rgba);

            P_SetIntp(si, DMU_MIDDLE_BLENDMODE, SV_ReadLong());
            P_SetIntp(si, DMU_FLAGS, SV_ReadShort());
        }
    }

#if !__JHEXEN__
    if(type == lc_xg1)
        SV_ReadXGLine(li);
#endif
}

#if __JHEXEN__
static void SV_WritePolyObj(Polyobj* po)
{
    SV_WriteByte(1); // write a version byte.

    SV_WriteLong(po->tag);
    SV_WriteLong(po->angle);
    SV_WriteLong(FLT2FIX(po->origin[VX]));
    SV_WriteLong(FLT2FIX(po->origin[VY]));
}

static int SV_ReadPolyObj(void)
{
    coord_t deltaX, deltaY;
    angle_t angle;
    Polyobj* po;
    int ver;

    if(mapVersion >= 3)
        ver = SV_ReadByte();

    po = P_PolyobjByTag(SV_ReadLong());
    if(!po) Con_Error("UnarchivePolyobjs: Invalid polyobj tag");

    angle = (angle_t) SV_ReadLong();
    P_PolyobjRotate(po, angle);
    po->destAngle = angle;
    deltaX = FIX2FLT(SV_ReadLong()) - po->origin[VX];
    deltaY = FIX2FLT(SV_ReadLong()) - po->origin[VY];
    P_PolyobjMoveXY(po, deltaX, deltaY);

    /// @todo What about speed? It isn't saved at all?

    return true;
}
#endif

/**
 * Only write world in the latest format.
 */
static void P_ArchiveWorld(void)
{
    uint i;

    { Writer* svWriter = SV_NewWriter();
    MaterialArchive_Write(materialArchive, svWriter);
    Writer_Delete(svWriter);
    }

    SV_BeginSegment(ASEG_WORLD);
    for(i = 0; i < numsectors; ++i)
        SV_WriteSector(P_ToPtr(DMU_SECTOR, i));

    for(i = 0; i < numlines; ++i)
        SV_WriteLine(P_ToPtr(DMU_LINE, i));

#if __JHEXEN__
    SV_BeginSegment(ASEG_POLYOBJS);
    SV_WriteLong(numpolyobjs);
    for(i = 0; i < numpolyobjs; ++i)
        SV_WritePolyObj(P_PolyobjByID(i));
#endif
}

static void P_UnArchiveWorld(void)
{
    int matArchiveVer = -1;
    uint i;

#if __JHEXEN__
    if(mapVersion < 6)
#else
    if(hdr->version < 6)
#endif
        matArchiveVer = 0;

    // Load the material archive for this map?
#if !__JHEXEN__
    if(hdr->version >= 4)
#endif
    {
        Reader* svReader = SV_NewReader();
        MaterialArchive_Read(materialArchive, svReader, matArchiveVer);
        Reader_Delete(svReader);
    }

    SV_AssertSegment(ASEG_WORLD);
    // Load sectors.
    for(i = 0; i < numsectors; ++i)
        SV_ReadSector(P_ToPtr(DMU_SECTOR, i));

    // Load lines.
    for(i = 0; i < numlines; ++i)
        SV_ReadLine(P_ToPtr(DMU_LINE, i));

#if __JHEXEN__
    // Load polyobjects.
    SV_AssertSegment(ASEG_POLYOBJS);
    if(SV_ReadLong() != numpolyobjs)
        Con_Error("UnarchivePolyobjs: Bad polyobj count");

    for(i = 0; i < numpolyobjs; ++i)
        SV_ReadPolyObj();
#endif
}

static void SV_WriteCeiling(const ceiling_t* ceiling)
{
    SV_WriteByte(2); // Write a version byte.

    SV_WriteByte((byte) ceiling->type);
    SV_WriteLong(P_ToIndex(ceiling->sector));

    SV_WriteShort((int)ceiling->bottomHeight);
    SV_WriteShort((int)ceiling->topHeight);
    SV_WriteLong(FLT2FIX(ceiling->speed));

    SV_WriteByte(ceiling->crush);

    SV_WriteByte((byte) ceiling->state);
    SV_WriteLong(ceiling->tag);
    SV_WriteByte((byte) ceiling->oldState);
}

static int SV_ReadCeiling(ceiling_t* ceiling)
{
    Sector*             sector;

#if __JHEXEN__
    if(mapVersion >= 4)
#else
    if(hdr->version >= 5)
#endif
    {   // Note: the thinker class byte has already been read.
        int                 ver = SV_ReadByte(); // version byte.

        ceiling->thinker.function = T_MoveCeiling;

#if !__JHEXEN__
        // Should we put this into stasis?
        if(hdr->version == 5)
        {
            if(!SV_ReadByte())
                Thinker_SetStasis(&ceiling->thinker, true);
        }
#endif

        ceiling->type = (ceilingtype_e) SV_ReadByte();

        sector = P_ToPtr(DMU_SECTOR, SV_ReadLong());

        if(!sector)
            Con_Error("TC_CEILING: bad sector number\n");

        ceiling->sector = sector;

        ceiling->bottomHeight = (float) SV_ReadShort();
        ceiling->topHeight = (float) SV_ReadShort();
        ceiling->speed = FIX2FLT((fixed_t) SV_ReadLong());

        ceiling->crush = SV_ReadByte();

        if(ver == 2)
            ceiling->state = SV_ReadByte();
        else
            ceiling->state = (SV_ReadLong() == -1? CS_DOWN : CS_UP);
        ceiling->tag = SV_ReadLong();
        if(ver == 2)
            ceiling->oldState = SV_ReadByte();
        else
            ceiling->state = (SV_ReadLong() == -1? CS_DOWN : CS_UP);
    }
    else
    {
        // Its in the old format which serialized ceiling_t
        // Padding at the start (an old thinker_t struct)
        thinker_t junk;
        SV_Read(&junk, (size_t) 16);

        // Start of used data members.
#if __JHEXEN__
        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, SV_ReadLong());
        if(!sector)
            Con_Error("TC_CEILING: bad sector number\n");
        ceiling->sector = sector;

        ceiling->type = SV_ReadLong();
#else
        ceiling->type = SV_ReadLong();

        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, SV_ReadLong());
        if(!sector)
            Con_Error("TC_CEILING: bad sector number\n");
        ceiling->sector = sector;
#endif

        ceiling->bottomHeight = FIX2FLT((fixed_t) SV_ReadLong());
        ceiling->topHeight = FIX2FLT((fixed_t) SV_ReadLong());
        ceiling->speed = FIX2FLT((fixed_t) SV_ReadLong());

        ceiling->crush = SV_ReadLong();
        ceiling->state = (SV_ReadLong() == -1? CS_DOWN : CS_UP);
        ceiling->tag = SV_ReadLong();
        ceiling->oldState = (SV_ReadLong() == -1? CS_DOWN : CS_UP);

        ceiling->thinker.function = T_MoveCeiling;
#if !__JHEXEN__
        if(!junk.function)
            Thinker_SetStasis(&ceiling->thinker, true);
#endif
    }

    P_ToXSector(ceiling->sector)->specialData = ceiling;
    return true; // Add this thinker.
}

static void SV_WriteDoor(const door_t *door)
{
    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteByte((byte) door->type);

    SV_WriteLong(P_ToIndex(door->sector));

    SV_WriteShort((int)door->topHeight);
    SV_WriteLong(FLT2FIX(door->speed));

    SV_WriteLong(door->state);
    SV_WriteLong(door->topWait);
    SV_WriteLong(door->topCountDown);
}

static int SV_ReadDoor(door_t *door)
{
    Sector *sector;

#if __JHEXEN__
    if(mapVersion >= 4)
#else
    if(hdr->version >= 5)
#endif
    {   // Note: the thinker class byte has already been read.
        /*int ver =*/ SV_ReadByte(); // version byte.

        door->type = (doortype_e) SV_ReadByte();

        sector = P_ToPtr(DMU_SECTOR, SV_ReadLong());

        if(!sector)
            Con_Error("TC_DOOR: bad sector number\n");

        door->sector = sector;

        door->topHeight = (float) SV_ReadShort();
        door->speed = FIX2FLT((fixed_t) SV_ReadLong());

        door->state = SV_ReadLong();
        door->topWait = SV_ReadLong();
        door->topCountDown = SV_ReadLong();
    }
    else
    {
        // Its in the old format which serialized door_t
        // Padding at the start (an old thinker_t struct)
        SV_Seek(16);

        // Start of used data members.
#if __JHEXEN__
        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_DOOR: bad sector number\n");
        door->sector = sector;

        door->type = SV_ReadLong();
#else
        door->type = SV_ReadLong();

        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_DOOR: bad sector number\n");
        door->sector = sector;
#endif
        door->topHeight = FIX2FLT((fixed_t) SV_ReadLong());
        door->speed = FIX2FLT((fixed_t) SV_ReadLong());

        door->state = SV_ReadLong();
        door->topWait = SV_ReadLong();
        door->topCountDown = SV_ReadLong();
    }

    P_ToXSector(door->sector)->specialData = door;
    door->thinker.function = T_Door;

    return true; // Add this thinker.
}

static void SV_WriteFloor(const floor_t *floor)
{
    SV_WriteByte(3); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteByte((byte) floor->type);

    SV_WriteLong(P_ToIndex(floor->sector));

    SV_WriteByte((byte) floor->crush);

    SV_WriteLong((int) floor->state);
    SV_WriteLong(floor->newSpecial);

    SV_WriteShort(MaterialArchive_FindUniqueSerialId(materialArchive, floor->material));

    SV_WriteShort((int) floor->floorDestHeight);
    SV_WriteLong(FLT2FIX(floor->speed));

#if __JHEXEN__
    SV_WriteLong(floor->delayCount);
    SV_WriteLong(floor->delayTotal);
    SV_WriteLong(FLT2FIX(floor->stairsDelayHeight));
    SV_WriteLong(FLT2FIX(floor->stairsDelayHeightDelta));
    SV_WriteLong(FLT2FIX(floor->resetHeight));
    SV_WriteShort(floor->resetDelay);
    SV_WriteShort(floor->resetDelayCount);
#endif
}

static int SV_ReadFloor(floor_t* floor)
{
    Sector*             sector;

#if __JHEXEN__
    if(mapVersion >= 4)
#else
    if(hdr->version >= 5)
#endif
    {   // Note: the thinker class byte has already been read.
        byte                ver = SV_ReadByte(); // version byte.

        floor->type = (floortype_e) SV_ReadByte();

        sector = P_ToPtr(DMU_SECTOR, SV_ReadLong());

        if(!sector)
            Con_Error("TC_FLOOR: bad sector number\n");

        floor->sector = sector;

        floor->crush = (boolean) SV_ReadByte();

        floor->state = (int) SV_ReadLong();
        floor->newSpecial = SV_ReadLong();

        if(ver >= 2)
        {
            floor->material = SV_GetArchiveMaterial(SV_ReadShort(), 0);
        }
        else
        {
            // Flat number is an absolute lump index.
            Uri* uri = Uri_NewWithPath2("Flats:", RC_NULL);
            ddstring_t name;
            Str_Init(&name);
            F_FileName(&name, Str_Text(W_LumpName(SV_ReadShort())));
            Uri_SetPath(uri, Str_Text(&name));
            floor->material = P_ToPtr(DMU_MATERIAL, Materials_ResolveUri(uri));
            Uri_Delete(uri);
            Str_Free(&name);
        }

        floor->floorDestHeight = (float) SV_ReadShort();
        floor->speed = FIX2FLT(SV_ReadLong());

#if __JHEXEN__
        floor->delayCount = SV_ReadLong();
        floor->delayTotal = SV_ReadLong();
        floor->stairsDelayHeight = FIX2FLT(SV_ReadLong());
        floor->stairsDelayHeightDelta = FIX2FLT(SV_ReadLong());
        floor->resetHeight = FIX2FLT(SV_ReadLong());
        floor->resetDelay = SV_ReadShort();
        floor->resetDelayCount = SV_ReadShort();
#endif
    }
    else
    {
        // Its in the old format which serialized floor_t
        // Padding at the start (an old thinker_t struct)
        SV_Seek(16);

        // Start of used data members.
#if __JHEXEN__
        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_FLOOR: bad sector number\n");
        floor->sector = sector;

        floor->type = SV_ReadLong();
        floor->crush = SV_ReadLong();
#else
        floor->type = SV_ReadLong();

        floor->crush = SV_ReadLong();

        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_FLOOR: bad sector number\n");
        floor->sector = sector;
#endif
        floor->state = (int) SV_ReadLong();
        floor->newSpecial = SV_ReadLong();
        // Flat number is an absolute lump index.
        { Uri* uri = Uri_NewWithPath2("Flats:", RC_NULL);
        ddstring_t name;
        Str_Init(&name);
        F_FileName(&name, Str_Text(W_LumpName(SV_ReadShort())));
        Uri_SetPath(uri, Str_Text(&name));
        floor->material = P_ToPtr(DMU_MATERIAL, Materials_ResolveUri(uri));
        Uri_Delete(uri);
        Str_Free(&name);
        }

        floor->floorDestHeight = FIX2FLT((fixed_t) SV_ReadLong());
        floor->speed = FIX2FLT((fixed_t) SV_ReadLong());

#if __JHEXEN__
        floor->delayCount = SV_ReadLong();
        floor->delayTotal = SV_ReadLong();
        floor->stairsDelayHeight = FIX2FLT((fixed_t) SV_ReadLong());
        floor->stairsDelayHeightDelta = FIX2FLT((fixed_t) SV_ReadLong());
        floor->resetHeight = FIX2FLT((fixed_t) SV_ReadLong());
        floor->resetDelay = SV_ReadShort();
        floor->resetDelayCount = SV_ReadShort();
        /*floor->textureChange =*/ SV_ReadByte();
#endif
    }

    P_ToXSector(floor->sector)->specialData = floor;
    floor->thinker.function = T_MoveFloor;

    return true; // Add this thinker.
}

static void SV_WritePlat(const plat_t *plat)
{
    SV_WriteByte(1); // Write a version byte.

    SV_WriteByte((byte) plat->type);

    SV_WriteLong(P_ToIndex(plat->sector));

    SV_WriteLong(FLT2FIX(plat->speed));
    SV_WriteShort((int)plat->low);
    SV_WriteShort((int)plat->high);

    SV_WriteLong(plat->wait);
    SV_WriteLong(plat->count);

    SV_WriteByte((byte) plat->state);
    SV_WriteByte((byte) plat->oldState);
    SV_WriteByte((byte) plat->crush);

    SV_WriteLong(plat->tag);
}

static int SV_ReadPlat(plat_t *plat)
{
    Sector *sector;

#if __JHEXEN__
    if(mapVersion >= 4)
#else
    if(hdr->version >= 5)
#endif
    {   // Note: the thinker class byte has already been read.
        /*int ver =*/ SV_ReadByte(); // version byte.

        plat->thinker.function = T_PlatRaise;

#if !__JHEXEN__
        // Should we put this into stasis?
        if(hdr->version == 5)
        {
        if(!SV_ReadByte())
            Thinker_SetStasis(&plat->thinker, true);
        }
#endif

        plat->type = (plattype_e) SV_ReadByte();

        sector = P_ToPtr(DMU_SECTOR, SV_ReadLong());

        if(!sector)
            Con_Error("TC_PLAT: bad sector number\n");

        plat->sector = sector;

        plat->speed = FIX2FLT(SV_ReadLong());
        plat->low = (float) SV_ReadShort();
        plat->high = (float) SV_ReadShort();

        plat->wait = SV_ReadLong();
        plat->count = SV_ReadLong();

        plat->state = (platstate_e) SV_ReadByte();
        plat->oldState = (platstate_e) SV_ReadByte();
        plat->crush = (boolean) SV_ReadByte();

        plat->tag = SV_ReadLong();
    }
    else
    {
        // Its in the old format which serialized plat_t
        // Padding at the start (an old thinker_t struct)
        thinker_t junk;
        SV_Read(&junk, (size_t) 16);

        // Start of used data members.
        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_PLAT: bad sector number\n");
        plat->sector = sector;

        plat->speed = FIX2FLT((fixed_t) SV_ReadLong());
        plat->low = FIX2FLT((fixed_t) SV_ReadLong());
        plat->high = FIX2FLT((fixed_t) SV_ReadLong());

        plat->wait = SV_ReadLong();
        plat->count = SV_ReadLong();
        plat->state = SV_ReadLong();
        plat->oldState = SV_ReadLong();
        plat->crush = SV_ReadLong();
        plat->tag = SV_ReadLong();
        plat->type = SV_ReadLong();

        plat->thinker.function = T_PlatRaise;
#if !__JHEXEN__
        if(!junk.function)
            Thinker_SetStasis(&plat->thinker, true);
#endif
    }

    P_ToXSector(plat->sector)->specialData = plat;
    return true; // Add this thinker.
}

#if __JHEXEN__
static void SV_WriteLight(const light_t* th)
{
    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteByte((byte) th->type);

    SV_WriteLong(P_ToIndex(th->sector));

    SV_WriteLong((int) (255.0f * th->value1));
    SV_WriteLong((int) (255.0f * th->value2));
    SV_WriteLong(th->tics1);
    SV_WriteLong(th->tics2);
    SV_WriteLong(th->count);
}

static int SV_ReadLight(light_t* th)
{
    Sector*             sector;

    if(mapVersion >= 4)
    {
        /*int ver =*/ SV_ReadByte(); // version byte.

        th->type = (lighttype_t) SV_ReadByte();

        sector = P_ToPtr(DMU_SECTOR, SV_ReadLong());
        if(!sector)
            Con_Error("TC_LIGHT: bad sector number\n");
        th->sector = sector;

        th->value1 = (float) SV_ReadLong() / 255.0f;
        th->value2 = (float) SV_ReadLong() / 255.0f;
        th->tics1 = SV_ReadLong();
        th->tics2 = SV_ReadLong();
        th->count = SV_ReadLong();
    }
    else
    {
        // Its in the old pre V4 format which serialized light_t
        // Padding at the start (an old thinker_t struct)
        thinker_t junk;
        SV_Read(&junk, (size_t) 16);

        // Start of used data members.
        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_LIGHT: bad sector number\n");
        th->sector = sector;

        th->type = (lighttype_t) SV_ReadLong();
        th->value1 = (float) SV_ReadLong() / 255.0f;
        th->value2 = (float) SV_ReadLong() / 255.0f;
        th->tics1 = SV_ReadLong();
        th->tics2 = SV_ReadLong();
        th->count = SV_ReadLong();
    }

    th->thinker.function = (thinkfunc_t) T_Light;

    return true; // Add this thinker.
}

static void SV_WritePhase(const phase_t* th)
{
    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteLong(P_ToIndex(th->sector));

    SV_WriteLong(th->index);
    SV_WriteLong((int) (255.0f * th->baseValue));
}

static int SV_ReadPhase(phase_t* th)
{
    Sector*             sector;

    if(mapVersion >= 4)
    {
        // Note: the thinker class byte has already been read.
        /*int ver =*/ SV_ReadByte(); // version byte.

        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_PHASE: bad sector number\n");
        th->sector = sector;

        th->index = SV_ReadLong();
        th->baseValue = (float) SV_ReadLong() / 255.0f;
    }
    else
    {
        // Its in the old pre V4 format which serialized phase_t
        // Padding at the start (an old thinker_t struct)
        thinker_t junk;
        SV_Read(&junk, (size_t) 16);

        // Start of used data members.
        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_PHASE: bad sector number\n");
        th->sector = sector;

        th->index = SV_ReadLong();
        th->baseValue = (float) SV_ReadLong() / 255.0f;
    }

    th->thinker.function = (thinkfunc_t) T_Phase;

    return true; // Add this thinker.
}

static void SV_WriteScript(const acs_t* th)
{
    uint                i;

    SV_WriteByte(1); // Write a version byte.

    SV_WriteLong(SV_ThingArchiveNum(th->activator));
    SV_WriteLong(th->line ? P_ToIndex(th->line) : -1);
    SV_WriteLong(th->side);
    SV_WriteLong(th->number);
    SV_WriteLong(th->infoIndex);
    SV_WriteLong(th->delayCount);
    for(i = 0; i < ACS_STACK_DEPTH; ++i)
        SV_WriteLong(th->stack[i]);
    SV_WriteLong(th->stackPtr);
    for(i = 0; i < MAX_ACS_SCRIPT_VARS; ++i)
        SV_WriteLong(th->vars[i]);
    SV_WriteLong(((const byte*)th->ip) - ActionCodeBase);
}

static int SV_ReadScript(acs_t* th)
{
    int                 temp;
    uint                i;

    if(mapVersion >= 4)
    {
        // Note: the thinker class byte has already been read.
        /*int ver =*/ SV_ReadByte(); // version byte.

        th->activator = (mobj_t*) SV_ReadLong();
        th->activator = SV_GetArchiveThing(PTR2INT(th->activator), &th->activator);
        temp = SV_ReadLong();
        if(temp == -1)
            th->line = NULL;
        else
            th->line = P_ToPtr(DMU_LINE, temp);
        th->side = SV_ReadLong();
        th->number = SV_ReadLong();
        th->infoIndex = SV_ReadLong();
        th->delayCount = SV_ReadLong();
        for(i = 0; i < ACS_STACK_DEPTH; ++i)
            th->stack[i] = SV_ReadLong();
        th->stackPtr = SV_ReadLong();
        for(i = 0; i < MAX_ACS_SCRIPT_VARS; ++i)
            th->vars[i] = SV_ReadLong();
        th->ip = (int *) (ActionCodeBase + SV_ReadLong());
    }
    else
    {
        // Its in the old pre V4 format which serialized acs_t
        // Padding at the start (an old thinker_t struct)
        thinker_t   junk;
        SV_Read(&junk, (size_t) 16);

        // Start of used data members.
        th->activator = (mobj_t*) SV_ReadLong();
        th->activator = SV_GetArchiveThing(PTR2INT(th->activator), &th->activator);
        temp = SV_ReadLong();
        if(temp == -1)
            th->line = NULL;
        else
            th->line = P_ToPtr(DMU_LINE, temp);
        th->side = SV_ReadLong();
        th->number = SV_ReadLong();
        th->infoIndex = SV_ReadLong();
        th->delayCount = SV_ReadLong();
        for(i = 0; i < ACS_STACK_DEPTH; ++i)
            th->stack[i] = SV_ReadLong();
        th->stackPtr = SV_ReadLong();
        for(i = 0; i < MAX_ACS_SCRIPT_VARS; ++i)
            th->vars[i] = SV_ReadLong();
        th->ip = (int *) (ActionCodeBase + SV_ReadLong());
    }

    th->thinker.function = (thinkfunc_t) T_InterpretACS;

    return true; // Add this thinker.
}

static void SV_WriteDoorPoly(const polydoor_t* th)
{
    SV_WriteByte(1); // Write a version byte.

    SV_WriteByte(th->type);

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteLong(th->polyobj);
    SV_WriteLong(th->intSpeed);
    SV_WriteLong(th->dist);
    SV_WriteLong(th->totalDist);
    SV_WriteLong(th->direction);
    SV_WriteLong(FLT2FIX(th->speed[VX]));
    SV_WriteLong(FLT2FIX(th->speed[VY]));
    SV_WriteLong(th->tics);
    SV_WriteLong(th->waitTics);
    SV_WriteByte(th->close);
}

static int SV_ReadDoorPoly(polydoor_t* th)
{
    if(mapVersion >= 4)
    {
        // Note: the thinker class byte has already been read.
        /*int ver =*/ SV_ReadByte(); // version byte.

        // Start of used data members.
        th->type = SV_ReadByte();

        th->polyobj = SV_ReadLong();
        th->intSpeed = SV_ReadLong();
        th->dist = SV_ReadLong();
        th->totalDist = SV_ReadLong();
        th->direction = SV_ReadLong();
        th->speed[VX] = FIX2FLT(SV_ReadLong());
        th->speed[VY] = FIX2FLT(SV_ReadLong());
        th->tics = SV_ReadLong();
        th->waitTics = SV_ReadLong();
        th->close = SV_ReadByte();
    }
    else
    {
        // Its in the old pre V4 format which serialized polydoor_t
        // Padding at the start (an old thinker_t struct)
        thinker_t junk;
        SV_Read(&junk, (size_t) 16);

        // Start of used data members.
        th->polyobj = SV_ReadLong();
        th->intSpeed = SV_ReadLong();
        th->dist = SV_ReadLong();
        th->totalDist = SV_ReadLong();
        th->direction = SV_ReadLong();
        th->speed[VX] = FIX2FLT(SV_ReadLong());
        th->speed[VY] = FIX2FLT(SV_ReadLong());
        th->tics = SV_ReadLong();
        th->waitTics = SV_ReadLong();
        th->type = SV_ReadByte();
        th->close = SV_ReadByte();
    }

    th->thinker.function = T_PolyDoor;

    return true; // Add this thinker.
}

static void SV_WriteMovePoly(const polyevent_t* th)
{
    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteLong(th->polyobj);
    SV_WriteLong(th->intSpeed);
    SV_WriteLong(th->dist);
    SV_WriteLong(th->fangle);
    SV_WriteLong(FLT2FIX(th->speed[VX]));
    SV_WriteLong(FLT2FIX(th->speed[VY]));
}

static int SV_ReadMovePoly(polyevent_t* th)
{
    if(mapVersion >= 4)
    {
        // Note: the thinker class byte has already been read.
        /*int ver =*/ SV_ReadByte(); // version byte.

        // Start of used data members.
        th->polyobj = SV_ReadLong();
        th->intSpeed = SV_ReadLong();
        th->dist = SV_ReadLong();
        th->fangle = SV_ReadLong();
        th->speed[VX] = FIX2FLT(SV_ReadLong());
        th->speed[VY] = FIX2FLT(SV_ReadLong());
    }
    else
    {
        // Its in the old pre V4 format which serialized polyevent_t
        // Padding at the start (an old thinker_t struct)
        thinker_t junk;
        SV_Read(&junk, (size_t) 16);

        // Start of used data members.
        th->polyobj = SV_ReadLong();
        th->intSpeed = SV_ReadLong();
        th->dist = SV_ReadLong();
        th->fangle = SV_ReadLong();
        th->speed[VX] = FIX2FLT(SV_ReadLong());
        th->speed[VY] = FIX2FLT(SV_ReadLong());
    }

    th->thinker.function = T_MovePoly;

    return true; // Add this thinker.
}

static void SV_WriteRotatePoly(const polyevent_t* th)
{
    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteLong(th->polyobj);
    SV_WriteLong(th->intSpeed);
    SV_WriteLong(th->dist);
    SV_WriteLong(th->fangle);
    SV_WriteLong(FLT2FIX(th->speed[VX]));
    SV_WriteLong(FLT2FIX(th->speed[VY]));
}

static int SV_ReadRotatePoly(polyevent_t* th)
{
    if(mapVersion >= 4)
    {
        // Note: the thinker class byte has already been read.
        /*int ver =*/ SV_ReadByte(); // version byte.

        // Start of used data members.
        th->polyobj = SV_ReadLong();
        th->intSpeed = SV_ReadLong();
        th->dist = SV_ReadLong();
        th->fangle = SV_ReadLong();
        th->speed[VX] = FIX2FLT(SV_ReadLong());
        th->speed[VY] = FIX2FLT(SV_ReadLong());
    }
    else
    {
        // Its in the old pre V4 format which serialized polyevent_t
        // Padding at the start (an old thinker_t struct)
        thinker_t junk;
        SV_Read(&junk, (size_t) 16);

        // Start of used data members.
        th->polyobj = SV_ReadLong();
        th->intSpeed = SV_ReadLong();
        th->dist = SV_ReadLong();
        th->fangle = SV_ReadLong();
        th->speed[VX] = FIX2FLT(SV_ReadLong());
        th->speed[VY] = FIX2FLT(SV_ReadLong());
    }

    th->thinker.function = T_RotatePoly;
    return true; // Add this thinker.
}

static void SV_WritePillar(const pillar_t* th)
{
    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteLong(P_ToIndex(th->sector));

    SV_WriteLong(FLT2FIX(th->ceilingSpeed));
    SV_WriteLong(FLT2FIX(th->floorSpeed));
    SV_WriteLong(FLT2FIX(th->floorDest));
    SV_WriteLong(FLT2FIX(th->ceilingDest));
    SV_WriteLong(th->direction);
    SV_WriteLong(th->crush);
}

static int SV_ReadPillar(pillar_t* th)
{
    Sector*             sector;

    if(mapVersion >= 4)
    {
        // Note: the thinker class byte has already been read.
        /*int ver =*/ SV_ReadByte(); // version byte.

        // Start of used data members.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_BUILD_PILLAR: bad sector number\n");
        th->sector = sector;

        th->ceilingSpeed = FIX2FLT((fixed_t) SV_ReadLong());
        th->floorSpeed = FIX2FLT((fixed_t) SV_ReadLong());
        th->floorDest = FIX2FLT((fixed_t) SV_ReadLong());
        th->ceilingDest = FIX2FLT((fixed_t) SV_ReadLong());
        th->direction = SV_ReadLong();
        th->crush = SV_ReadLong();
    }
    else
    {
        // Its in the old pre V4 format which serialized pillar_t
        // Padding at the start (an old thinker_t struct)
        thinker_t junk;
        SV_Read(&junk, (size_t) 16);

        // Start of used data members.
        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_BUILD_PILLAR: bad sector number\n");
        th->sector = sector;

        th->ceilingSpeed = FIX2FLT((fixed_t) SV_ReadLong());
        th->floorSpeed = FIX2FLT((fixed_t) SV_ReadLong());
        th->floorDest = FIX2FLT((fixed_t) SV_ReadLong());
        th->ceilingDest = FIX2FLT((fixed_t) SV_ReadLong());
        th->direction = SV_ReadLong();
        th->crush = SV_ReadLong();
    }

    th->thinker.function = (thinkfunc_t) T_BuildPillar;

    P_ToXSector(th->sector)->specialData = th;
    return true; // Add this thinker.
}

static void SV_WriteFloorWaggle(const waggle_t* th)
{
    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteLong(P_ToIndex(th->sector));

    SV_WriteLong(FLT2FIX(th->originalHeight));
    SV_WriteLong(FLT2FIX(th->accumulator));
    SV_WriteLong(FLT2FIX(th->accDelta));
    SV_WriteLong(FLT2FIX(th->targetScale));
    SV_WriteLong(FLT2FIX(th->scale));
    SV_WriteLong(FLT2FIX(th->scaleDelta));
    SV_WriteLong(th->ticker);
    SV_WriteLong(th->state);
}

static int SV_ReadFloorWaggle(waggle_t* th)
{
    Sector*             sector;

    if(mapVersion >= 4)
    {
        /*int ver =*/ SV_ReadByte(); // version byte.

        // Start of used data members.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_FLOOR_WAGGLE: bad sector number\n");
        th->sector = sector;

        th->originalHeight = FIX2FLT((fixed_t) SV_ReadLong());
        th->accumulator = FIX2FLT((fixed_t) SV_ReadLong());
        th->accDelta = FIX2FLT((fixed_t) SV_ReadLong());
        th->targetScale = FIX2FLT((fixed_t) SV_ReadLong());
        th->scale = FIX2FLT((fixed_t) SV_ReadLong());
        th->scaleDelta = FIX2FLT((fixed_t) SV_ReadLong());
        th->ticker = SV_ReadLong();
        th->state = SV_ReadLong();
    }
    else
    {
        // Its in the old pre V4 format which serialized waggle_t
        // Padding at the start (an old thinker_t struct)
        thinker_t junk;
        SV_Read(&junk, (size_t) 16);

        // Start of used data members.
        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_FLOOR_WAGGLE: bad sector number\n");
        th->sector = sector;

        th->originalHeight = FIX2FLT((fixed_t) SV_ReadLong());
        th->accumulator = FIX2FLT((fixed_t) SV_ReadLong());
        th->accDelta = FIX2FLT((fixed_t) SV_ReadLong());
        th->targetScale = FIX2FLT((fixed_t) SV_ReadLong());
        th->scale = FIX2FLT((fixed_t) SV_ReadLong());
        th->scaleDelta = FIX2FLT((fixed_t) SV_ReadLong());
        th->ticker = SV_ReadLong();
        th->state = SV_ReadLong();
    }

    th->thinker.function = (thinkfunc_t) T_FloorWaggle;

    P_ToXSector(th->sector)->specialData = th;
    return true; // Add this thinker.
}
#endif // __JHEXEN__

#if !__JHEXEN__
static void SV_WriteFlash(const lightflash_t* flash)
{
    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteLong(P_ToIndex(flash->sector));

    SV_WriteLong(flash->count);
    SV_WriteLong((int) (255.0f * flash->maxLight));
    SV_WriteLong((int) (255.0f * flash->minLight));
    SV_WriteLong(flash->maxTime);
    SV_WriteLong(flash->minTime);
}

static int SV_ReadFlash(lightflash_t* flash)
{
    Sector*             sector;

    if(hdr->version >= 5)
    {   // Note: the thinker class byte has already been read.
        /*int ver =*/ SV_ReadByte(); // version byte.

        // Start of used data members.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_FLASH: bad sector number\n");
        flash->sector = sector;

        flash->count = SV_ReadLong();
        flash->maxLight = (float) SV_ReadLong() / 255.0f;
        flash->minLight = (float) SV_ReadLong() / 255.0f;
        flash->maxTime = SV_ReadLong();
        flash->minTime = SV_ReadLong();
    }
    else
    {
        // Its in the old pre V5 format which serialized lightflash_t
        // Padding at the start (an old thinker_t struct)
        SV_Seek(16);

        // Start of used data members.
        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_FLASH: bad sector number\n");
        flash->sector = sector;

        flash->count = SV_ReadLong();
        flash->maxLight = (float) SV_ReadLong() / 255.0f;
        flash->minLight = (float) SV_ReadLong() / 255.0f;
        flash->maxTime = SV_ReadLong();
        flash->minTime = SV_ReadLong();
    }

    flash->thinker.function = (thinkfunc_t) T_LightFlash;
    return true; // Add this thinker.
}

static void SV_WriteStrobe(const strobe_t* strobe)
{
    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteLong(P_ToIndex(strobe->sector));

    SV_WriteLong(strobe->count);
    SV_WriteLong((int) (255.0f * strobe->maxLight));
    SV_WriteLong((int) (255.0f * strobe->minLight));
    SV_WriteLong(strobe->darkTime);
    SV_WriteLong(strobe->brightTime);
}

static int SV_ReadStrobe(strobe_t* strobe)
{
    Sector*             sector;

    if(hdr->version >= 5)
    {   // Note: the thinker class byte has already been read.
        /*int ver =*/ SV_ReadByte(); // version byte.

        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_STROBE: bad sector number\n");
        strobe->sector = sector;

        strobe->count = SV_ReadLong();
        strobe->maxLight = (float) SV_ReadLong() / 255.0f;
        strobe->minLight = (float) SV_ReadLong() / 255.0f;
        strobe->darkTime = SV_ReadLong();
        strobe->brightTime = SV_ReadLong();
    }
    else
    {
        // Its in the old pre V5 format which serialized strobe_t
        // Padding at the start (an old thinker_t struct)
        SV_Seek(16);

        // Start of used data members.
        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_STROBE: bad sector number\n");
        strobe->sector = sector;

        strobe->count = SV_ReadLong();
        strobe->minLight = (float) SV_ReadLong() / 255.0f;
        strobe->maxLight = (float) SV_ReadLong() / 255.0f;
        strobe->darkTime = SV_ReadLong();
        strobe->brightTime = SV_ReadLong();
    }

    strobe->thinker.function = (thinkfunc_t) T_StrobeFlash;
    return true; // Add this thinker.
}

static void SV_WriteGlow(const glow_t* glow)
{
    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteLong(P_ToIndex(glow->sector));

    SV_WriteLong((int) (255.0f * glow->maxLight));
    SV_WriteLong((int) (255.0f * glow->minLight));
    SV_WriteLong(glow->direction);
}

static int SV_ReadGlow(glow_t* glow)
{
    Sector* sector;

    if(hdr->version >= 5)
    {
        // Note: the thinker class byte has already been read.
        /*int ver =*/ SV_ReadByte(); // version byte.

        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_GLOW: bad sector number\n");
        glow->sector = sector;

        glow->maxLight = (float) SV_ReadLong() / 255.0f;
        glow->minLight = (float) SV_ReadLong() / 255.0f;
        glow->direction = SV_ReadLong();
    }
    else
    {
        // Its in the old pre V5 format which serialized strobe_t
        // Padding at the start (an old thinker_t struct)
        SV_Seek(16);

        // Start of used data members.
        // A 32bit pointer to sector, serialized.
        sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector)
            Con_Error("TC_GLOW: bad sector number\n");
        glow->sector = sector;

        glow->minLight = (float) SV_ReadLong() / 255.0f;
        glow->maxLight = (float) SV_ReadLong() / 255.0f;
        glow->direction = SV_ReadLong();
    }

    glow->thinker.function = (thinkfunc_t) T_Glow;
    return true; // Add this thinker.
}

# if __JDOOM__ || __JDOOM64__
static void SV_WriteFlicker(const fireflicker_t* flicker)
{
    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteLong(P_ToIndex(flicker->sector));

    SV_WriteLong((int) (255.0f * flicker->maxLight));
    SV_WriteLong((int) (255.0f * flicker->minLight));
}

/**
 * T_FireFlicker was added to save games in ver5, therefore we don't have
 * an old format to support.
 */
static int SV_ReadFlicker(fireflicker_t* flicker)
{
    Sector*             sector;
    /*int ver =*/ SV_ReadByte(); // version byte.

    // Note: the thinker class byte has already been read.
    sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
    if(!sector)
        Con_Error("TC_FLICKER: bad sector number\n");
    flicker->sector = sector;

    flicker->maxLight = (float) SV_ReadLong() / 255.0f;
    flicker->minLight = (float) SV_ReadLong() / 255.0f;

    flicker->thinker.function = (thinkfunc_t) T_FireFlicker;
    return true; // Add this thinker.
}
# endif

# if __JDOOM64__
static void SV_WriteBlink(const lightblink_t* blink)
{
    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    SV_WriteLong(P_ToIndex(blink->sector));

    SV_WriteLong(blink->count);
    SV_WriteLong((int) (255.0f * blink->maxLight));
    SV_WriteLong((int) (255.0f * blink->minLight));
    SV_WriteLong(blink->maxTime);
    SV_WriteLong(blink->minTime);
}

/**
 * T_LightBlink was added to save games in ver5, therefore we don't have an
 * old format to support
 */
static int SV_ReadBlink(lightblink_t* blink)
{
    Sector* sector;
    /*int ver =*/ SV_ReadByte(); // version byte.

    // Note: the thinker class byte has already been read.
    sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
    if(!sector)
        Con_Error("tc_lightblink: bad sector number\n");
    blink->sector = sector;

    blink->count = SV_ReadLong();
    blink->maxLight = (float) SV_ReadLong() / 255.0f;
    blink->minLight = (float) SV_ReadLong() / 255.0f;
    blink->maxTime = SV_ReadLong();
    blink->minTime = SV_ReadLong();

    blink->thinker.function = (thinkfunc_t) T_LightBlink;
    return true; // Add this thinker.
}
# endif
#endif // !__JHEXEN__

static void SV_WriteMaterialChanger(const materialchanger_t* mchanger)
{
    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    // Write a type byte. For future use (e.g., changing plane surface
    // materials as well as sidedef surface materials).
    SV_WriteByte(0);
    SV_WriteLong(mchanger->timer);
    SV_WriteLong(P_ToIndex(mchanger->side));
    SV_WriteByte((byte) mchanger->section);
    SV_WriteShort(MaterialArchive_FindUniqueSerialId(materialArchive, mchanger->material));
}

static int SV_ReadMaterialChanger(materialchanger_t* mchanger)
{
    SideDef* side;
    /*int ver =*/ SV_ReadByte(); // version byte.

    SV_ReadByte(); // Type byte.
    mchanger->timer = SV_ReadLong();
    // Note: the thinker class byte has already been read.
    side = P_ToPtr(DMU_SIDEDEF, (int) SV_ReadLong());
    if(!side)
        Con_Error("t_materialchanger: bad sidedef number\n");
    mchanger->side = side;
    mchanger->section = (SideDefSection) SV_ReadByte();
    mchanger->material = SV_GetArchiveMaterial(SV_ReadShort(), 0);

    mchanger->thinker.function = T_MaterialChanger;

    return true; // Add this thinker.
}

static void SV_WriteScroll(const scroll_t* scroll)
{
    DENG_ASSERT(scroll);

    SV_WriteByte(1); // Write a version byte.

    // Note we don't bother to save a byte to tell if the function
    // is present as we ALWAYS add one when loading.

    // Write a type byte. For future use (e.g., scrolling plane surface
    // materials as well as sidedef surface materials).
    SV_WriteByte(DMU_GetType(scroll->dmuObject));
    SV_WriteLong(P_ToIndex(scroll->dmuObject));
    SV_WriteLong(scroll->elementBits);
    SV_WriteLong(FLT2FIX(scroll->offset[0]));
    SV_WriteLong(FLT2FIX(scroll->offset[1]));
}

static int SV_ReadScroll(scroll_t* scroll)
{
    /*int ver;*/

    DENG_ASSERT(scroll);

    /*ver =*/ SV_ReadByte(); // version byte.
    // Note: the thinker class byte has already been read.

    if(SV_ReadByte() == DMU_SIDEDEF) // Type byte.
    {
        SideDef* side = P_ToPtr(DMU_SIDEDEF, (int) SV_ReadLong());
        if(!side) Con_Error("t_scroll: bad sidedef number\n");
        scroll->dmuObject = side;
    }
    else // Sector plane-surface.
    {
        Sector* sector = P_ToPtr(DMU_SECTOR, (int) SV_ReadLong());
        if(!sector) Con_Error("t_scroll: bad sector number\n");
        scroll->dmuObject = sector;
    }

    scroll->elementBits = SV_ReadLong();
    scroll->offset[0] = FIX2FLT((fixed_t) SV_ReadLong());
    scroll->offset[1] = FIX2FLT((fixed_t) SV_ReadLong());

    scroll->thinker.function = (thinkfunc_t) T_Scroll;

    return true; // Add this thinker.
}

/**
 * Archives the specified thinker.
 *
 * @param th        The thinker to be archived.
 */
static int archiveThinker(thinker_t* th, void* context)
{
    boolean             savePlayers = *(boolean*) context;

    // Are we archiving players?
    if(!(th->function == (thinkfunc_t) P_MobjThinker && ((mobj_t *) th)->player &&
       !savePlayers))
    {
        thinkerinfo_t*      thInfo = infoForThinker(th);

        if(!thInfo)
            return false; // This is not a thinker we need to save.

        // Only the server saves this class of thinker?
        if(!((thInfo->flags & TSF_SERVERONLY) && IS_CLIENT))
        {
#if _DEBUG
assert(thInfo->Write);
#endif
            // Write the header block for this thinker.
            SV_WriteByte(thInfo->thinkclass); // Thinker type byte.
            SV_WriteByte(th->inStasis? 1 : 0); // In stasis?

            // Write the thinker data.
            thInfo->Write(th);
        }
    }

    return false; // Continue iteration.
}

/**
 * Archives thinkers for both client and server.
 * Clients do not save all data for all thinkers (the server will send
 * it to us anyway so saving it would just bloat client save games).
 *
 * @note Some thinker classes are NEVER saved by clients.
 */
static void P_ArchiveThinkers(boolean savePlayers)
{
    boolean             localSavePlayers = savePlayers;

    SV_BeginSegment(ASEG_THINKERS);
#if __JHEXEN__
    SV_WriteLong(thingArchiveSize); // number of mobjs.
#endif

    // Save off the current thinkers.
    Thinker_Iterate(NULL, archiveThinker, &localSavePlayers);

    // Add a terminating marker.
    SV_WriteByte(TC_END);
}

static int restoreMobjLinks(thinker_t* th, void* context)
{
    mobj_t*             mo = (mobj_t *) th;

    mo->target = SV_GetArchiveThing(PTR2INT(mo->target), &mo->target);
    mo->onMobj = SV_GetArchiveThing(PTR2INT(mo->onMobj), &mo->onMobj);

#if __JHEXEN__
    switch(mo->type)
    {
    // Just tracer
    case MT_BISH_FX:
    case MT_HOLY_FX:
    case MT_DRAGON:
    case MT_THRUSTFLOOR_UP:
    case MT_THRUSTFLOOR_DOWN:
    case MT_MINOTAUR:
    case MT_SORCFX1:
        if(mapVersion >= 3)
        {
            mo->tracer = SV_GetArchiveThing(PTR2INT(mo->tracer), &mo->tracer);
        }
        else
        {
            mo->tracer = SV_GetArchiveThing(mo->special1, &mo->tracer);
            mo->special1 = 0;
        }
        break;

    // Just special2
    case MT_LIGHTNING_FLOOR:
    case MT_LIGHTNING_ZAP:
        mo->special2 = PTR2INT(SV_GetArchiveThing(mo->special2, &mo->special2));
        break;

    // Both tracer and special2
    case MT_HOLY_TAIL:
    case MT_LIGHTNING_CEILING:
        if(mapVersion >= 3)
        {
            mo->tracer = SV_GetArchiveThing(PTR2INT(mo->tracer), &mo->tracer);
        }
        else
        {
            mo->tracer = SV_GetArchiveThing(mo->special1, &mo->tracer);
            mo->special1 = 0;
        }
        mo->special2 = PTR2INT(SV_GetArchiveThing(mo->special2, &mo->special2));
        break;

    default:
        break;
    }
#else
# if __JDOOM__ || __JDOOM64__
    mo->tracer = SV_GetArchiveThing(PTR2INT(mo->tracer), &mo->tracer);
# endif
# if __JHERETIC__
    mo->generator = SV_GetArchiveThing(PTR2INT(mo->generator), &mo->generator);
# endif
#endif

    return false; // Continue iteration.
}

#if __JHEXEN__
static boolean mobjtypeHasCorpse(mobjtype_t type)
{
    // Only corpses that call A_QueueCorpse from death routine.
    /// @todo fixme: What about mods? Look for this action in the death state sequence?
    switch(type)
    {
    case MT_CENTAUR:
    case MT_CENTAURLEADER:
    case MT_DEMON:
    case MT_DEMON2:
    case MT_WRAITH:
    case MT_WRAITHB:
    case MT_BISHOP:
    case MT_ETTIN:
    case MT_PIG:
    case MT_CENTAUR_SHIELD:
    case MT_CENTAUR_SWORD:
    case MT_DEMONCHUNK1:
    case MT_DEMONCHUNK2:
    case MT_DEMONCHUNK3:
    case MT_DEMONCHUNK4:
    case MT_DEMONCHUNK5:
    case MT_DEMON2CHUNK1:
    case MT_DEMON2CHUNK2:
    case MT_DEMON2CHUNK3:
    case MT_DEMON2CHUNK4:
    case MT_DEMON2CHUNK5:
    case MT_FIREDEMON_SPLOTCH1:
    case MT_FIREDEMON_SPLOTCH2:
        return true;

    default: return false;
    }
}

static int rebuildCorpseQueueWorker(thinker_t* th, void* parameters)
{
    DENG_UNUSED(parameters);
{
    mobj_t* mo = (mobj_t*) th;

    // Must be a non-iced corpse.
    if((mo->flags & MF_CORPSE) && !(mo->flags & MF_ICECORPSE) && mobjtypeHasCorpse(mo->type))
    {
        A_QueueCorpse(mo); // Add a corpse to the queue.
    }

    return false; // Continue iteration.
}}

/**
 * @todo fixme: the corpse queue should be serialized (original order unknown).
 */
static void rebuildCorpseQueue(void)
{
    P_InitCorpseQueue();
    // Search the thinker list for corpses and place them in the queue.
    Thinker_Iterate((thinkfunc_t) P_MobjThinker, rebuildCorpseQueueWorker, NULL/*no params*/);
}
#endif

/**
 * Un-Archives thinkers for both client and server.
 */
static void P_UnArchiveThinkers(void)
{
    uint        i;
    byte        tClass = 0;
    thinker_t  *th = 0;
    thinkerinfo_t *thInfo = 0;
    boolean     found, knownThinker;
    boolean     inStasis;
#if __JHEXEN__
    boolean     doSpecials = (mapVersion >= 4);
#else
    boolean     doSpecials = (hdr->version >= 5);
#endif

#if !__JHEXEN__
    if(IS_SERVER)
#endif
    {
        Thinker_Iterate(NULL, removeThinker, NULL);
        Thinker_Init();
    }

#if __JHEXEN__
    if(mapVersion < 4)
        SV_AssertSegment(ASEG_MOBJS);
    else
#endif
        SV_AssertSegment(ASEG_THINKERS);

#if __JHEXEN__
    targetPlayerAddrs = NULL;
    SV_InitThingArchive(true, savingPlayers);
#endif

    // Read in saved thinkers.
#if __JHEXEN__
    i = 0;
#endif
    for(;;)
    {
#if __JHEXEN__
        if(doSpecials)
#endif
            tClass = SV_ReadByte();

#if __JHEXEN__
        if(mapVersion < 4)
        {
            if(doSpecials) // Have we started on the specials yet?
            {
                // Versions prior to 4 used a different value to mark
                // the end of the specials data and the thinker class ids
                // are differrent, so we need to manipulate the thinker
                // class identifier value.
                if(tClass != TC_END)
                    tClass += 2;
            }
            else
                tClass = TC_MOBJ;

            if(tClass == TC_MOBJ && i == thingArchiveSize)
            {
                SV_AssertSegment(ASEG_THINKERS);
                // We have reached the begining of the "specials" block.
                doSpecials = true;
                continue;
            }
        }
#else
        if(hdr->version < 5)
        {
            if(doSpecials) // Have we started on the specials yet?
            {
                // Versions prior to 5 used a different value to mark
                // the end of the specials data so we need to manipulate
                // the thinker class identifier value.
                if(tClass == PRE_VER5_END_SPECIALS)
                    tClass = TC_END;
                else
                    tClass += 3;
            }
            else if(tClass == TC_END)
            {
                // We have reached the begining of the "specials" block.
                doSpecials = true;
                continue;
            }
        }
#endif
        if(tClass == TC_END)
            break; // End of the list.

        found = knownThinker = inStasis = false;
        thInfo = thinkerInfo;
        while(thInfo->thinkclass != TC_NULL && !found)
        {
            if(thInfo->thinkclass == tClass)
            {
                found = true;

                // Not for us? (it shouldn't be here anyway!).
                assert(!((thInfo->flags & TSF_SERVERONLY) && IS_CLIENT));

                {
                    // Mobjs use a special engine-side allocator.
                    if(thInfo->thinkclass == TC_MOBJ)
                    {
                        th = (thinker_t*)
                            P_MobjCreateXYZ((thinkfunc_t) P_MobjThinker, 0, 0, 0, 0, 64, 64, 0);
                    }
                    else
                    {
                        th = Z_Calloc(thInfo->size, PU_MAP, 0);
                    }

                    // Is there a thinker header block?
#if __JHEXEN__
                    if(mapVersion >= 6)
#else
                    if(hdr->version >= 6)
#endif
                    {
                        inStasis = (boolean) SV_ReadByte();
                    }

                    knownThinker = thInfo->Read(th);
                }
            }
            if(!found)
                thInfo++;
        }
#if __JHEXEN__
        if(tClass == TC_MOBJ)
            i++;
#endif
        if(!found)
            Con_Error("P_UnarchiveThinkers: Unknown tClass %i in savegame",
                      tClass);

        if(knownThinker)
            Thinker_Add(th);
        if(inStasis)
            Thinker_SetStasis(th, true);
    }

    // Update references to things.
#if __JHEXEN__
    Thinker_Iterate((thinkfunc_t) P_MobjThinker, restoreMobjLinks, NULL);
#else
    if(IS_SERVER)
    {
        Thinker_Iterate((thinkfunc_t) P_MobjThinker, restoreMobjLinks, NULL);

        for(i = 0; i < numlines; ++i)
        {
            xline_t *xline = P_ToXLine(P_ToPtr(DMU_LINE, i));
            if(xline->xg)
                xline->xg->activator =
                    SV_GetArchiveThing(PTR2INT(xline->xg->activator),
                                       &xline->xg->activator);
        }
    }
#endif

#if __JHEXEN__
    P_CreateTIDList();
    rebuildCorpseQueue();
#endif
}

#if __JDOOM__
static void P_ArchiveBrain(void)
{
    int i;

    SV_WriteByte(1); // Write a version byte.

    SV_WriteShort(brain.numTargets);
    SV_WriteShort(brain.targetOn);
    SV_WriteByte(brain.easy!=0? 1:0);

    // Write the mobj references using the mobj archive.
    for(i = 0; i < brain.numTargets; ++i)
        SV_WriteShort(SV_ThingArchiveNum(brain.targets[i]));
}

static void P_UnArchiveBrain(void)
{
    int i, numTargets, ver = 0;

    if(hdr->version < 3)
        return; // No brain data before version 3.

    if(hdr->version >= 8)
        ver = SV_ReadByte();

    P_BrainClearTargets();
    if(ver >= 1)
    {
        numTargets = SV_ReadShort();
        brain.targetOn = SV_ReadShort();
        brain.easy = SV_ReadByte()!=0? true : false;
    }
    else
    {
        numTargets = SV_ReadByte();
        brain.targetOn = SV_ReadByte();
        brain.easy = false;
    }

    for(i = 0; i < numTargets; ++i)
    {
        P_BrainAddTarget(SV_GetArchiveThing((int) SV_ReadShort(), 0));
    }
}
#endif

#if !__JHEXEN__
static void P_ArchiveSoundTargets(void)
{
    uint                i;
    xsector_t*          xsec;

    // Write the total number.
    SV_WriteLong(numSoundTargets);

    // Write the mobj references using the mobj archive.
    for(i = 0; i < numsectors; ++i)
    {
        xsec = P_ToXSector(P_ToPtr(DMU_SECTOR, i));

        if(xsec->soundTarget)
        {
            SV_WriteLong(i);
            SV_WriteShort(SV_ThingArchiveNum(xsec->soundTarget));
        }
    }
}

static void P_UnArchiveSoundTargets(void)
{
    uint                i;
    uint                secid;
    uint                numsoundtargets;
    xsector_t*          xsec;

    // Sound Target data was introduced in ver 5
    if(hdr->version < 5)
        return;

    // Read the number of targets
    numsoundtargets = SV_ReadLong();

    // Read in the sound targets.
    for(i = 0; i < numsoundtargets; ++i)
    {
        secid = SV_ReadLong();

        if(secid > numsectors)
            Con_Error("P_UnArchiveSoundTargets: bad sector number\n");

        xsec = P_ToXSector(P_ToPtr(DMU_SECTOR, secid));
        xsec->soundTarget = INT2PTR(mobj_t, SV_ReadShort());
        xsec->soundTarget =
            SV_GetArchiveThing(PTR2INT(xsec->soundTarget), &xsec->soundTarget);
    }
}
#endif

#if __JHEXEN__
static void P_ArchiveSounds(void)
{
    uint                i;
    int                 difference;
    seqnode_t*          node;
    Sector*             sec;

    // Save the sound sequences.
    SV_BeginSegment(ASEG_SOUNDS);
    SV_WriteLong(ActiveSequences);
    for(node = SequenceListHead; node; node = node->next)
    {
        SV_WriteByte(1); // Write a version byte.

        SV_WriteLong(node->sequence);
        SV_WriteLong(node->delayTics);
        SV_WriteLong(node->volume);
        SV_WriteLong(SN_GetSequenceOffset(node->sequence, node->sequencePtr));
        SV_WriteLong(node->currentSoundID);

        i = 0;
        if(node->mobj)
        {
            for(; i < numpolyobjs; ++i)
            {
                if(node->mobj == (mobj_t*) P_GetPolyobj(i | 0x80000000))
                {
                    break;
                }
            }
        }

        if(i == numpolyobjs)
        {   // Sound is attached to a sector, not a polyobj.
            sec = P_GetPtrp(P_BspLeafAtPoint(node->mobj->origin), DMU_SECTOR);
            difference = P_ToIndex(sec);
            SV_WriteLong(0); // 0 -- sector sound origin.
        }
        else
        {
            SV_WriteLong(1); // 1 -- polyobj sound origin
            difference = i;
        }
        SV_WriteLong(difference);
    }
}

static void P_UnArchiveSounds(void)
{
    int             i;
    int             numSequences, sequence, seqOffset;
    int             delayTics, soundID, volume;
    int             polySnd, secNum, ver;
    mobj_t*         sndMobj = NULL;

    SV_AssertSegment(ASEG_SOUNDS);

    // Reload and restart all sound sequences
    numSequences = SV_ReadLong();
    i = 0;
    while(i < numSequences)
    {
        if(mapVersion >= 3)
            ver = SV_ReadByte();

        sequence = SV_ReadLong();
        delayTics = SV_ReadLong();
        volume = SV_ReadLong();
        seqOffset = SV_ReadLong();

        soundID = SV_ReadLong();
        polySnd = SV_ReadLong();
        secNum = SV_ReadLong();
        if(!polySnd)
        {
            sndMobj = P_GetPtr(DMU_SECTOR, secNum, DMU_BASE);
        }
        else
        {
            Polyobj* po = P_PolyobjByID(secNum);
            if(po) sndMobj = (mobj_t*) po;
        }

        SN_StartSequence(sndMobj, sequence);
        SN_ChangeNodeData(i, seqOffset, delayTics, volume, soundID);
        i++;
    }
}

static void P_ArchiveScripts(void)
{
    int         i;

    SV_BeginSegment(ASEG_SCRIPTS);
    for(i = 0; i < ACScriptCount; ++i)
    {
        SV_WriteShort(ACSInfo[i].state);
        SV_WriteShort(ACSInfo[i].waitValue);
    }
    for(i = 0; i < MAX_ACS_MAP_VARS; ++i)
        SV_WriteLong(MapVars[i]);
}

static void P_UnArchiveScripts(void)
{
    int         i;

    SV_AssertSegment(ASEG_SCRIPTS);
    for(i = 0; i < ACScriptCount; ++i)
    {
        ACSInfo[i].state = SV_ReadShort();
        ACSInfo[i].waitValue = SV_ReadShort();
    }
    for(i = 0; i < MAX_ACS_MAP_VARS; ++i)
        MapVars[i] = SV_ReadLong();
}

static void P_ArchiveGlobalScriptData(void)
{
    int i;

    SV_BeginSegment(ASEG_GLOBALSCRIPTDATA);
    SV_WriteByte(3); // version byte

    for(i = 0; i < MAX_ACS_WORLD_VARS; ++i)
        SV_WriteLong(WorldVars[i]);

    SV_WriteLong(ACSStoreSize);
    for(i = 0; i < ACSStoreSize; ++i)
    {
        const acsstore_t* store = &ACSStore[i];
        int j;

        SV_WriteLong(store->map);
        SV_WriteLong(store->script);
        for(j = 0; j < 4; ++j)
            SV_WriteByte(store->args[j]);
    }
}

static void P_UnArchiveGlobalScriptData(void)
{
    int i, ver = 1;

    if(hdr->version >= 7)
    {
        SV_AssertSegment(ASEG_GLOBALSCRIPTDATA);
        ver = SV_ReadByte();
    }

    for(i = 0; i < MAX_ACS_WORLD_VARS; ++i)
        WorldVars[i] = SV_ReadLong();

    if(ver >= 3)
    {
        ACSStoreSize = SV_ReadLong();
        if(ACSStoreSize)
        {
            if(ACSStore)
                ACSStore = Z_Realloc(ACSStore, sizeof(acsstore_t) * ACSStoreSize, PU_GAMESTATIC);
            else
                ACSStore = Z_Malloc(sizeof(acsstore_t) * ACSStoreSize, PU_GAMESTATIC, 0);

            for(i = 0; i < ACSStoreSize; ++i)
            {
                acsstore_t* store = &ACSStore[i];
                int j;

                store->map = SV_ReadLong();
                store->script = SV_ReadLong();
                for(j = 0; j < 4; ++j)
                    store->args[j] = SV_ReadByte();
            }
        }
    }
    else
    {   // Old format.
        acsstore_t tempStore[20];

        ACSStoreSize = 0;
        for(i = 0; i < 20; ++i)
        {
            int map = SV_ReadLong();
            acsstore_t* store = &tempStore[map < 0? 19 : ACSStoreSize++];
            int j;

            store->map = map < 0? 0 : map-1;
            store->script = SV_ReadLong();
            for(j = 0; j < 4; ++j)
                store->args[j] = SV_ReadByte();
        }

        if(hdr->version < 7)
            SV_Seek(12); // Junk.

        if(ACSStoreSize)
        {
            if(ACSStore)
                ACSStore = Z_Realloc(ACSStore, sizeof(acsstore_t) * ACSStoreSize, PU_GAMESTATIC);
            else
                ACSStore = Z_Malloc(sizeof(acsstore_t) * ACSStoreSize, PU_GAMESTATIC, 0);
            memcpy(ACSStore, tempStore, sizeof(acsstore_t) * ACSStoreSize);
        }
    }

    if(!ACSStoreSize && ACSStore)
    {
        Z_Free(ACSStore); ACSStore = NULL;
    }
}

static void P_ArchiveMisc(void)
{
    int         ix;

    SV_BeginSegment(ASEG_MISC);
    for(ix = 0; ix < MAXPLAYERS; ++ix)
    {
        SV_WriteLong(localQuakeHappening[ix]);
    }
}

static void P_UnArchiveMisc(void)
{
    int         ix;

    SV_AssertSegment(ASEG_MISC);
    for(ix = 0; ix < MAXPLAYERS; ++ix)
    {
        localQuakeHappening[ix] = SV_ReadLong();
    }
}
#endif

static void P_ArchiveMap(boolean savePlayers)
{
    // Place a header marker
    SV_BeginSegment(ASEG_MAP_HEADER2);

#if __JHEXEN__
    savingPlayers = savePlayers;

    // Write a version byte
    SV_WriteByte(MY_SAVE_VERSION);

    // Write the map timer
    SV_WriteLong(mapTime);
#else
    // Clear the sound target count (determined while saving sectors).
    numSoundTargets = 0;
#endif

    P_ArchiveWorld();
    P_ArchiveThinkers(savePlayers);

#if __JHEXEN__
    P_ArchiveScripts();
    P_ArchiveSounds();
    P_ArchiveMisc();
#else
    if(IS_SERVER)
    {
# if __JDOOM__
        // Doom saves the brain data, too. (It's a part of the world.)
        P_ArchiveBrain();
# endif
        // Save the sound target data (prevents bug where monsters who have
        // been alerted go back to sleep when loading a save game).
        P_ArchiveSoundTargets();
    }
#endif

    // Place a termination marker
    SV_BeginSegment(ASEG_END);
}

static void P_UnArchiveMap(void)
{
#if __JHEXEN__
    int segType = SV_ReadLong();

    // Determine the map version.
    if(segType == ASEG_MAP_HEADER2)
    {
        mapVersion = SV_ReadByte();
    }
    else if(segType == ASEG_MAP_HEADER)
    {
        mapVersion = 2;
    }
    else
    {
        Con_Error("Corrupt save game: Segment [%d] failed alignment check",
                  ASEG_MAP_HEADER);
    }
#else
    SV_AssertSegment(ASEG_MAP_HEADER2);
#endif

#if __JHEXEN__
    // Read the map timer
    mapTime = SV_ReadLong();
#endif

    P_UnArchiveWorld();
    P_UnArchiveThinkers();

#if __JHEXEN__
    P_UnArchiveScripts();
    P_UnArchiveSounds();
    P_UnArchiveMisc();
#else
    if(IS_SERVER)
    {
#if __JDOOM__
        // Doom saves the brain data, too. (It's a part of the world.)
        P_UnArchiveBrain();
#endif

        // Read the sound target data (prevents bug where monsters who have
        // been alerted go back to sleep when loading a save game).
        P_UnArchiveSoundTargets();
    }
#endif

    SV_AssertSegment(ASEG_END);
}

/**
 * @return  Pointer to the (currently in-use) material archive.
 */
MaterialArchive* SV_MaterialArchive(void)
{
    errorIfNotInited("SV_MaterialArchive");
    return materialArchive;
}

void SV_Init(void)
{
    static boolean firstInit = true;

    SV_InitIO();
    saveInfo = NULL;

    inited = true;
    if(firstInit)
    {
        firstInit = false;
        playerHeaderOK = false;
        thingArchive = NULL;
        thingArchiveSize = 0;
        materialArchive = NULL;
#if __JHEXEN__
        targetPlayerAddrs = NULL;
        saveBuffer = NULL;
#else
        numSoundTargets = 0;
#endif
        // -1 = Not yet chosen/determined.
        cvarLastSlot = -1;
        cvarQuickSlot = -1;
    }

    // (Re)Initialize the saved game paths, possibly creating them if they do not exist.
    SV_ConfigureSavePaths();
}

void SV_Shutdown(void)
{
    if(!inited) return;

    SV_ShutdownIO();
    clearSaveInfo();

    cvarLastSlot  = -1;
    cvarQuickSlot = -1;

    inited = false;
}

static boolean openGameSaveFile(const char* fileName, boolean write)
{
#if __JHEXEN__
    if(!write)
    {
        boolean result = M_ReadFile(fileName, (char**)&saveBuffer) > 0;
        // Set the save pointer.
        SV_HxSavePtr()->b = saveBuffer;
        return result;
    }
    else
#endif
    SV_OpenFile(fileName, write? "wp" : "rp");
    if(!SV_File()) return false;
    return true;
}

static int SV_LoadState(const char* path, SaveInfo* saveInfo)
{
    boolean loaded[MAXPLAYERS], infile[MAXPLAYERS];
    int i;

    DENG_ASSERT(path);
    DENG_ASSERT(saveInfo);

    playerHeaderOK = false; // Uninitialized.

    if(!openGameSaveFile(path, false))
        return 1; // Failed?

    // Read the header again.
    /// @todo Seek past the header straight to the game state.
    {
    SaveInfo* tmp = SaveInfo_New();
    SV_SaveInfo_Read(tmp);
    SaveInfo_Delete(tmp);
    }
    hdr = SaveInfo_Header(saveInfo);

    // Configure global game state:
    gameEpisode = hdr->episode - 1;
    gameMap = hdr->map - 1;
#if __JHEXEN__
    gameSkill = hdr->skill;
#else
    gameSkill = hdr->skill & 0x7f;
    fastParm = (hdr->skill & 0x80) != 0;
#endif
    deathmatch = hdr->deathmatch;
    noMonstersParm = hdr->noMonsters;
#if __JHEXEN__
    randomClassParm = hdr->randomClasses;
#else
    respawnMonsters = hdr->respawnMonsters;
#endif

    // Read global save data not part of the game metadata.
#if __JHEXEN__
    // Read global script info.
    P_UnArchiveGlobalScriptData();
#endif

    // We don't want to see a briefing if we're loading a save game.
    briefDisabled = true;

    // Load the map and configure some game settings.
    G_NewGame(gameSkill, gameEpisode, gameMap, 0/*gameMapEntryPoint*/);
    /// @todo Necessary?
    G_SetGameAction(GA_NONE);

#if !__JHEXEN__
    // Set the time.
    mapTime = hdr->mapTime;

    SV_InitThingArchive(true, true);
#endif

    P_UnArchivePlayerHeader();

    // Read the player structures
    // We don't have the right to say which players are in the game. The
    // players that already are will continue to be. If the data for a given
    // player is not in the savegame file, he will be notified. The data for
    // players who were saved but are not currently in the game will be
    // discarded.
    P_UnArchivePlayers(infile, loaded);

#if __JHEXEN__
    Z_Free(saveBuffer);
#endif

    // Create and populate the MaterialArchive.
#ifdef __JHEXEN__
    materialArchive = MaterialArchive_NewEmpty(true /* segment checks */);
#else
    materialArchive = MaterialArchive_NewEmpty(false);
#endif

    // Load the current map state.
#if __JHEXEN__
    unarchiveMap(composeGameSavePathForSlot2(BASE_SLOT, gameMap+1));
#else
    unarchiveMap();
#endif

#if !__JHEXEN__
    // Check consistency.
    if(SV_ReadByte() != CONSISTENCY)
    {
        Con_Error("SV_LoadGame: Bad savegame (consistency test failed!)\n");
    }
    SV_CloseFile();
#endif

    // We are done with the MaterialArchive.
    MaterialArchive_Delete(materialArchive);
    materialArchive = NULL;

#if !__JHEXEN__
    // We're done with the ThingArchive.
    SV_FreeThingArchive();
#endif

#if __JHEXEN__
    // Don't need the player mobj relocation info for load game
    SV_FreeTargetPlayerList();
#endif

    // Notify the players that weren't in the savegame.
    for(i = 0; i < MAXPLAYERS; ++i)
    {
        boolean notLoaded = false;

#if __JHEXEN__
        if(players[i].plr->inGame)
        {
            // Try to find a saved player that corresponds this one.
            uint k;
            for(k = 0; k < MAXPLAYERS; ++k)
            {
                if(saveToRealPlayerNum[k] == i)
                    break;
            }

            if(k < MAXPLAYERS)
                continue; // Found; don't bother this player.

            players[i].playerState = PST_REBORN;

            if(!i)
            {
                // If the CONSOLEPLAYER isn't in the save, it must be some
                // other player's file?
                P_SetMessage(players, LMF_NO_HIDE, GET_TXT(TXT_LOADMISSING));
            }
            else
            {
                NetSv_SendMessage(i, GET_TXT(TXT_LOADMISSING));
                notLoaded = true;
            }
        }
#else
        if(!loaded[i] && players[i].plr->inGame)
        {
            if(!i)
            {
                P_SetMessage(players, LMF_NO_HIDE, GET_TXT(TXT_LOADMISSING));
            }
            else
            {
                NetSv_SendMessage(i, GET_TXT(TXT_LOADMISSING));
            }
            notLoaded = true;
        }
#endif

        if(notLoaded)
        {
            // Kick this player out, he doesn't belong here.
            DD_Executef(false, "kick %i", i);
        }
    }

#if !__JHEXEN__
    // In netgames, the server tells the clients about this.
    NetSv_LoadGame(SaveInfo_GameId(saveInfo));
#endif

    return 0;
}

static void onLoadStateSuccess(void)
{
    int i;

    // Let the engine know where the local players are now.
    for(i = 0; i < MAXPLAYERS; ++i)
    {
        R_UpdateConsoleView(i);
    }

    // Spawn particle generators, fix HOMS etc, etc...
    R_SetupMap(DDSMM_AFTER_LOADING, 0);
}

static int loadStateWorker(const char* path, SaveInfo* saveInfo)
{
    int loadError = true; // Failed.

    if(path && saveInfo)
    {
        if(recogniseState(path, saveInfo))
        {
            loadError = SV_LoadState(path, saveInfo);
        }
        // Perhaps an original game save?
#if __JDOOM__
        else if(SV_RecogniseState_Dm_v19(path, saveInfo))
        {
            loadError = SV_LoadState_Dm_v19(path, saveInfo);
        }
#endif
#if __JHERETIC__
        else if(SV_RecogniseState_Hr_v13(path, saveInfo))
        {
            loadError = SV_LoadState_Hr_v13(path, saveInfo);
        }
#endif
    }

    if(!loadError)
    {
        // Material origin scrollers must be re-spawned for older save state versions.
        const saveheader_t* hdr = SaveInfo_Header(saveInfo);
        /// @todo Implement SaveInfo format type identifiers.
        if((hdr->magic != (IS_NETWORK_CLIENT? MY_CLIENT_SAVE_MAGIC : MY_SAVE_MAGIC)) ||
           hdr->version <= 10)
        {
            P_SpawnAllMaterialOriginScrollers();
        }

        onLoadStateSuccess();
    }

    return loadError;
}

boolean SV_LoadGame(int slot)
{
#if __JHEXEN__
    const int logicalSlot = BASE_SLOT;
#else
    const int logicalSlot = slot;
#endif
    SaveInfo* saveInfo;
    AutoStr* path;
    int loadError;

    errorIfNotInited("SV_LoadGame");

    if(!SV_IsValidSlot(slot)) return false;

    path = composeGameSavePathForSlot(slot);
    if(Str_IsEmpty(path))
    {
        Con_Message("Warning: Path \"%s\" is unreachable, game not loaded.", SV_SavePath());
        return false;
    }

    VERBOSE( Con_Message("Attempting load of game-save slot #%i...", slot) )

#if __JHEXEN__
    // Copy all needed save files to the base slot.
    /// @todo Why do this BEFORE loading?? (G_NewGame() does not load the serialized map state)
    /// @todo Does any caller ever attempt to load the base slot?? (Doesn't seem logical)
    if(slot != BASE_SLOT)
    {
        SV_CopySlot(slot, BASE_SLOT);
    }
#endif

    saveInfo = SV_SaveInfoForSlot(logicalSlot);
    loadError = loadStateWorker(Str_Text(path), saveInfo);

    if(!loadError)
    {
        Con_SetInteger2("game-save-last-slot", slot, SVF_WRITE_OVERRIDE);
    }
    else
    {
        Con_Message("Warning: Failed loading game-save slot #%i.", slot);
    }

    return !loadError;
}

void SV_SaveGameClient(uint gameId)
{
#if !__JHEXEN__ // unsupported in libhexen
    player_t* pl = &players[CONSOLEPLAYER];
    mobj_t* mo = pl->plr->mo;
    AutoStr* gameSavePath;
    SaveInfo* saveInfo;

    errorIfNotInited("SV_SaveGameClient");

    if(!IS_CLIENT || !mo)
        return;

    playerHeaderOK = false; // Uninitialized.

    gameSavePath = composeGameSavePathForClientGameId(gameId);
    if(!SV_OpenFile(Str_Text(gameSavePath), "wp"))
    {
        Con_Message("Warning: SV_SaveGameClient: Failed opening \"%s\" for writing.", Str_Text(gameSavePath));
        return;
    }

    // Prepare the header.
    saveInfo = SaveInfo_New();
    SaveInfo_SetGameId(saveInfo, gameId);
    SaveInfo_Configure(saveInfo);

    {Writer* svWriter = SV_NewWriter();
    SaveInfo_Write(saveInfo, svWriter);
    Writer_Delete(svWriter);
    }

    // Some important information.
    // Our position and look angles.
    SV_WriteLong(FLT2FIX(mo->origin[VX]));
    SV_WriteLong(FLT2FIX(mo->origin[VY]));
    SV_WriteLong(FLT2FIX(mo->origin[VZ]));
    SV_WriteLong(FLT2FIX(mo->floorZ));
    SV_WriteLong(FLT2FIX(mo->ceilingZ));
    SV_WriteLong(mo->angle); /* $unifiedangles */
    SV_WriteFloat(pl->plr->lookDir); /* $unifiedangles */
    P_ArchivePlayerHeader();
    SV_WritePlayer(CONSOLEPLAYER);

    // Create and populate the MaterialArchive.
    materialArchive = MaterialArchive_New(false);

    P_ArchiveMap(true);

    // We are done with the MaterialArchive.
    MaterialArchive_Delete(materialArchive);
    materialArchive = NULL;

    SV_CloseFile();
    SaveInfo_Delete(saveInfo);
#endif
}

void SV_LoadGameClient(uint gameId)
{
#if !__JHEXEN__ // unsupported in libhexen
    player_t* cpl = players + CONSOLEPLAYER;
    mobj_t* mo = cpl->plr->mo;
    AutoStr* gameSavePath;
    SaveInfo* saveInfo;

    errorIfNotInited("SV_LoadGameClient");

    if(!IS_CLIENT || !mo)
        return;

    playerHeaderOK = false; // Uninitialized.

    gameSavePath = composeGameSavePathForClientGameId(gameId);
    if(!SV_OpenFile(Str_Text(gameSavePath), "rp"))
    {
        Con_Message("Warning: SV_LoadGameClient: Failed opening \"%s\" for reading.", Str_Text(gameSavePath));
        return;
    }

    saveInfo = SaveInfo_New();
    SV_SaveInfo_Read(saveInfo);

    hdr = SaveInfo_Header(saveInfo);
    if(hdr->magic != MY_CLIENT_SAVE_MAGIC)
    {
        SaveInfo_Delete(saveInfo);
        SV_CloseFile();
        Con_Message("SV_LoadGameClient: Bad magic!");
        return;
    }

    gameSkill = hdr->skill;
    deathmatch = hdr->deathmatch;
    noMonstersParm = hdr->noMonsters;
    respawnMonsters = hdr->respawnMonsters;
    // Do we need to change the map?
    if(gameMap != hdr->map - 1 || gameEpisode != hdr->episode - 1)
    {
        gameEpisode = hdr->episode - 1;
        gameMap = hdr->map - 1;
        gameMapEntryPoint = 0;
        G_NewGame(gameSkill, gameEpisode, gameMap, gameMapEntryPoint);
        /// @todo Necessary?
        G_SetGameAction(GA_NONE);
    }
    mapTime = hdr->mapTime;

    P_MobjUnsetOrigin(mo);
    mo->origin[VX] = FIX2FLT(SV_ReadLong());
    mo->origin[VY] = FIX2FLT(SV_ReadLong());
    mo->origin[VZ] = FIX2FLT(SV_ReadLong());
    P_MobjSetOrigin(mo);
    mo->floorZ = FIX2FLT(SV_ReadLong());
    mo->ceilingZ = FIX2FLT(SV_ReadLong());
    mo->angle = SV_ReadLong(); /* $unifiedangles */
    cpl->plr->lookDir = SV_ReadFloat(); /* $unifiedangles */
    P_UnArchivePlayerHeader();
    SV_ReadPlayer(cpl);

    /**
     * Create and populate the MaterialArchive.
     *
     * @todo Does this really need to be done at all as a client?
     * When the client connects to the server it should send a copy
     * of the map upon joining, so why are we reading it here?
     */
    materialArchive = MaterialArchive_New(false);

    P_UnArchiveMap();

    // We are done with the MaterialArchive.
    MaterialArchive_Delete(materialArchive);
    materialArchive = NULL;

    SV_CloseFile();
    SaveInfo_Delete(saveInfo);
#endif
}

#if __JHEXEN__
static void unarchiveMap(const Str* path)
#else
static void unarchiveMap(void)
#endif
{
#if __JHEXEN__
    size_t bufferSize;

    DENG_ASSERT(path);

#ifdef _DEBUG
    Con_Printf("unarchiveMap: Reading %s\n", Str_Text(path));
#endif

    // Load the file
    bufferSize = M_ReadFile(Str_Text(path), (char**)&saveBuffer);
    if(0 == bufferSize)
    {
        Con_Message("Warning: unarchiveMap: Failed opening \"%s\" for reading.", Str_Text(path));
        return;
    }

    SV_HxSavePtr()->b = saveBuffer;
#endif

    P_UnArchiveMap();

#if __JHEXEN__
    // Free mobj list and save buffer
    SV_FreeThingArchive();
    Z_Free(saveBuffer);
#endif
}

static int saveStateWorker(const char* path, SaveInfo* saveInfo)
{
#if _DEBUG
    VERBOSE( Con_Message("SV_SaveGame: Attempting save game to \"%s\".", path) )
#endif

    if(!openGameSaveFile(path, true))
    {
        return SV_INVALIDFILENAME; // No success.
    }

    playerHeaderOK = false; // Uninitialized.

    // Write the game session header.
    { Writer* svWriter = SV_NewWriter();
    SaveInfo_Write(saveInfo, svWriter);
    Writer_Delete(svWriter);
    }

#if __JHEXEN__
    P_ArchiveGlobalScriptData();
#endif

    // In netgames the server tells the clients to save their games.
#if !__JHEXEN__
    NetSv_SaveGame(SaveInfo_GameId(saveInfo));
#endif

    // Set the mobj archive numbers.
    SV_InitThingArchive(false, true);

#if !__JHEXEN__
    SV_WriteLong(thingArchiveSize);
#endif

    // Create and populate the MaterialArchive.
#ifdef __JHEXEN__
    materialArchive = MaterialArchive_New(true /* segment check */);
#else
    materialArchive = MaterialArchive_New(false);
#endif

    P_ArchivePlayerHeader();
    P_ArchivePlayers();

    // Place a termination marker
    SV_BeginSegment(ASEG_END);

#if __JHEXEN__
    // Close the game session file (maps are saved into a seperate file).
    SV_CloseFile();
#endif

    // Save out the current map.
#if __JHEXEN__
    {
    // Compose the full name to the saved map file.
    AutoStr* mapPath = composeGameSavePathForSlot2(BASE_SLOT, gameMap+1);

    SV_OpenFile(Str_Text(mapPath), "wp");
    P_ArchiveMap(true); // true = save player info
    SV_CloseFile();
    }
#else
    P_ArchiveMap(true);
#endif

    // We are done with the MaterialArchive.
    MaterialArchive_Delete(materialArchive);
    materialArchive = NULL;

#if!__JHEXEN__
    // To be absolutely sure...
    SV_WriteByte(CONSISTENCY);

    SV_FreeThingArchive();
    SV_CloseFile();
#endif

    return SV_OK;
}

/**
 * Construct a new SaveInfo configured for the current game session.
 */
static SaveInfo* constructNewSaveInfo(const char* name)
{
    ddstring_t nameStr;
    SaveInfo* info = SaveInfo_New();
    SaveInfo_SetName(info, Str_InitStatic(&nameStr, name));
    SaveInfo_SetGameId(info, SV_GenerateGameId());
    SaveInfo_Configure(info);
    return info;
}

boolean SV_SaveGame(int slot, const char* name)
{
#if __JHEXEN__
    const int logicalSlot = BASE_SLOT;
#else
    const int logicalSlot = slot;
#endif
    SaveInfo* info;
    AutoStr* path;
    int saveError;
    assert(name);

    errorIfNotInited("SV_SaveGame");

    if(!SV_IsValidSlot(slot))
    {
        Con_Message("Warning: Invalid slot '%i' specified, game not saved.", slot);
        return false;
    }
    if(!name[0])
    {
        Con_Message("Warning: Empty name specified for slot #%i, game not saved.", slot);
        return false;
    }

    path = composeGameSavePathForSlot(logicalSlot);
    if(Str_IsEmpty(path))
    {
        Con_Message("Warning: Path \"%s\" is unreachable, game not saved.", SV_SavePath());
        return false;
    }

    info = constructNewSaveInfo(name);
    saveError = saveStateWorker(Str_Text(path), info);

    if(!saveError)
    {
        // Swap the save info.
        replaceSaveInfo(logicalSlot, info);

#if __JHEXEN__
        // Copy base slot to destination slot.
        SV_CopySlot(logicalSlot, slot);
#endif

        // The "last" save slot is now this.
        Con_SetInteger2("game-save-last-slot", slot, SVF_WRITE_OVERRIDE);
    }
    else
    {
        // We no longer need the info.
        SaveInfo_Delete(info);

        if(saveError == SV_INVALIDFILENAME)
        {
            Con_Message("Warning: Failed opening \"%s\" for writing.", Str_Text(path));
        }
    }

    return !saveError;
}

#if __JHEXEN__
void SV_HxSaveClusterMap(void)
{
    AutoStr* mapFilePath;

    errorIfNotInited("SV_HxSaveClusterMap");

    playerHeaderOK = false; // Uninitialized.

    // Set the mobj archive numbers
    SV_InitThingArchive(false, false);

    // Create and populate the MaterialArchive.
    materialArchive = MaterialArchive_New(true);

    // Compose the full path name to the saved map file.
    mapFilePath = composeGameSavePathForSlot2(BASE_SLOT, gameMap+1);
    SV_OpenFile(Str_Text(mapFilePath), "wp");
    P_ArchiveMap(false);

    // We are done with the MaterialArchive.
    MaterialArchive_Delete(materialArchive);
    materialArchive = NULL;

    // Close the output file
    SV_CloseFile();
}

void SV_HxLoadClusterMap(void)
{
    // Only unarchiveMap() uses targetPlayerAddrs, so it's NULLed here for the
    // following check (player mobj redirection).
    targetPlayerAddrs = NULL;

    playerHeaderOK = false; // Uninitialized.

    // Create the MaterialArchive.
    materialArchive = MaterialArchive_NewEmpty(true);

    // Been here before, load the previous map state.
    unarchiveMap(composeGameSavePathForSlot2(BASE_SLOT, gameMap+1));

    // We are done with the MaterialArchive.
    MaterialArchive_Delete(materialArchive);
    materialArchive = NULL;
}

void SV_HxBackupPlayersInCluster(playerbackup_t playerBackup[MAXPLAYERS])
{
    uint i;

    DENG_ASSERT(playerBackup);

    for(i = 0; i < MAXPLAYERS; ++i)
    {
        playerbackup_t* pb = playerBackup + i;
        player_t* plr = players + i;
        uint j;

        memcpy(&pb->player, plr, sizeof(player_t));

        // Make a copy of the inventory states also.
        for(j = 0; j < NUM_INVENTORYITEM_TYPES; ++j)
        {
            pb->numInventoryItems[j] = P_InventoryCount(i, j);
        }
        pb->readyItem = P_InventoryReadyItem(i);
    }
}

void SV_HxRestorePlayersInCluster(playerbackup_t playerBackup[MAXPLAYERS],
    uint entryPoint)
{
    uint i, j, k;
    mobj_t* targetPlayerMobj;

    DENG_ASSERT(playerBackup);

    for(i = 0; i < MAXPLAYERS; ++i)
    {
        playerbackup_t* pb = playerBackup + i;
        player_t* plr = players + i;
        ddplayer_t* ddplr = plr->plr;
        int oldKeys = 0, oldPieces = 0;
        boolean oldWeaponOwned[NUM_WEAPON_TYPES];
        boolean wasReborn;

        if(!ddplr->inGame) continue;

        memcpy(plr, &pb->player, sizeof(player_t));
        for(j = 0; j < NUM_INVENTORYITEM_TYPES; ++j)
        {
            // Don't give back the wings of wrath if reborn.
            if(j == IIT_FLY && plr->playerState == PST_REBORN)
                continue;

            for(k = 0; k < pb->numInventoryItems[j]; ++k)
            {
                P_InventoryGive(i, j, true);
            }
        }
        P_InventorySetReadyItem(i, pb->readyItem);

        ST_LogEmpty(i);
        plr->attacker = NULL;
        plr->poisoner = NULL;

        if(IS_NETGAME || deathmatch)
        {
            // In a network game, force all players to be alive
            if(plr->playerState == PST_DEAD)
            {
                plr->playerState = PST_REBORN;
            }

            if(!deathmatch)
            {
                // Cooperative net-play; retain keys and weapons.
                oldKeys = plr->keys;
                oldPieces = plr->pieces;
                for(j = 0; j < NUM_WEAPON_TYPES; j++)
                {
                    oldWeaponOwned[j] = plr->weapons[j].owned;
                }
            }
        }

        wasReborn = (plr->playerState == PST_REBORN);

        if(deathmatch)
        {
            memset(plr->frags, 0, sizeof(plr->frags));
            ddplr->mo = NULL;
            G_DeathMatchSpawnPlayer(i);
        }
        else
        {
            const playerstart_t* start;

            if((start = P_GetPlayerStart(entryPoint, i, false)))
            {
                const mapspot_t* spot = &mapSpots[start->spot];
                P_SpawnPlayer(i, cfg.playerClass[i], spot->origin[VX],
                              spot->origin[VY], spot->origin[VZ], spot->angle,
                              spot->flags, false, true);
            }
            else
            {
                P_SpawnPlayer(i, cfg.playerClass[i], 0, 0, 0, 0,
                              MSF_Z_FLOOR, true, true);
            }
        }

        if(wasReborn && IS_NETGAME && !deathmatch)
        {
            int bestWeapon;

            // Restore keys and weapons when reborn in co-op.
            plr->keys = oldKeys;
            plr->pieces = oldPieces;

            for(bestWeapon = 0, j = 0; j < NUM_WEAPON_TYPES; ++j)
            {
                if(oldWeaponOwned[j])
                {
                    bestWeapon = j;
                    plr->weapons[j].owned = true;
                }
            }

            plr->ammo[AT_BLUEMANA].owned = 25; /// @todo values.ded
            plr->ammo[AT_GREENMANA].owned = 25; /// @todo values.ded

            // Bring up the best weapon.
            if(bestWeapon)
            {
                plr->pendingWeapon = bestWeapon;
            }
        }
    }

    targetPlayerMobj = NULL;
    { uint i;
    for(i = 0; i < MAXPLAYERS; ++i)
    {
        player_t* plr = players + i;
        ddplayer_t* ddplr = plr->plr;

        if(!ddplr->inGame) continue;

        if(!targetPlayerMobj)
        {
            targetPlayerMobj = ddplr->mo;
        }
    }}

    /// @todo Redirect anything targeting a player mobj
    /// FIXME! This only supports single player games!!
    if(targetPlayerAddrs)
    {
        targetplraddress_t* p;

        for(p = targetPlayerAddrs; p; p = p->next)
        {
            *(p->address) = targetPlayerMobj;
        }
        SV_FreeTargetPlayerList();

        /* dj: - When XG is available in Hexen, call this after updating
                 target player references (after a load).
        // The activator mobjs must be set.
        XL_UpdateActivators();
        */
    }

    // Destroy all things touching players.
    P_TelefragMobjsTouchingPlayers();
}
#endif
