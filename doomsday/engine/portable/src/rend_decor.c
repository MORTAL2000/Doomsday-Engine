/**\file
 *\section Copyright and License Summary
 * License: GPL
 * Online License Link: http://www.gnu.org/licenses/gpl.html
 *
 *\author Copyright © 2003-2006 Jaakko Keränen <skyjake@dengine.net>
 *\author Copyright © 2006 Daniel Swanson <danij@dengine.net>
 *\author Copyright © 2006 Jamie Jones <yagisan@dengine.net>
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

/*
 * rend_decor.c: Decorations
 *
 * Surface decorations (dynamic lights).
 */

// HEADER FILES ------------------------------------------------------------

#include "de_base.h"
#include "de_play.h"
#include "de_refresh.h"
#include "de_graphics.h"
#include "de_render.h"
#include "de_misc.h"

// MACROS ------------------------------------------------------------------

// Quite a bit of lights, there!
#define MAX_SOURCES     16384

// TYPES -------------------------------------------------------------------

typedef struct decorsource_s {
    mobj_t  thing;
    struct decorsource_s *next;
} decorsource_t;

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

// PUBLIC DATA DEFINITIONS -------------------------------------------------

byte    useDecorations = true;
float   decorWallMaxDist = 1500;    // No decorations are visible beyond this.
float   decorPlaneMaxDist = 1500;
float   decorWallFactor = 1;
float   decorPlaneFactor = 1;
float   decorFadeAngle = .1f;

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static unsigned int numDecorLightSources;
static decorsource_t *sourceFirst, *sourceLast, *sourceCursor;

// Lights near surfaces get dimmer if the angle is too small.
static float surfaceNormal[3];

// CODE --------------------------------------------------------------------

void Rend_DecorRegister(void)
{
    C_VAR_BYTE("rend-light-decor", &useDecorations, 0, 0, 1);
    C_VAR_FLOAT("rend-light-decor-plane-far", &decorPlaneMaxDist, CVF_NO_MAX,
                0, 0);
    C_VAR_FLOAT("rend-light-decor-wall-far", &decorWallMaxDist, CVF_NO_MAX,
                0, 0);
    C_VAR_FLOAT("rend-light-decor-plane-bright", &decorPlaneFactor, 0, 0,
                10);
    C_VAR_FLOAT("rend-light-decor-wall-bright", &decorWallFactor, 0, 0, 10);
    C_VAR_FLOAT("rend-light-decor-angle", &decorFadeAngle, 0, 0, 1);
}

/**
 * Returns a pointer to the surface decoration, if any.
 */
static ded_decor_t *Rend_GetGraphicResourceDecoration(int id, boolean isFlat)
{
    if(!id)
        return NULL;

    if(isFlat)
    {
        flat_t *flat = R_GetFlat(id);

        // Get the translated one?
        if(flat->translation.current != id)
        {
            flat = R_GetFlat(flat->translation.current);
        }
        return flat->decoration;
    }
    else
        return textures[texturetranslation[id].current]->decoration;
}

/**
 * Clears the list of decoration dummies.
 */
static void Rend_ClearDecorations(void)
{
    numDecorLightSources = 0;
    sourceCursor = sourceFirst;
}

/**
 * Project all the non-clipped decorations. They become regular vissprites.
 * This is needed for rendering halos.
 */
void Rend_ProjectDecorations(void)
{
    decorsource_t *src;

    // No need for this if no halos are rendered.
    if(!haloMode)
        return;

    //for(i = 0; i < numDecorLightSources; i++)
    for(src = sourceFirst; src != sourceCursor; src = src->next)
    {
        lumobj_t *lum = DL_GetLuminous(src->thing.light);

        // Clipped sources don't get halos.
        if(lum->flags & LUMF_CLIPPED || lum->flareSize <= 0)
            continue;

        R_ProjectDecoration(&src->thing);
    }
}

/**
 * Create a new source for a light decoration.
 */
static decorsource_t *Rend_NewLightDecorationSource(void)
{
    decorsource_t *src;

    if(numDecorLightSources > MAX_SOURCES)
        return NULL;

    numDecorLightSources++;

    // If the cursor is NULL, new sources must be allocated.
    if(!sourceCursor)
    {
        // Allocate a new entry.
        src = Z_Calloc(sizeof(decorsource_t), PU_STATIC, NULL);

        if(sourceLast)
            sourceLast->next = src;
        sourceLast = src;

        if(!sourceFirst)
            sourceFirst = src;
    }
    else
    {
        // There are old sources to use.
        src = sourceCursor;
        memset(&src->thing, 0, sizeof(src->thing));

        // Advance the cursor.
        sourceCursor = sourceCursor->next;
    }

    return src;
}

/**
 * A light decoration is created in the specified coordinates.
 * Does largely the same thing as DL_AddLuminous().
 */
static void Rend_AddLightDecoration(float pos[3], ded_decorlight_t *def,
                                    float brightness, boolean isWall,
                                    DGLuint decorMap)
{
    decorsource_t *source;
    lumobj_t   *lum;
    float       distance = Rend_PointDist3D(pos);
    float       fadeMul = 1, flareMul = 1;
    float       maxDist = (isWall ? decorWallMaxDist : decorPlaneMaxDist);
    unsigned int i;

    // Is the point in range?
    if(distance > maxDist)
        return;

    // Close enough to the maximum distance, the lights fade out.
    if(distance > .67f * maxDist)
    {
        fadeMul = (maxDist - distance) / (.33f * maxDist);
    }

    // Apply the brightness factor (was calculated using sector lightlevel).
    fadeMul *= brightness * (isWall ? decorWallFactor : decorPlaneFactor);

    // Brightness drops as the angle gets too big.
    if(def->elevation < 2 && decorFadeAngle > 0)    // Close the surface?
    {
        float   vector[3] = { pos[VX] - vx, pos[VZ] - vy, pos[VY] - vz };
        float   dot;

        M_Normalize(vector);
        dot =
            -(surfaceNormal[VX] * vector[VX] + surfaceNormal[VY] * vector[VY] +
              surfaceNormal[VZ] * vector[VZ]);
        if(dot < decorFadeAngle / 2)
        {
            flareMul = 0;
        }
        else if(dot < 3 * decorFadeAngle)
        {
            flareMul *= (dot - decorFadeAngle / 2) / (2.5f * decorFadeAngle);
        }
    }

    if(fadeMul <= 0)
        return;

    if(!(source = Rend_NewLightDecorationSource()))
        return;                 // Out of sources!

    // Initialize the essentials in the dummy mobj.
    source->thing.pos[VX] = pos[VX] * FRACUNIT;
    source->thing.pos[VY] = pos[VY] * FRACUNIT;
    source->thing.pos[VZ] = pos[VZ] * FRACUNIT;
    source->thing.ddflags = DDMF_ALWAYSLIT;
    source->thing.halofactor = 0xff;    // Assumed visible.
    source->thing.subsector =
        R_PointInSubsector(source->thing.pos[VX], source->thing.pos[VY]);

    // Fill in the data for a new luminous object.
    source->thing.light = DL_NewLuminous();
    lum = DL_GetLuminous(source->thing.light);
    lum->thing = &source->thing;
    lum->center = 0;
    lum->flags = LUMF_CLIPPED;
    lum->tex = def->sides.tex;
    lum->ceilTex = def->up.tex;
    lum->floorTex = def->down.tex;

    // These are the same rules as in DL_ThingRadius().
    lum->radius = def->radius * 40 * dlRadFactor;

    // Don't make a too small or too large light.
    if(lum->radius > dlMaxRad)
        lum->radius = dlMaxRad;

    if(def->halo_radius > 0)
    {
        lum->flareSize = def->halo_radius * 60 * (50 + haloSize) / 100.0f;
        if(lum->flareSize < 1)
            lum->flareSize = 1;
    }
    else
    {
        lum->flareSize = 0;
    }

    if(def->flare.disabled)
        lum->flags |= LUMF_NOHALO;
    else
    {
        lum->flareCustom = def->flare.custom;
        lum->flareTex = def->flare.tex;
    }

    lum->flareMul = flareMul;

    // This light source is associated with a decoration map, if one is
    // available.
    lum->decorMap = decorMap;

    for(i = 0; i < 3; ++i)
        lum->rgb[i] = (byte) (255 * def->color[i] * fadeMul);

    // Approximate the distance.
    lum->distance =
        P_ApproxDistance3(source->thing.pos[VX] - viewx,
                          source->thing.pos[VY] - viewy,
                          source->thing.pos[VZ] - viewz);
}

/**
 * Returns true if the view point is close enough to the bounding box
 * so that there could be visible decorations inside.
 */
static boolean Rend_CheckDecorationBounds(fixed_t bounds[6], float fMaxDist)
{
    fixed_t maxDist = FRACUNIT * fMaxDist;

    return viewx > bounds[BLEFT] - maxDist   && viewx < bounds[BRIGHT] + maxDist
        && viewy > bounds[BBOTTOM] - maxDist && viewy < bounds[BTOP] + maxDist
        && viewz > bounds[BFLOOR] - maxDist  && viewz < bounds[BCEILING] + maxDist;
}

/**
 * Returns > 0 if the sector lightlevel passes the limit condition.
 */
static float Rend_CheckSectorLight(sector_t *sector, ded_decorlight_t *lightDef)
{
    int    lightlevel;
    float   factor;

    lightlevel = sector->lightlevel;

    // Has a limit been set?
    if(lightDef->light_levels[0] == lightDef->light_levels[1])
        return 1;

    // Apply adaptation
    Rend_ApplyLightAdaptation(&lightlevel);

    factor =
        (lightlevel -
         lightDef->light_levels[0]) / (float) (lightDef->light_levels[1] -
                                               lightDef->light_levels[0]);
    if(factor < 0)
        return 0;
    if(factor > 1)
        return 1;
    return factor;
}

/**
 * Determine proper skip values.
 */
static void Rend_DecorationPatternSkip(ded_decorlight_t * lightDef, int *skip)
{
    unsigned int k;

    for(k = 0; k < 2; ++k)
    {
        // Skip must be at least one.
        skip[k] = lightDef->pattern_skip[k] + 1;
        if(skip[k] < 1)
            skip[k] = 1;
    }
}

/**
 * Generate decorations for the specified section of a line.
 */
static void Rend_DecorateLineSection(line_t *line, side_t *side,
                                     surface_t *surface, float top,
                                     float bottom, float texOffY)
{
    ded_decor_t *def;
    ded_decorlight_t *lightDef;
    vertex_t   *v1, *v2;
    float       lh, s, t;           // Horizontal and vertical offset.
    float       posBase[2], delta[2], pos[3], brightMul;
    float       surfTexW, surfTexH, patternW, patternH;
    int         skip[2];
    unsigned int i;

    // Is this a valid section?
    if(bottom > top || line->info->length == 0)
        return;

    // Should this be decorated at all?
    if(!(def = Rend_GetGraphicResourceDecoration(surface->texture,
                                                 surface->isflat)))
        return;

    v1 = line->v1;
    v2 = line->v2;

    // Let's see which sidedef is present.
    if(line->sidenum[1] != NO_INDEX && SIDE_PTR(line->sidenum[1]) == side)
    {
        // Flip vertices, this is the backside.
        v1 = line->v2;
        v2 = line->v1;
    }

    delta[VX] = FIX2FLT(v2->x - v1->x);
    delta[VY] = FIX2FLT(v2->y - v1->y);
    surfaceNormal[VX] = delta[VY] / line->info->length;
    surfaceNormal[VZ] = -delta[VX] / line->info->length;
    surfaceNormal[VY] = 0;

    // Height of the section.
    lh = top - bottom;

    // Setup the global texture info variables.
    if(surface->isflat)
        GL_PrepareFlat2(surface->texture, true);
    else
        GL_GetTextureInfo(surface->texture);

    surfTexW = texw;
    surfTexH = texh;

    // Generate a number of lights.
    for(i = 0; i < DED_DECOR_NUM_LIGHTS; ++i)
    {
        lightDef = def->lights + i;

        // No more?
        if(!R_IsValidLightDecoration(lightDef))
            break;

        // Does it pass the sectorlight limitation?
        if((brightMul = Rend_CheckSectorLight(side->sector, lightDef)) <= 0)
            continue;

        // Skip must be at least one.
        Rend_DecorationPatternSkip(lightDef, skip);

        posBase[VX] = FIX2FLT(v1->x) + lightDef->elevation * surfaceNormal[VX];
        posBase[VY] = FIX2FLT(v1->y) + lightDef->elevation * surfaceNormal[VZ];

        patternW = surfTexW * skip[VX];
        patternH = surfTexH * skip[VY];

        // Let's see where the top left light is.
        s = M_CycleIntoRange(lightDef->pos[VX] - surface->offx -
                             surfTexW * lightDef->pattern_offset[VX],
                             patternW);

        for(; s < line->info->length; s += patternW)
        {
            t = M_CycleIntoRange(lightDef->pos[VY] - surface->offy -
                                 surfTexH * lightDef->pattern_offset[VY] +
                                 texOffY, patternH);

            for(; t < lh; t += patternH)
            {
                // Let there be light.
                pos[VX] = posBase[VX] + delta[VX] * s / line->info->length;
                pos[VY] = posBase[VY] + delta[VY] * s / line->info->length;
                pos[VZ] = top - t;
                Rend_AddLightDecoration(pos, lightDef, brightMul, true,
                                        def->pregen_lightmap);
            }
        }
    }
}

/**
 * Returns the side that faces the sector (if any).
 */
static side_t *R_GetSectorSide(line_t *line, sector_t *sector)
{
    side_t *side = SIDE_PTR(line->sidenum[0]);

    // Swap if that wasn't the right one.
    if(side->sector != sector)
        return SIDE_PTR(line->sidenum[1]);

    return side;
}

/**
 * Return true if the line is within the visible decoration 'box'.
 */
static boolean Rend_LineDecorationBounds(line_t *line)
{
    fixed_t     bounds[6];
    sector_t   *sector;

    bounds[BLEFT]   = line->bbox[BOXLEFT];
    bounds[BRIGHT]  = line->bbox[BOXRIGHT];
    bounds[BTOP]    = line->bbox[BOXTOP];
    bounds[BBOTTOM] = line->bbox[BOXBOTTOM];

    // Figure out the highest and lowest Z height.
    sector = line->frontsector;
    bounds[BFLOOR]   = sector->planes[PLN_FLOOR]->height;
    bounds[BCEILING] = sector->planes[PLN_CEILING]->height;

    // Is the other sector higher/lower?
    if((sector = line->backsector) != NULL)
    {
        if(sector->planes[PLN_FLOOR]->height < bounds[BFLOOR])
            bounds[BFLOOR] = sector->planes[PLN_FLOOR]->height;

        if(sector->planes[PLN_CEILING]->height > bounds[BCEILING])
            bounds[BCEILING] = sector->planes[PLN_CEILING]->height;
    }

    return Rend_CheckDecorationBounds(bounds, decorWallMaxDist);
}

/**
 * Return true if the sector is within the visible decoration 'box'.
 */
static boolean Rend_SectorDecorationBounds(sector_t *sector)
{
    fixed_t     bounds[6];

    bounds[BLEFT]    = FRACUNIT * sector->info->bounds[BLEFT];
    bounds[BRIGHT]   = FRACUNIT * sector->info->bounds[BRIGHT];

    // Sectorinfo has top and bottom the other way around.
    bounds[BBOTTOM]  = FRACUNIT * sector->info->bounds[BTOP];
    bounds[BTOP]     = FRACUNIT * sector->info->bounds[BBOTTOM];
    bounds[BFLOOR]   = FRACUNIT * SECT_FLOOR(sector);
    bounds[BCEILING] = FRACUNIT * SECT_CEIL(sector);

    return Rend_CheckDecorationBounds(bounds, decorPlaneMaxDist);
}

/**
 * Generate decorations for upper, middle and bottom parts of the line,
 * on both sides.
 */
static void Rend_DecorateLine(int index)
{
    line_t     *line = LINE_PTR(index);
    side_t     *side;
    sector_t   *highSector, *lowSector;
    float       frontCeil, frontFloor, backCeil, backFloor;

    // Only the lines within the decoration visibility bounding box
    // are processed.
    if(!Rend_LineDecorationBounds(line))
        return;

    frontCeil  = SECT_CEIL(line->frontsector);
    frontFloor = SECT_FLOOR(line->frontsector);

    // Do we have a double-sided line?
    if(line->backsector)
    {
        backCeil  = SECT_CEIL(line->backsector);
        backFloor = SECT_FLOOR(line->backsector);

        // Is there a top section visible on either side?
        if(backCeil != frontCeil &&
           (!R_IsSkySurface(&line->backsector->SP_ceilsurface) ||
            !R_IsSkySurface(&line->frontsector->SP_ceilsurface)))
        {
            if(frontCeil > backCeil)
            {
                highSector = line->frontsector;
                lowSector  = line->backsector;
            }
            else
            {
                lowSector  = line->frontsector;
                highSector = line->backsector;
            }

            // Figure out the right side.
            side = R_GetSectorSide(line, highSector);

            if(side->top.texture > 0)
            {
                if(side->top.isflat)
                    GL_PrepareFlat2(side->top.texture, true);
                else
                    GL_GetTextureInfo(side->top.texture);

                Rend_DecorateLineSection(line, side, &side->top,
                                         SECT_CEIL(highSector),
                                         SECT_CEIL(lowSector),
                                         line->flags & ML_DONTPEGTOP ? 0 : -texh +
                                         (SECT_CEIL(highSector) -
                                          SECT_CEIL(lowSector)));
            }
        }

        // Is there a bottom section visible?
        if(backFloor != frontFloor &&
           (!R_IsSkySurface(&line->backsector->SP_floorsurface) ||
            !R_IsSkySurface(&line->frontsector->SP_floorsurface)))
        {
            if(frontFloor > backFloor)
            {
                highSector = line->frontsector;
                lowSector  = line->backsector;
            }
            else
            {
                lowSector  = line->frontsector;
                highSector = line->backsector;
            }

            // Figure out the right side.
            side = R_GetSectorSide(line, lowSector);

            if(side->bottom.texture > 0)
            {
                if(side->bottom.isflat)
                    GL_PrepareFlat2(side->bottom.texture, true);
                else
                    GL_GetTextureInfo(side->bottom.texture);

                Rend_DecorateLineSection(line, side, &side->bottom,
                                         SECT_FLOOR(highSector),
                                         SECT_FLOOR(lowSector),
                                         line->flags & ML_DONTPEGBOTTOM ?
                                         SECT_FLOOR(highSector) -
                                         SECT_CEIL(lowSector) : 0);
            }
        }

        // 2-sided middle texture?
        // FIXME: Since halos aren't usually clipped by 2-sided middle
        // textures, this looks a bit silly.
        /*if(line->sidenum[0] >= 0 && (side = SIDE_PTR(line->sidenum[0]))->midtexture)
        {
            rendpoly_t *quad = R_AllocRendPoly(RP_QUAD, true, 4);

            // If there is an opening, process it.
            if(side->middle.texture.isflat)
                GL_PrepareFlat2(side->middle.texture, true);
            else
                GL_GetTextureInfo(side->middle.texture);

            quad->top = MIN_OF(frontCeil, backCeil);
            quad->bottom = MAX_OF(frontFloor, backFloor);
            quad->texoffy = FIX2FLT(side->textureoffset);
            if(Rend_MidTexturePos(&quad->top, &quad->bottom, &quad->texoffy, 0,
                                  (line->flags & ML_DONTPEGBOTTOM) != 0))
            {
                Rend_DecorateLineSection(line, side, &side->middle,
                                         quad->top, quad->bottom, quad->texoffy);
            }
            R_FreeRendPoly(quad);
        }*/
    }
    else
    {
        // This is a single-sided line. We only need to worry about the
        // middle texture.
        side =
            SIDE_PTR(line->sidenum[0] != NO_INDEX ? line->sidenum[0] : line->sidenum[1]);

        if(side->middle.texture > 0)
        {
            if(side->middle.isflat)
                GL_PrepareFlat2(side->middle.texture, true);
            else
                GL_GetTextureInfo(side->middle.texture);

            Rend_DecorateLineSection(line, side, &side->middle, frontCeil,
                                     frontFloor,
                                     line->flags & ML_DONTPEGBOTTOM ? -texh +
                                     (frontCeil - frontFloor) : 0);
        }
    }
}

/**
 * Generate decorations for a plane.
 */
static void Rend_DecoratePlane(int sectorIndex, float z, float elevateDir,
                               float offX, float offY, ded_decor_t *def)
{
    sector_t   *sector = SECTOR_PTR(sectorIndex);
    sectorinfo_t *sin = sector->info;
    ded_decorlight_t *lightDef;
    float       pos[3], tileSize = 64, brightMul;
    int         skip[2];
    unsigned int i;

    surfaceNormal[VX] = 0;
    surfaceNormal[VY] = elevateDir;
    surfaceNormal[VZ] = 0;

    // Generate a number of lights.
    for(i = 0; i < DED_DECOR_NUM_LIGHTS; ++i)
    {
        lightDef = def->lights + i;

        // No more?
        if(!R_IsValidLightDecoration(lightDef))
            break;

        // Does it pass the sectorlight limitation?
        if((brightMul = Rend_CheckSectorLight(sector, lightDef)) <= 0)
            continue;

        // Skip must be at least one.
        Rend_DecorationPatternSkip(lightDef, skip);

        pos[VY] =
            (int) (sin->bounds[BTOP] / tileSize) * tileSize - offY -
            lightDef->pos[VY] - lightDef->pattern_offset[VY] * tileSize;
        while(pos[VY] > sin->bounds[BTOP])
            pos[VY] -= tileSize * skip[VY];

        for(; pos[VY] < sin->bounds[BBOTTOM]; pos[VY] += tileSize * skip[VY])
        {
            if(pos[VY] < sin->bounds[BTOP])
                continue;

            pos[VX] =
                (int) (sin->bounds[BLEFT] / tileSize) * tileSize - offX +
                lightDef->pos[VX] - lightDef->pattern_offset[VX] * tileSize;
            while(pos[VX] > sin->bounds[BLEFT])
                pos[VX] -= tileSize * skip[VX];

            for(; pos[VX] < sin->bounds[BRIGHT];
                pos[VX] += tileSize * skip[VX])
            {
                if(pos[VX] < sin->bounds[BLEFT])
                    continue;

                // The point must be inside the correct sector.
                if(!R_IsPointInSector
                   (pos[VX] * FRACUNIT, pos[VY] * FRACUNIT, sector))
                    continue;

                pos[VZ] = z + lightDef->elevation * elevateDir;
                Rend_AddLightDecoration(pos, lightDef, brightMul, false,
                                        def->pregen_lightmap);
            }
        }
    }
}

/**
 * Generate decorations for the planes of the sector.
 */
static void Rend_DecorateSector(int index)
{
    int         i;
    plane_t    *pln;
    sector_t   *sector = SECTOR_PTR(index);
    ded_decor_t *def;

    // The sector must have height if it wants decorations.
    if(sector->planes[PLN_CEILING]->height <= sector->planes[PLN_FLOOR]->height)
        return;

    // Is this sector close enough for the decorations to be visible?
    if(!Rend_SectorDecorationBounds(sector))
        return;

    for(i = 0; i < sector->planecount; ++i)
    {
        pln = sector->planes[i];
        def = Rend_GetGraphicResourceDecoration(pln->surface.texture,
                                                pln->surface.isflat);

        if(def != NULL) // The surface is decorated.
            Rend_DecoratePlane(index, SECT_PLANE_HEIGHT(sector, i),
                               pln->surface.normal[VZ],
                               pln->surface.offx, pln->surface.offy, def);
    }
}

/**
 * Decorations are generated for each frame.
 */
void Rend_InitDecorationsForFrame(void)
{
    int     i;

    Rend_ClearDecorations();

    // This only needs to be done if decorations have been enabled.
    if(!useDecorations)
        return;

    // Process all lines. This could also be done during sectors,
    // but validcount would need to be used to prevent duplicate
    // processing.
    for(i = 0; i < numlines; ++i)
        Rend_DecorateLine(i);

    // Process all planes.
    for(i = 0; i < numsectors; ++i)
        Rend_DecorateSector(i);
}
