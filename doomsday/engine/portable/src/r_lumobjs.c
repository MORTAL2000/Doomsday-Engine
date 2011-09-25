/**\file r_lumobjs.c
 *\section License
 * License: GPL
 * Online License Link: http://www.gnu.org/licenses/gpl.html
 *
 *\author Copyright © 2003-2011 Jaakko Keränen <jaakko.keranen@iki.fi>
 *\author Copyright © 2006-2011 Daniel Swanson <danij@dengine.net>
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

/**
 * Lumobj (luminous object) management.
 */

// HEADER FILES ------------------------------------------------------------

#include <math.h>

#include "de_base.h"
#include "de_console.h"
#include "de_refresh.h"
#include "de_render.h"
#include "de_graphics.h"
#include "de_misc.h"
#include "de_play.h"
#include "de_defs.h"

#include "sys_opengl.h"
#include "texture.h"
#include "materialvariant.h"

// MACROS ------------------------------------------------------------------

BEGIN_PROF_TIMERS()
  PROF_LUMOBJ_INIT_ADD,
  PROF_LUMOBJ_FRAME_SORT
END_PROF_TIMERS()

// TYPES -------------------------------------------------------------------

typedef struct lumlistnode_s {
    struct lumlistnode_s* next;
    struct lumlistnode_s* nextUsed;
    void* data;
} lumlistnode_t;

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

static boolean iterateSubsectorLumObjs(subsector_t* ssec, boolean (*func) (void*, void*), void* data);

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

extern int useBias;

// PUBLIC DATA DEFINITIONS -------------------------------------------------

boolean loInited = false;
uint loMaxLumobjs = 0;

int loMaxRadius = 256; // Dynamic lights maximum radius.
float loRadiusFactor = 3;

int useMobjAutoLights = true; // Enable automaticaly calculated lights
                              // attached to mobjs.
byte rendInfoLums = false;
byte devDrawLums = false; // Display active lumobjs?

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static zblockset_t* luminousBlockSet = NULL;
static uint numLuminous = 0, maxLuminous = 0;
static lumobj_t** luminousList = NULL;
static float* luminousDist = NULL;
static byte* luminousClipped = NULL;
static uint* luminousOrder = NULL;

// List of unused and used list nodes, for linking lumobjs with subsectors.
static lumlistnode_t* listNodeFirst = NULL, *listNodeCursor = NULL;

// List of lumobjs for each subsector;
static lumlistnode_t** subLumObjList = NULL;

// CODE --------------------------------------------------------------------

void LO_Register(void)
{
    C_VAR_INT("rend-mobj-light-auto", &useMobjAutoLights, 0, 0, 1);
    C_VAR_INT("rend-light-num", &loMaxLumobjs, CVF_NO_MAX, 0, 0);
    C_VAR_FLOAT("rend-light-radius-scale", &loRadiusFactor, 0, 0.1f, 10);
    C_VAR_INT("rend-light-radius-max", &loMaxRadius, 0, 64, 512);

    C_VAR_BYTE("rend-info-lums", &rendInfoLums, 0, 0, 1);
    C_VAR_BYTE("rend-dev-lums", &devDrawLums, CVF_NO_ARCHIVE, 0, 1);
}

static lumlistnode_t* allocListNode(void)
{
    lumlistnode_t* ln;

    if(listNodeCursor == NULL)
    {
        ln = Z_Malloc(sizeof(*ln), PU_APPSTATIC, 0);

        // Link to the list of list nodes.
        ln->nextUsed = listNodeFirst;
        listNodeFirst = ln;
    }
    else
    {
        ln = listNodeCursor;
        listNodeCursor = listNodeCursor->nextUsed;
    }

    ln->next = NULL;
    ln->data = NULL;

    return ln;
}

static void linkLumObjToSSec(lumobj_t* lum, subsector_t* ssec)
{
    lumlistnode_t* ln = allocListNode();
    lumlistnode_t** root;

    root = &subLumObjList[GET_SUBSECTOR_IDX(ssec)];
    ln->next = *root;
    ln->data = lum;
    *root = ln;
}

static uint lumToIndex(const lumobj_t* lum)
{
    uint i;
    for(i = 0; i < numLuminous; ++i)
        if(luminousList[i] == lum)
            return i;
    Con_Error("lumToIndex: Invalid lumobj.\n");
    return 0;
}

void LO_InitForMap(void)
{
    // First initialize the subsector links (root pointers).
    subLumObjList = Z_Calloc(sizeof(*subLumObjList) * numSSectors, PU_MAPSTATIC, 0);

    maxLuminous = 0;
    luminousBlockSet = 0; // Will have already been free'd.
}

void LO_Clear(void)
{
    if(luminousBlockSet)
        ZBlockSet_Delete(luminousBlockSet);
    luminousBlockSet = 0;

    if(luminousList)
        M_Free(luminousList);
    luminousList = 0;

    if(luminousDist)
        M_Free(luminousDist);
    luminousDist = 0;

    if(luminousClipped)
        M_Free(luminousClipped);
    luminousClipped = 0;

    if(luminousOrder)
        M_Free(luminousOrder);
    luminousOrder = 0;

    maxLuminous = numLuminous = 0;
}

void LO_BeginWorldFrame(void)
{
#ifdef DD_PROFILE
    static int i;

    if(++i > 40)
    {
        i = 0;
        PRINT_PROF(PROF_LUMOBJ_INIT_ADD);
        PRINT_PROF(PROF_LUMOBJ_FRAME_SORT);
    }
#endif

    // Start reusing nodes from the first one in the list.
    listNodeCursor = listNodeFirst;
    if(subLumObjList)
        memset(subLumObjList, 0, sizeof(lumlistnode_t*) * numSSectors);
    numLuminous = 0;
}

uint LO_GetNumLuminous(void)
{
    return numLuminous;
}

static lumobj_t* allocLumobj(void)
{
#define LUMOBJ_BATCH_SIZE       (32)

    lumobj_t* lum;

    // Only allocate memory when it's needed.
    // \fixme No upper limit?
    if(++numLuminous > maxLuminous)
    {
        uint i, newMax = maxLuminous + LUMOBJ_BATCH_SIZE;

        if(!luminousBlockSet)
        {
            luminousBlockSet = ZBlockSet_New(sizeof(lumobj_t), LUMOBJ_BATCH_SIZE, PU_MAP);
        }

        luminousList = M_Realloc(luminousList, sizeof(lumobj_t*) * newMax);

        // Add the new lums to the end of the list.
        for(i = maxLuminous; i < newMax; ++i)
            luminousList[i] = ZBlockSet_Allocate(luminousBlockSet);

        maxLuminous = newMax;

        // Resize the associated buffers used for per-frame stuff.
        luminousDist =
            M_Realloc(luminousDist, sizeof(*luminousDist) * maxLuminous);
        luminousClipped =
            M_Realloc(luminousClipped, sizeof(*luminousClipped) * maxLuminous);
        luminousOrder =
            M_Realloc(luminousOrder, sizeof(*luminousOrder) * maxLuminous);
    }

    lum = luminousList[numLuminous - 1];
    memset(lum, 0, sizeof(*lum));

    return lum;

#undef LUMOBJ_BATCH_SIZE
}

static lumobj_t* createLuminous(lumtype_t type, subsector_t* ssec)
{
    lumobj_t* lum = allocLumobj();

    lum->type = type;
    lum->subsector = ssec;
    linkLumObjToSSec(lum, ssec);

    if(type != LT_PLANE)
        R_ObjLinkCreate(lum, OT_LUMOBJ); // For spreading purposes.

    return lum;
}

uint LO_NewLuminous(lumtype_t type, subsector_t* ssec)
{
    createLuminous(type, ssec);
    return numLuminous; // == index + 1
}

lumobj_t* LO_GetLuminous(uint idx)
{
    if(!(idx == 0 || idx > numLuminous))
        return luminousList[idx - 1];
    return NULL;
}

uint LO_ToIndex(const lumobj_t* lum)
{
    return lumToIndex(lum)+1;
}

boolean LO_IsClipped(uint idx, int i)
{
    if(!(idx == 0 || idx > numLuminous))
        return (luminousClipped[idx - 1]? true : false);
    return false;
}

boolean LO_IsHidden(uint idx, int i)
{
    if(!(idx == 0 || idx > numLuminous))
        return (luminousClipped[idx - 1] == 2? true : false);
    return false;
}

float LO_DistanceToViewer(uint idx, int i)
{
    if(!(idx == 0 || idx > numLuminous))
        return luminousDist[idx - 1];
    return 0;
}

float LO_AttenuationFactor(uint idx, float distance)
{
    lumobj_t* lum = LO_GetLuminous(idx);
    if(lum)
    switch(lum->type)
    {
    case LT_OMNI:
        if(distance <= 0) return 1;
        if(distance > lum->maxDistance) return 0;
        if(distance > .67f * lum->maxDistance)
            return (lum->maxDistance - distance) / (.33f * lum->maxDistance);
        break;
    case LT_PLANE: break;
    default:
        Con_Error("LO_AttenuationFactor: Invalid lumobj type %i.", (int)lum->type);
        exit(1); // Unreachable.
    }
    return 1;
}

/**
 * Registers the given mobj as a luminous, light-emitting object.
 * \note: This is called each frame for each luminous object!
 *
 * @param mo  Ptr to the mobj to register.
 */
static void addLuminous(mobj_t* mo)
{
    uint i;
    float mul, center;
    int radius;
    float rgb[3], yOffset, size;
    lumobj_t* l;
    ded_light_t* def;
    spritedef_t* sprDef;
    spriteframe_t* sprFrame;
    spritetex_t* sprTex;
    material_t* mat;
    float autoLightColor[3];
    material_snapshot_t ms;
    const pointlight_analysis_t* pl;

    if(!(((mo->state && (mo->state->flags & STF_FULLBRIGHT)) &&
         !(mo->ddFlags & DDMF_DONTDRAW)) ||
       (mo->ddFlags & DDMF_ALWAYSLIT)))
        return;

    // Are the automatically calculated light values for fullbright
    // sprite frames in use?
    if(mo->state &&
       (!useMobjAutoLights || (mo->state->flags & STF_NOAUTOLIGHT)) &&
       !stateLights[mo->state - states])
       return;

    def = (mo->state? stateLights[mo->state - states] : NULL);

    // Determine the sprite frame lump of the source.
    sprDef = &sprites[mo->sprite];
    sprFrame = &sprDef->spriteFrames[mo->frame];
    // Always use rotation zero.
    mat = sprFrame->mats[0];

#if _DEBUG
if(!mat)
Con_Error("LO_AddLuminous: Sprite '%i' frame '%i' missing material.",
          (int) mo->sprite, mo->frame);
#endif

    // Ensure we have up-to-date information about the material.
    Materials_Prepare(&ms, mat, true,
        Materials_VariantSpecificationForContext(MC_SPRITE, 0, 1, 0, 0,
            GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, 1, -2, -1, true, true, true, false));
    pl = (const pointlight_analysis_t*) Texture_Analysis(
        MSU(&ms, MTU_PRIMARY).tex.texture, TA_SPRITE_AUTOLIGHT);
    if(NULL == pl)
        return; // Not good...

    size = pl->brightMul;
    yOffset = pl->originY;
    // Does the mobj have an active light definition?
    if(def)
    {
        if(def->size)
            size = def->size;
        if(def->offset[VY])
            yOffset = def->offset[VY];
    }

    autoLightColor[CR] = pl->color[CR];
    autoLightColor[CG] = pl->color[CG];
    autoLightColor[CB] = pl->color[CB];

    sprTex = R_SpriteTextureByIndex(Texture_TypeIndex(MSU(&ms, MTU_PRIMARY).tex.texture));
    assert(NULL != sprTex);

    center = sprTex->offY - mo->floorClip - R_GetBobOffset(mo) - yOffset;

    // Will the sprite be allowed to go inside the floor?
    mul = mo->pos[VZ] + sprTex->offY - (float) ms.height - mo->subsector->sector->SP_floorheight;
    if(!(mo->ddFlags & DDMF_NOFITBOTTOM) && mul < 0)
    {
        // Must adjust.
        center -= mul;
    }

    radius = size * 40 * loRadiusFactor;

    // Don't make a too small light.
    if(radius < 32)
        radius = 32;

    // Does the mobj use a light scale?
    if(mo->ddFlags & DDMF_LIGHTSCALE)
    {
        // Also reduce the size of the light according to
        // the scale flags. *Won't affect the flare.*
        mul =
            1.0f -
            ((mo->ddFlags & DDMF_LIGHTSCALE) >> DDMF_LIGHTSCALESHIFT) /
            4.0f;
        radius *= mul;
    }

    // If any of the color components are != 0, use the def's color.
    if(def && (def->color[0] || def->color[1] || def->color[2]))
    {
        for(i = 0; i < 3; ++i)
            rgb[i] = def->color[i];
    }
    else
    {   // Use the auto-calculated color.
        for(i = 0; i < 3; ++i)
            rgb[i] = autoLightColor[i];
    }

    // This'll allow a halo to be rendered. If the light is hidden from
    // view by world geometry, the light pointer will be set to NULL.
    mo->lumIdx = LO_NewLuminous(LT_OMNI, mo->subsector);

    l = LO_GetLuminous(mo->lumIdx);
    l->pos[VX] = mo->pos[VX];
    l->pos[VY] = mo->pos[VY];
    l->pos[VZ] = mo->pos[VZ];
    l->maxDistance = 0;
    l->decorSource = NULL;

    // Don't make too large a light.
    if(radius > loMaxRadius)
        radius = loMaxRadius;

    LUM_OMNI(l)->radius = radius;
    for(i = 0; i < 3; ++i)
        LUM_OMNI(l)->color[i] = rgb[i];
    LUM_OMNI(l)->zOff = center;

    if(def)
    {
        LUM_OMNI(l)->tex = GL_GetLightMapTexture(def->sides);
        LUM_OMNI(l)->ceilTex = GL_GetLightMapTexture(def->up);
        LUM_OMNI(l)->floorTex = GL_GetLightMapTexture(def->down);
    }
    else
    {
        // Use the same default light texture for all directions.
        LUM_OMNI(l)->tex = LUM_OMNI(l)->ceilTex =
            LUM_OMNI(l)->floorTex = GL_PrepareLSTexture(LST_DYNAMIC);
    }
}

/// Used to sort lumobjs by distance from viewpoint.
static int C_DECL lumobjSorter(const void* e1, const void* e2)
{
    float a = luminousDist[*(const uint *) e1];
    float b = luminousDist[*(const uint *) e2];
    if(a > b) return 1;
    if(a < b) return -1;
    return 0;
}

void LO_BeginFrame(void)
{
    const viewdata_t* viewData = R_ViewData(viewPlayer - ddPlayers);
    uint i;

    if(!(numLuminous > 0))
        return;

BEGIN_PROF( PROF_LUMOBJ_FRAME_SORT );

    // Update lumobj distances ready for linking and sorting.
    for(i = 0; i < numLuminous; ++i)
    {
        lumobj_t* lum = luminousList[i];
        float pos[3];

        V3_Subtract(pos, lum->pos, viewData->current.pos);

        // Approximate the distance in 3D.
        luminousDist[i] = P_ApproxDistance3(pos[VX], pos[VY], pos[VZ]);
    }

    if(loMaxLumobjs > 0 && numLuminous > loMaxLumobjs)
    {   // Sort lumobjs by distance from the viewer. Then clip all lumobjs
        // so that only the closest are visible (max loMaxLumobjs).
        uint n;

        // Init the lumobj indices, sort array.
        for(i = 0; i < numLuminous; ++i)
            luminousOrder[i] = i;

        qsort(luminousOrder, numLuminous, sizeof(uint), lumobjSorter);

        // Mark all as hidden.
        memset(luminousClipped, 2, numLuminous * sizeof(*luminousClipped));

        n = 0;
        for(i = 0; i < numLuminous; ++i)
        {
            if(n++ > loMaxLumobjs)
                break;

            // Unhide this lumobj.
            luminousClipped[luminousOrder[i]] = 1;
        }
    }
    else
    {
        // Mark all as clipped.
        memset(luminousClipped, 1, numLuminous * sizeof(*luminousClipped));
    }

    // objLinks already contains links if there are any light decorations
    // currently in use.
    loInited = true;

END_PROF( PROF_LUMOBJ_FRAME_SORT );
}

static __inline void setGlowLightProps(lumobj_t* l, surface_t* surface)
{
    assert(l && surface);
    {
    material_snapshot_t ms;
    Materials_Prepare(&ms, surface->material, true,
        Materials_VariantSpecificationForContext(MC_MAPSURFACE, 0, 0, 0, 0,
            GL_REPEAT, GL_REPEAT, -1, -1, -1, true, true, false, false));
    V3_Copy(LUM_PLANE(l)->normal, ((plane_t*)surface->owner)->PS_normal);
    V3_Copy(LUM_PLANE(l)->color, ms.colorAmplified);
    LUM_PLANE(l)->intensity = ms.glowing;
    LUM_PLANE(l)->tex = GL_PrepareLSTexture(LST_GRADIENT);
    l->maxDistance = 0;
    l->decorSource = 0;
    }
}

/**
 * Generate one dynlight node for each plane glow.
 * The light is attached to the appropriate dynlight node list.
 *
 * @param ssec  Ptr to the subsector to process.
 */
static boolean createGlowLightForSurface(surface_t* suf, void* paramaters)
{
    static material_snapshot_t ms;

    switch(DMU_GetType(suf->owner))
    {
    case DMU_PLANE:
        {
        plane_t* pln = (plane_t*)suf->owner;
        sector_t* sec = pln->sector;
        linkobjtossecparams_t params;
        lumobj_t* lum;

        // Only produce a light for sectors with open space.
        /// \todo Do not add surfaces from sectors with zero subsectors to the glowing list.
        if(!sec->ssectorCount || sec->SP_floorvisheight >= sec->SP_ceilvisheight)
            return true; // Continue iteration.

        // Are we glowing at this moment in time?
        Materials_Prepare(&ms, suf->material, true,
            Materials_VariantSpecificationForContext(MC_MAPSURFACE, 0, 0, 0, 0,
                GL_REPEAT, GL_REPEAT, -1, -1, -1, true, true, false, false));
        if(!(ms.glowing > .0001f))
            return true; // Continue iteration.

        // \note Plane lights do not spread so simply link to all subsectors of this sector.
        lum = createLuminous(LT_PLANE, sec->ssectors[0]);
        V3_Set(lum->pos, pln->soundOrg.pos[VX], pln->soundOrg.pos[VY], pln->visHeight);
        setGlowLightProps(lum, suf);

        params.obj = lum;
        params.type = OT_LUMOBJ;
        RIT_LinkObjToSubsector(sec->ssectors[0], (void*)&params);
        { uint i;
        for(i = 1; i < sec->ssectorCount; ++i)
        {
            linkLumObjToSSec(lum, sec->ssectors[i]);
            RIT_LinkObjToSubsector(sec->ssectors[i], (void*)&params);
        }}
        break;
        }
    case DMU_SIDEDEF:
        return true; // Not yet supported by this algorithm.

    default:
        Con_Error("createGlowLightForSurface: Internal error, unknown type %s.",
                  DMU_Str(DMU_GetType(suf->owner)));
    }
    return true;
}

void LO_AddLuminousMobjs(void)
{
    if(!useDynlights && !useWallGlow)
        return;

BEGIN_PROF( PROF_LUMOBJ_INIT_ADD );

    if(useDynlights)
    {
        sector_t* seciter;
        uint i;
        for(i = 0, seciter = sectors; i < numSectors; seciter++, ++i)
        {
            mobj_t* iter;
            for(iter = seciter->mobjList; iter; iter = iter->sNext)
            {
                iter->lumIdx = 0;
                addLuminous(iter);
            }
        }
    }

    // If the segs of this subsector are affected by glowing planes we need
    // to create dynlights and link them.
    if(useWallGlow)
    {
        R_SurfaceListIterate(glowingSurfaceList, createGlowLightForSurface, 0);
    }

END_PROF( PROF_LUMOBJ_INIT_ADD );
}

typedef struct lumobjiterparams_s {
    float origin[2];
    float radius;
    void* paramaters;
    int (*callback) (const lumobj_t*, float distance, void* paramaters);
} lumobjiterparams_t;

int LOIT_RadiusLumobjs(void* ptr, void* paramaters)
{
    const lumobj_t* lum = (const lumobj_t*) ptr;
    lumobjiterparams_t* p = (lumobjiterparams_t*)paramaters;
    float dist = P_ApproxDistance(lum->pos[VX] - p->origin[VX], lum->pos[VY] - p->origin[VY]);
    int result = 0; // Continue iteration.
    if(dist <= p->radius)
    {
        result = p->callback(lum, dist, p->paramaters);
    }
    return result;
}

int LO_LumobjsRadiusIterator2(subsector_t* ssec, float x, float y, float radius,
    int (*callback) (const lumobj_t*, float distance, void* paramaters), void* paramaters)
{
    lumobjiterparams_t p;
    if(!ssec || !callback) return 0;

    p.origin[VX] = x;
    p.origin[VY] = y;
    p.radius = radius;
    p.callback = callback;
    p.paramaters = paramaters;

    return R_IterateSubsectorContacts2(ssec, OT_LUMOBJ, LOIT_RadiusLumobjs, (void*) &p);
}

int LO_LumobjsRadiusIterator(subsector_t* ssec, float x, float y, float radius,
    int (*callback) (const lumobj_t*, float distance, void* paramaters))
{
    return LO_LumobjsRadiusIterator2(ssec, x, y, radius, callback, NULL);
}

boolean LOIT_ClipLumObj(void* data, void* context)
{
    lumobj_t* lum = (lumobj_t*) data;
    uint lumIdx = lumToIndex(lum);
    vec3_t pos;

    if(lum->type != LT_OMNI)
        return true; // Only interested in omnilights.

    if(luminousClipped[lumIdx] > 1)
        return true; // Already hidden by some other means.

    luminousClipped[lumIdx] = 0;

    // \fixme Determine the exact centerpoint of the light in
    // addLuminous!
    V3_Set(pos, lum->pos[VX], lum->pos[VY], lum->pos[VZ] + LUM_OMNI(lum)->zOff);

    /**
     * Select clipping strategy:
     *
     * If culling world surfaces with the angle clipper and the viewer is
     * not in the void; use the angle clipper here too. Otherwise, use the
     * BSP-based LOS algorithm.
     */
    if(!(devNoCulling || P_IsInVoid(&ddPlayers[displayPlayer])))
    {
        if(!C_IsPointVisible(pos[VX], pos[VY], pos[VZ]))
            luminousClipped[lumIdx] = 1; // Won't have a halo.
    }
    else
    {
        vec3_t              vpos;

        V3_Set(vpos, vx, vz, vy);

        luminousClipped[lumIdx] = 1;
        if(P_CheckLineSight(vpos, pos, -1, 1, LS_PASSLEFT | LS_PASSOVER | LS_PASSUNDER))
        {
            luminousClipped[lumIdx] = 0; // Will have a halo.
        }
    }

    return true; // Continue iteration.
}

void LO_ClipInSubsector(uint ssecidx)
{
    iterateSubsectorLumObjs(&ssectors[ssecidx], LOIT_ClipLumObj, NULL);
}

boolean LOIT_ClipLumObjBySight(void* data, void* context)
{
    lumobj_t* lum = (lumobj_t*) data;
    uint lumIdx = lumToIndex(lum);
    subsector_t* ssec = (subsector_t*) context;

    if(lum->type != LT_OMNI)
        return true; // Only interested in omnilights.

    if(!luminousClipped[lumIdx])
    {
        vec2_t eye;
        uint i;

        V2_Set(eye, vx, vz);

        // We need to figure out if any of the polyobj's segments lies
        // between the viewpoint and the lumobj.
        for(i = 0; i < ssec->polyObj->numSegs; ++i)
        {
            seg_t* seg = ssec->polyObj->segs[i];

            // Ignore segs facing the wrong way.
            if(seg->frameFlags & SEGINF_FACINGFRONT)
            {
                vec2_t source;

                V2_Set(source, lum->pos[VX], lum->pos[VY]);
                if(V2_Intercept2(source, eye, seg->SG_v1pos, seg->SG_v2pos, NULL, NULL, NULL))
                {
                    luminousClipped[lumIdx] = 1;
                    break;
                }
            }
        }
    }

    return true; // Continue iteration.
}

void LO_ClipInSubsectorBySight(uint ssecidx)
{
    iterateSubsectorLumObjs(&ssectors[ssecidx], LOIT_ClipLumObjBySight, &ssectors[ssecidx]);
}

static boolean iterateSubsectorLumObjs(subsector_t* ssec, boolean (*func) (void*, void*),
    void* data)
{
    lumlistnode_t* ln = subLumObjList[GET_SUBSECTOR_IDX(ssec)];
    while(ln)
    {
        if(!func(ln->data, data))
            return false;
        ln = ln->next;
    }
    return true;
}

void LO_UnlinkMobjLumobj(mobj_t* mo)
{
    mo->lumIdx = 0;
}

boolean LOIT_UnlinkMobjLumobj(thinker_t* th, void* context)
{
    LO_UnlinkMobjLumobj((mobj_t*) th);
    return true; // Continue iteration.
}

void LO_UnlinkMobjLumobjs(void)
{
    if(!useDynlights)
    {
        // Mobjs are always public.
        P_IterateThinkers(gx.MobjThinker, 0x1, LOIT_UnlinkMobjLumobj, NULL);
    }
}

void LO_DrawLumobjs(void)
{
    static const float  black[4] = { 0, 0, 0, 0 };
    float color[4];
    uint i;

    if(!devDrawLums)
        return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    for(i = 0; i < numLuminous; ++i)
    {
        lumobj_t* lum = luminousList[i];
        vec3_t lumCenter;

        if(!(lum->type == LT_OMNI || lum->type == LT_PLANE))
            continue;

        if(lum->type == LT_OMNI && loMaxLumobjs > 0 && luminousClipped[i] == 2)
            continue;

        V3_Copy(lumCenter, lum->pos);
        if(lum->type == LT_OMNI)
            lumCenter[VZ] += LUM_OMNI(lum)->zOff;

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();

        glTranslatef(lumCenter[VX], lumCenter[VZ], lumCenter[VY]);

        switch(lum->type)
        {
        case LT_OMNI: {
            float scale = LUM_OMNI(lum)->radius;

            color[CR] = LUM_OMNI(lum)->color[CR];
            color[CG] = LUM_OMNI(lum)->color[CG];
            color[CB] = LUM_OMNI(lum)->color[CB];
            color[CA] = 1;

            glBegin(GL_LINES);
            {
                glColor4fv(black);
                glVertex3f(-scale, 0, 0);
                glColor4fv(color);
                glVertex3f(0, 0, 0);
                glVertex3f(0, 0, 0);
                glColor4fv(black);
                glVertex3f(scale, 0, 0);

                glVertex3f(0, -scale, 0);
                glColor4fv(color);
                glVertex3f(0, 0, 0);
                glVertex3f(0, 0, 0);
                glColor4fv(black);
                glVertex3f(0, scale, 0);

                glVertex3f(0, 0, -scale);
                glColor4fv(color);
                glVertex3f(0, 0, 0);
                glVertex3f(0, 0, 0);
                glColor4fv(black);
                glVertex3f(0, 0, scale);
            }
            glEnd();
            break;
          }
        case LT_PLANE: {
            float scale = LUM_PLANE(lum)->intensity * 200;

            color[CR] = LUM_PLANE(lum)->color[CR];
            color[CG] = LUM_PLANE(lum)->color[CG];
            color[CB] = LUM_PLANE(lum)->color[CB];
            color[CA] = 1;

            glBegin(GL_LINES);
            {
                glColor4fv(black);
                glVertex3f(scale * LUM_PLANE(lum)->normal[VX],
                             scale * LUM_PLANE(lum)->normal[VZ],
                             scale * LUM_PLANE(lum)->normal[VY]);
                glColor4fv(color);
                glVertex3f(0, 0, 0);

            }
            glEnd();
            break;
          }
        default: break;
        }

        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
    }

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}
