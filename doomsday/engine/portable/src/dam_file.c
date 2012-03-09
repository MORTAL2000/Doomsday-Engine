/**\file dam_file.c
 *\section License
 * License: GPL
 * Online License Link: http://www.gnu.org/licenses/gpl.html
 *
 *\author Copyright © 2007-2012 Daniel Swanson <danij@dengine.net>
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
 * Doomsday Archived Map (DAM) reader/writer.
 */

#include <lzss.h>
#include <stdlib.h>

#include "de_base.h"
#include "de_console.h"
#include "de_dam.h"
#include "de_defs.h"
#include "de_misc.h"
#include "de_refresh.h"
#include "de_filesys.h"

#include "p_mapdata.h"

// Global archived map format version identifier. Increment when making
// changes to the structure of the format.
#define DAM_VERSION             1

// Segments of a doomsday archived map file.
typedef enum damsegment_e {
    DAMSEG_END = -1,                // Terminates a segment.
    DAMSEG_HEADER = 100,            // File-level meta.
    DAMSEG_RELOCATIONTABLES,        // Tables of offsets to file positions.
    DAMSEG_SYMBOLTABLES,            // Global symbol tables.

    DAMSEG_MAP = 200,               // Start of the map data.
    DAMSEG_POLYOBJS,
    DAMSEG_VERTEXES,
    DAMSEG_LINES,
    DAMSEG_SIDES,
    DAMSEG_SECTORS,
    DAMSEG_SUBSECTORS,
    DAMSEG_HEDGES,
    DAMSEG_NODES,
    DAMSEG_BLOCKMAP,
    DAMSEG_REJECT
} damsegment_t;

static LZFILE* mapFile;
static int mapFileVersion;

static boolean openMapFile(const char* path, boolean write)
{
    mapFile = NULL;
    mapFileVersion = 0;
    mapFile = lzOpen((char*)path, (write? F_WRITE_PACKED : F_READ_PACKED));

    return ((mapFile)? true : false);
}

static boolean closeMapFile(void)
{
    return (lzClose(mapFile)? true : false);
}

static void writeNBytes(void* data, long len)
{
    lzWrite(data, len, mapFile);
}

static void writeByte(byte val)
{
    lzPutC(val, mapFile);
}

static void writeShort(short val)
{
    lzPutW(val, mapFile);
}

static void writeLong(long val)
{
    lzPutL(val, mapFile);
}

static void writeFloat(float val)
{
    long temp = 0;
    memcpy(&temp, &val, 4);
    lzPutL(temp, mapFile);
}

static void readNBytes(void *ptr, long len)
{
    lzRead(ptr, len, mapFile);
}

static byte readByte(void)
{
    return lzGetC(mapFile);
}

static short readShort(void)
{
    return lzGetW(mapFile);
}

static long readLong(void)
{
    return lzGetL(mapFile);
}

static float readFloat(void)
{
    long    val = lzGetL(mapFile);
    float   returnValue = 0;

    memcpy(&returnValue, &val, 4);
    return returnValue;
}

/**
 * Exit with a fatal error if the value at the current location in the
 * map file does not match that associated with the specified segment.
 *
 * @param segType       Value by which to check for alignment.
 */
static void assertSegment(damsegment_t segment)
{
    if(readLong() != segment)
    {
        Con_Error("assertSegment: Segment [%d] failed alignment check",
                  (int) segment);
    }
}

static void beginSegment(damsegment_t segment)
{
    writeLong(segment);
}

static void endSegment(void)
{
    writeLong(DAMSEG_END);
}

static void writeVertex(const GameMap *map, uint idx)
{
    vertex_t           *v = &map->vertexes[idx];

    writeFloat(v->V_pos[VX]);
    writeFloat(v->V_pos[VY]);
    writeLong((long) v->numLineOwners);

    if(v->numLineOwners > 0)
    {
        lineowner_t        *own, *base;

        own = base = (v->lineOwners)->LO_prev;
        do
        {
            writeLong((long) ((own->lineDef - map->lineDefs) + 1));
            writeLong((long) own->angle);
            own = own->LO_prev;
        } while(own != base);
    }
}

static void readVertex(const GameMap *map, uint idx)
{
    uint                i;
    vertex_t           *v = &map->vertexes[idx];

    v->V_pos[VX] = readFloat();
    v->V_pos[VY] = readFloat();
    v->numLineOwners = (uint) readLong();

    if(v->numLineOwners > 0)
    {
        lineowner_t        *own;

        v->lineOwners = NULL;
        for(i = 0; i < v->numLineOwners; ++i)
        {
            own = Z_Malloc(sizeof(lineowner_t), PU_MAP, 0);
            own->lineDef = &map->lineDefs[(unsigned) (readLong() - 1)];
            own->angle = (binangle_t) readLong();

            own->LO_next = v->lineOwners;
            v->lineOwners = own;
        }

        own = v->lineOwners;
        do
        {
            own->LO_next->LO_prev = own;
            own = own->LO_next;
        } while(own);
        own->LO_prev = v->lineOwners;
    }
}

static void archiveVertexes(GameMap *map, boolean write)
{
    uint                i;

    if(write)
        beginSegment(DAMSEG_VERTEXES);
    else
        assertSegment(DAMSEG_VERTEXES);

    if(write)
    {
        writeLong((long) map->numVertexes);
        for(i = 0; i < map->numVertexes; ++i)
            writeVertex(map, i);
    }
    else
    {
        map->numVertexes = (uint) readLong();
        for(i = 0; i < map->numVertexes; ++i)
            readVertex(map, i);
    }

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void writeLine(const GameMap *map, uint idx)
{
    int                 i;
    linedef_t             *l = &map->lineDefs[idx];

    writeLong((long) ((l->v[0] - map->vertexes) + 1));
    writeLong((long) ((l->v[1] - map->vertexes) + 1));
    writeLong(l->flags);
    writeByte(l->inFlags);
    writeFloat(l->dX);
    writeFloat(l->dY);
    writeLong((long) l->slopeType);
    writeLong((long) (l->sideDefs[0]? ((l->sideDefs[0] - map->sideDefs) + 1) : 0));
    writeLong((long) (l->sideDefs[1]? ((l->sideDefs[1] - map->sideDefs) + 1) : 0));
    writeFloat(l->aaBox.minX);
    writeFloat(l->aaBox.minY);
    writeFloat(l->aaBox.maxX);
    writeFloat(l->aaBox.maxY);
    writeFloat(l->length);
    writeLong((long) l->angle);
    for(i = 0; i < DDMAXPLAYERS; ++i)
        writeByte(l->mapped[i]? 1 : 0);
}

static void readLine(const GameMap *map, uint idx)
{
    int                 i;
    long                sideIdx;
    linedef_t             *l = &map->lineDefs[idx];

    l->v[0] = &map->vertexes[(unsigned) (readLong() - 1)];
    l->v[1] = &map->vertexes[(unsigned) (readLong() - 1)];
    l->flags = (int) readLong();
    l->inFlags = readByte();
    l->dX = readFloat();
    l->dY = readFloat();
    l->slopeType = (slopetype_t) readLong();
    sideIdx = readLong();
    l->sideDefs[0] = (sideIdx == 0? NULL : &map->sideDefs[sideIdx-1]);
    sideIdx = readLong();
    l->sideDefs[1] = (sideIdx == 0? NULL : &map->sideDefs[sideIdx-1]);
    l->aaBox.minX = readFloat();
    l->aaBox.minY = readFloat();
    l->aaBox.maxX = readFloat();
    l->aaBox.maxY = readFloat();
    l->length = readFloat();
    l->angle = (binangle_t) readLong();
    for(i = 0; i < DDMAXPLAYERS; ++i)
        l->mapped[i] = (readByte()? true : false);
}

static void archiveLines(GameMap *map, boolean write)
{
    uint                i;

    if(write)
        beginSegment(DAMSEG_LINES);
    else
        assertSegment(DAMSEG_LINES);

    if(write)
    {
        writeLong(map->numLineDefs);
        for(i = 0; i < map->numLineDefs; ++i)
            writeLine(map, i);
    }
    else
    {
        map->numLineDefs = readLong();
        for(i = 0; i < map->numLineDefs; ++i)
            readLine(map, i);
    }

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void writeSide(const GameMap *map, uint idx)
{
    uint                i;
    sidedef_t             *s = &map->sideDefs[idx];

    for(i = 0; i < 3; ++i)
    {
        surface_t          *suf = &s->sections[3];

        writeLong(suf->flags);
        //writeLong(getMaterialDictID(materialDict, suf->material));
        writeLong((long) suf->blendMode);
        writeFloat(suf->normal[VX]);
        writeFloat(suf->normal[VY]);
        writeFloat(suf->normal[VZ]);
        writeFloat(suf->offset[VX]);
        writeFloat(suf->offset[VY]);
        writeFloat(suf->rgba[CR]);
        writeFloat(suf->rgba[CG]);
        writeFloat(suf->rgba[CB]);
        writeFloat(suf->rgba[CA]);
    }
    writeLong(s->sector? ((s->sector - map->sectors) + 1) : 0);
    writeShort(s->flags);
    writeLong((long) s->hedgeCount);
    for(i = 0; i < s->hedgeCount; ++i)
        writeLong((s->hedges[i] - map->hedges) + 1);
}

static void readSide(const GameMap *map, uint idx)
{
    uint                i;
    long                secIdx;
    float               offset[2], rgba[4];
    sidedef_t          *s = &map->sideDefs[idx];

    for(i = 0; i < 3; ++i)
    {
        surface_t          *suf = &s->sections[3];

        suf->flags = (int) readLong();
        //Surface_SetMaterial(suf, lookupMaterialFromDict(materialDict, readLong()));
        Surface_SetBlendMode(suf, (blendmode_t) readLong());
        suf->normal[VX] = readFloat();
        suf->normal[VY] = readFloat();
        suf->normal[VZ] = readFloat();
        offset[VX] = readFloat();
        offset[VY] = readFloat();
        Surface_SetMaterialOrigin(suf, offset[VX], offset[VY]);
        rgba[CR] = readFloat();
        rgba[CG] = readFloat();
        rgba[CB] = readFloat();
        rgba[CA] = readFloat();
        Surface_SetColorAndAlpha(suf, rgba[CR], rgba[CG], rgba[CB], rgba[CA]);
        suf->decorations = NULL;
        suf->numDecorations = 0;
    }
    secIdx = readLong();
    s->sector = (secIdx == 0? NULL : &map->sectors[secIdx -1]);
    s->flags = readShort();
    s->hedgeCount = (uint) readLong();
    s->hedges = Z_Malloc(sizeof(HEdge*) * (s->hedgeCount + 1), PU_MAP, 0);
    for(i = 0; i < s->hedgeCount; ++i)
        s->hedges[i] = &map->hedges[(unsigned) readLong() - 1];
    s->hedges[i] = NULL; // Terminate.
}

static void archiveSides(GameMap *map, boolean write)
{
    uint                i;

    if(write)
        beginSegment(DAMSEG_SIDES);
    else
        assertSegment(DAMSEG_SIDES);

    if(write)
    {
        writeLong(map->numSideDefs);
        for(i = 0; i < map->numSideDefs; ++i)
            writeSide(map, i);
    }
    else
    {
        map->numSideDefs = readLong();
        for(i = 0; i < map->numSideDefs; ++i)
            readSide(map, i);
    }

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void writeSector(const GameMap *map, uint idx)
{
    uint                i;
    sector_t           *s = &map->sectors[idx];

    writeFloat(s->lightLevel);
    writeFloat(s->rgb[CR]);
    writeFloat(s->rgb[CG]);
    writeFloat(s->rgb[CB]);
    writeLong(s->planeCount);
    for(i = 0; i < s->planeCount; ++i)
    {
        plane_t            *p = s->planes[i];

        writeFloat(p->height);
        writeFloat(p->target);
        writeFloat(p->speed);
        writeFloat(p->visHeight);
        writeFloat(p->visHeightDelta);

        writeLong((long) p->surface.flags);
        //writeLong(getMaterialDictID(materialDict, p->surface.material));
        writeLong((long) p->surface.blendMode);
        writeFloat(p->surface.normal[VX]);
        writeFloat(p->surface.normal[VY]);
        writeFloat(p->surface.normal[VZ]);
        writeFloat(p->surface.offset[VX]);
        writeFloat(p->surface.offset[VY]);
        writeFloat(p->surface.rgba[CR]);
        writeFloat(p->surface.rgba[CG]);
        writeFloat(p->surface.rgba[CB]);
        writeFloat(p->surface.rgba[CA]);

        writeFloat(p->soundOrg.pos[VX]);
        writeFloat(p->soundOrg.pos[VY]);
        writeFloat(p->soundOrg.pos[VZ]);
    }

    writeLong(s->flags);
    writeFloat(s->bBox[BOXLEFT]);
    writeFloat(s->bBox[BOXRIGHT]);
    writeFloat(s->bBox[BOXBOTTOM]);
    writeFloat(s->bBox[BOXTOP]);
    writeFloat(s->soundOrg.pos[VX]);
    writeFloat(s->soundOrg.pos[VY]);
    writeFloat(s->soundOrg.pos[VZ]);

    for(i = 0; i < NUM_REVERB_DATA; ++i)
        writeFloat(s->reverb[i]);

    // Lightgrid block indices.
    writeLong((long) s->changedBlockCount);
    writeLong((long) s->blockCount);
    for(i = 0; i < s->blockCount; ++i)
        writeShort(s->blocks[i]);

    // Line list.
    writeLong((long) s->lineDefCount);
    for(i = 0; i < s->lineDefCount; ++i)
        writeLong((s->lineDefs[i] - map->lineDefs) + 1);

    // Subsector list.
    writeLong((long) s->subsectorCount);
    for(i = 0; i < s->subsectorCount; ++i)
        writeLong((s->subsectors[i] - map->subsectors) + 1);

    // Reverb subsector attributors.
    writeLong((long) s->numReverbSSecAttributors);
    for(i = 0; i < s->numReverbSSecAttributors; ++i)
        writeLong((s->reverbSubsectors[i] - map->subsectors) + 1);
}

static void readSector(const GameMap *map, uint idx)
{
    uint                i, numPlanes;
    long                secIdx;
    float               offset[2], rgba[4];
    sector_t           *s = &map->sectors[idx];

    s->lightLevel = readFloat();
    s->rgb[CR] = readFloat();
    s->rgb[CG] = readFloat();
    s->rgb[CB] = readFloat();
    numPlanes = (uint) readLong();
    for(i = 0; i < numPlanes; ++i)
    {
        plane_t            *p = R_NewPlaneForSector(s);

        p->height = readFloat();
        p->target = readFloat();
        p->speed = readFloat();
        p->visHeight = readFloat();
        p->visHeightDelta = readFloat();

        p->surface.flags = (int) readLong();
        //Surface_SetMaterial(&p->surface, lookupMaterialFromDict(materialDict, readLong()));
        Surface_SetBlendMode(&p->surface, (blendmode_t) readLong());
        p->surface.normal[VX] = readFloat();
        p->surface.normal[VY] = readFloat();
        p->surface.normal[VZ] = readFloat();
        offset[VX] = readFloat();
        offset[VY] = readFloat();
        Surface_SetMaterialOrigin(&p->surface, offset[VX], offset[VY]);
        rgba[CR] = readFloat();
        rgba[CG] = readFloat();
        rgba[CB] = readFloat();
        rgba[CA] = readFloat();
        Surface_SetColorAndAlpha(&p->surface, rgba[CR], rgba[CG], rgba[CB], rgba[CA]);

        p->soundOrg.pos[VX] = readFloat();
        p->soundOrg.pos[VY] = readFloat();
        p->soundOrg.pos[VZ] = readFloat();

        p->surface.decorations = NULL;
        p->surface.numDecorations = 0;
    }

    secIdx = readLong();
    s->flags = readLong();
    s->bBox[BOXLEFT] = readFloat();
    s->bBox[BOXRIGHT] = readFloat();
    s->bBox[BOXBOTTOM] = readFloat();
    s->bBox[BOXTOP] = readFloat();
    secIdx = readLong();
    s->soundOrg.pos[VX] = readFloat();
    s->soundOrg.pos[VY] = readFloat();
    s->soundOrg.pos[VZ] = readFloat();

    for(i = 0; i < NUM_REVERB_DATA; ++i)
        s->reverb[i] = readFloat();

    // Lightgrid block indices.
    s->changedBlockCount = (uint) readLong();
    s->blockCount = (uint) readLong();
    s->blocks = Z_Malloc(sizeof(short) * s->blockCount, PU_MAP, 0);
    for(i = 0; i < s->blockCount; ++i)
        s->blocks[i] = readShort();

    // Line list.
    s->lineDefCount = (uint) readLong();
    s->lineDefs = Z_Malloc(sizeof(linedef_t*) * (s->lineDefCount + 1), PU_MAP, 0);
    for(i = 0; i < s->lineDefCount; ++i)
        s->lineDefs[i] = &map->lineDefs[(unsigned) readLong() - 1];
    s->lineDefs[i] = NULL; // Terminate.

    // Subsector list.
    s->subsectorCount = (uint) readLong();
    s->subsectors =
        Z_Malloc(sizeof(subsector_t*) * (s->subsectorCount + 1), PU_MAP, 0);
    for(i = 0; i < s->subsectorCount; ++i)
        s->subsectors[i] = &map->subsectors[(unsigned) readLong() - 1];
    s->subsectors[i] = NULL; // Terminate.

    // Reverb subsector attributors.
    s->numReverbSSecAttributors = (uint) readLong();
    s->reverbSubsectors =
        Z_Malloc(sizeof(subsector_t*) * (s->numReverbSSecAttributors + 1), PU_MAP, 0);
    for(i = 0; i < s->numReverbSSecAttributors; ++i)
        s->reverbSubsectors[i] = &map->subsectors[(unsigned) readLong() - 1];
    s->reverbSubsectors[i] = NULL; // Terminate.
}

static void archiveSectors(GameMap *map, boolean write)
{
    uint                i;

    if(write)
        beginSegment(DAMSEG_SECTORS);
    else
        assertSegment(DAMSEG_SECTORS);

    if(write)
    {
        writeLong(map->numSectors);
        for(i = 0; i < map->numSectors; ++i)
            writeSector(map, i);
    }
    else
    {
        map->numSectors = readLong();
        for(i = 0; i < map->numSectors; ++i)
            readSector(map, i);
    }

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void writeSubsector(const GameMap *map, uint idx)
{
    uint                i;
    subsector_t        *s = &map->subsectors[idx];

    writeLong((long) s->flags);
    writeFloat(s->aaBox.minX);
    writeFloat(s->aaBox.minY);
    writeFloat(s->aaBox.maxX);
    writeFloat(s->aaBox.maxY);
    writeFloat(s->midPoint.pos[VX]);
    writeFloat(s->midPoint.pos[VY]);
    writeLong(s->sector? ((s->sector - map->sectors) + 1) : 0);
    writeLong(s->polyObj? (s->polyObj->idx + 1) : 0);

    // Subsector reverb.
    for(i = 0; i < NUM_REVERB_DATA; ++i)
        writeLong((long) s->reverb[i]);

    // Subsector hedges list.
    writeLong((long) s->hedgeCount);
    for(i = 0; i < s->hedgeCount; ++i)
        writeLong((s->hedges[i] - map->hedges) + 1);
}

static void readSubsector(const GameMap *map, uint idx)
{
    uint                i;
    long                obIdx;
    subsector_t        *s = &map->subsectors[idx];

    s->flags = (int) readLong();
    s->aaBox.minX = readFloat();
    s->aaBox.minY = readFloat();
    s->aaBox.maxX = readFloat();
    s->aaBox.maxY = readFloat();
    s->midPoint.pos[VX] = readFloat();
    s->midPoint.pos[VY] = readFloat();
    obIdx = readLong();
    s->sector = (obIdx == 0? NULL : &map->sectors[(unsigned) obIdx - 1]);
    obIdx = readLong();
    s->polyObj = (obIdx == 0? NULL : map->polyObjs[(unsigned) obIdx - 1]);

    // Subsector reverb.
    for(i = 0; i < NUM_REVERB_DATA; ++i)
        s->reverb[i] = (uint) readLong();

    // Subsector hedges list.
    s->hedgeCount = (uint) readLong();
    s->hedges = Z_Malloc(sizeof(HEdge*) * (s->hedgeCount + 1), PU_MAP, 0);
    for(i = 0; i < s->hedgeCount; ++i)
        s->hedges[i] = &map->hedges[(unsigned) readLong() - 1];
    s->hedges[i] = NULL; // Terminate.
}

static void archiveSubsectors(GameMap *map, boolean write)
{
    uint                i;

    if(write)
        beginSegment(DAMSEG_SUBSECTORS);
    else
        assertSegment(DAMSEG_SUBSECTORS);

    if(write)
    {
        writeLong(map->numSubsectors);
        for(i = 0; i < map->numSubsectors; ++i)
            writeSubsector(map, i);
    }
    else
    {
        map->numSubsectors = readLong();
        for(i = 0; i < map->numSubsectors; ++i)
            readSubsector(map, i);
    }

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void writeSeg(const GameMap *map, uint idx)
{
    HEdge              *s = &map->hedges[idx];

    writeLong((s->v[0] - map->vertexes) + 1);
    writeLong((s->v[1] - map->vertexes) + 1);
    writeFloat(s->length);
    writeFloat(s->offset);
    writeLong(s->lineDef? ((s->lineDef - map->lineDefs) + 1) : 0);
    writeLong(s->sec[FRONT]? ((s->sec[FRONT] - map->sectors) + 1) : 0);
    writeLong(s->sec[BACK]? ((s->sec[BACK] - map->sectors) + 1) : 0);
    writeLong(s->subsector? ((s->subsector - map->subsectors) + 1) : 0);
    writeLong(s->twin? ((s->twin - map->hedges) + 1) : 0);
    writeLong((long) s->angle);
    writeByte(s->side);
    writeByte(s->flags);
}

static void readSeg(const GameMap *map, uint idx)
{
    long                obIdx;
    HEdge              *s = &map->hedges[idx];

    s->v[0] = &map->vertexes[(unsigned) readLong() - 1];
    s->v[1] = &map->vertexes[(unsigned) readLong() - 1];
    s->length = readFloat();
    s->offset = readFloat();
    obIdx = readLong();
    s->lineDef = (obIdx == 0? NULL : &map->lineDefs[(unsigned) obIdx - 1]);
    obIdx = readLong();
    s->sec[FRONT] = (obIdx == 0? NULL : &map->sectors[(unsigned) obIdx - 1]);
    obIdx = readLong();
    s->sec[BACK] = (obIdx == 0? NULL : &map->sectors[(unsigned) obIdx - 1]);
    obIdx = readLong();
    s->subsector = (obIdx == 0? NULL : &map->subsectors[(unsigned) obIdx - 1]);
    obIdx = readLong();
    s->twin = (obIdx == 0? NULL : &map->hedges[(unsigned) obIdx - 1]);
    s->angle = (angle_t) readLong();
    s->side = readByte();
    s->flags = readByte();
}

static void archiveSegs(GameMap *map, boolean write)
{
    uint                i;

    if(write)
        beginSegment(DAMSEG_HEDGES);
    else
        assertSegment(DAMSEG_HEDGES);

    if(write)
    {
        writeLong(map->numHEdges);
        for(i = 0; i < map->numHEdges; ++i)
            writeSeg(map, i);
    }
    else
    {
        map->numHEdges = readLong();
        for(i = 0; i < map->numHEdges; ++i)
            readSeg(map, i);
    }

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void writeNode(const GameMap *map, uint idx)
{
    BspNode             *n = &map->bspNodes[idx];

    writeFloat(n->partition.x);
    writeFloat(n->partition.y);
    writeFloat(n->partition.dX);
    writeFloat(n->partition.dY);
    writeFloat(n->bBox[RIGHT][BOXLEFT]);
    writeFloat(n->bBox[RIGHT][BOXRIGHT]);
    writeFloat(n->bBox[RIGHT][BOXBOTTOM]);
    writeFloat(n->bBox[RIGHT][BOXTOP]);
    writeFloat(n->bBox[LEFT][BOXLEFT]);
    writeFloat(n->bBox[LEFT][BOXRIGHT]);
    writeFloat(n->bBox[LEFT][BOXBOTTOM]);
    writeFloat(n->bBox[LEFT][BOXTOP]);
    writeLong((long) n->children[RIGHT]);
    writeLong((long) n->children[LEFT]);
}

static void readNode(const GameMap *map, uint idx)
{
    BspNode             *n = &map->bspNodes[idx];

    n->partition.x = readFloat();
    n->partition.y = readFloat();
    n->partition.dX = readFloat();
    n->partition.dY = readFloat();
    n->bBox[RIGHT][BOXLEFT] = readFloat();
    n->bBox[RIGHT][BOXRIGHT] = readFloat();
    n->bBox[RIGHT][BOXBOTTOM] = readFloat();
    n->bBox[RIGHT][BOXTOP] = readFloat();
    n->bBox[LEFT][BOXLEFT] = readFloat();
    n->bBox[LEFT][BOXRIGHT] = readFloat();
    n->bBox[LEFT][BOXBOTTOM] = readFloat();
    n->bBox[LEFT][BOXTOP] = readFloat();
    n->children[RIGHT] = (uint) readLong();
    n->children[LEFT] = (uint) readLong();
}

static void archiveNodes(GameMap *map, boolean write)
{
    uint                i;

    if(write)
        beginSegment(DAMSEG_NODES);
    else
        assertSegment(DAMSEG_NODES);

    if(write)
    {
        writeLong(map->numBspNodes);
        for(i = 0; i < map->numBspNodes; ++i)
            writeNode(map, i);
    }
    else
    {
        map->numBspNodes = readLong();
        for(i = 0; i < map->numBspNodes; ++i)
            readNode(map, i);
    }

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void archiveBlockmap(GameMap *map, boolean write)
{
    if(write)
        beginSegment(DAMSEG_BLOCKMAP);
    else
        assertSegment(DAMSEG_BLOCKMAP);

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void archiveReject(GameMap *map, boolean write)
{
    if(write)
        beginSegment(DAMSEG_REJECT);
    else
        assertSegment(DAMSEG_REJECT);

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void writePolyobj(const GameMap* map, uint idx)
{
    polyobj_t* p = map->polyObjs[idx];
    uint i;

    writeLong((long) p->idx);
    writeFloat(p->pos[VX]);
    writeFloat(p->pos[VY]);
    writeFloat(p->pos[VZ]);
    writeLong((long) p->angle);
    writeLong((long) p->tag);
    writeFloat(p->aaBox.minX);
    writeFloat(p->aaBox.minY);
    writeFloat(p->aaBox.maxX);
    writeFloat(p->aaBox.maxY);
    writeFloat(p->dest[VX]);
    writeFloat(p->dest[VY]);
    writeFloat(p->speed);
    writeLong((long) p->destAngle);
    writeLong((long) p->angleSpeed);
    writeByte(p->crush? 1 : 0);
    writeLong((long) p->seqType);

    writeLong((long) p->lineCount);
    for(i = 0; i < p->lineCount; ++i)
    {
        linedef_t* line = p->lines[i];
        HEdge* he = line->L_frontside->hedges[0];

        writeLong((he->v[0] - map->vertexes) + 1);
        writeLong((he->v[1] - map->vertexes) + 1);
        writeFloat(he->length);
        writeFloat(he->offset);
        writeLong(he->lineDef? ((he->lineDef - map->lineDefs) + 1) : 0);
        writeLong(he->sec[FRONT]? ((he->sec[FRONT] - map->sectors) + 1) : 0);
        writeLong((long) he->angle);
        writeByte(he->side);
        writeByte(he->flags);
    }
}

static void readPolyobj(const GameMap* map, uint idx)
{
    polyobj_t* p = map->polyObjs[idx];
    long obIdx;
    HEdge* hedges;
    uint i;

    p->idx = (uint) readLong();
    p->pos[VX] = readFloat();
    p->pos[VY] = readFloat();
    p->pos[VZ] = readFloat();
    p->angle = (angle_t) readLong();
    p->tag = (int) readLong();
    p->aaBox.minX = readFloat();
    p->aaBox.minY = readFloat();
    p->aaBox.maxX = readFloat();
    p->aaBox.maxY = readFloat();
    p->dest[VX] = readFloat();
    p->dest[VY] = readFloat();
    p->speed = readFloat();
    p->destAngle = (angle_t) readLong();
    p->angleSpeed = (angle_t) readLong();
    p->crush = (readByte()? true : false);
    p->seqType = (int) readLong();

    // Polyobj line list.
    p->lineCount = (uint) readLong();

    hedges = Z_Calloc(sizeof(HEdge) * p->lineCount, PU_MAP, 0);
    p->lines = Z_Malloc(sizeof(HEdge*) * (p->lineCount + 1), PU_MAP, 0);
    for(i = 0; i < p->lineCount; ++i)
    {
        HEdge* he = hedges + i;
        linedef_t* line;

        he->v[0] = &map->vertexes[(unsigned) readLong() - 1];
        he->v[1] = &map->vertexes[(unsigned) readLong() - 1];
        he->length = readFloat();
        he->offset = readFloat();
        obIdx = readLong();
        he->lineDef = (obIdx == 0? NULL : &map->lineDefs[(unsigned) obIdx - 1]);
        obIdx = readLong();
        he->sec[FRONT] = (obIdx == 0? NULL : &map->sectors[(unsigned) obIdx - 1]);
        he->angle = (angle_t) readLong();
        he->side = (readByte()? 1 : 0);
        he->flags = readByte();

        line = he->lineDef;
        line->L_frontside->hedges = Z_Malloc(sizeof(*line->L_frontside->hedges), PU_MAP, 0);
        line->L_frontside->hedges[0] = he;

        p->lines[i] = line;
    }
    p->lines[i] = NULL; // Terminate.
}

static void archivePolyobjs(GameMap* map, boolean write)
{
    uint i;

    if(write)
        beginSegment(DAMSEG_POLYOBJS);
    else
        assertSegment(DAMSEG_POLYOBJS);

    if(write)
    {
        writeLong(map->numPolyObjs);
        for(i = 0; i < map->numPolyObjs; ++i)
            writePolyobj(map, i);
    }
    else
    {
        map->numPolyObjs = readLong();
        for(i = 0; i < map->numPolyObjs; ++i)
            readPolyobj(map, i);
    }

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

/*
static void writeThing(const GameMap *map, uint idx)
{

}

static void readThing(const GameMap *map, uint idx)
{

}
*/

static void archiveMap(GameMap *map, boolean write)
{
    if(write)
        beginSegment(DAMSEG_MAP);
    else
    {
        assertSegment(DAMSEG_MAP);

        // Call the game's setup routines.
        if(gx.SetupForMapData)
        {
            gx.SetupForMapData(DMU_VERTEX, map->numVertexes);
            gx.SetupForMapData(DMU_LINEDEF, map->numLineDefs);
            gx.SetupForMapData(DMU_SIDEDEF, map->numSideDefs);
            gx.SetupForMapData(DMU_SECTOR, map->numSectors);
        }
    }

    archivePolyobjs(map, write);
    archiveVertexes(map, write);
    archiveLines(map, write); // Must follow vertexes (lineowner nodes).
    archiveSides(map, write);
    archiveSectors(map, write);
    archiveSubsectors(map, write);
    archiveSegs(map, write);
    archiveNodes(map, write);
    archiveBlockmap(map, write);
    archiveReject(map, write);

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void archiveSymbolTables(boolean write)
{
    if(write)
        beginSegment(DAMSEG_SYMBOLTABLES);
    else
        assertSegment(DAMSEG_SYMBOLTABLES);

    //archiveMaterialDict(materialDict, write);

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void archiveRelocationTables(boolean write)
{
    if(write)
        beginSegment(DAMSEG_RELOCATIONTABLES);
    else
        assertSegment(DAMSEG_RELOCATIONTABLES);

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void archiveHeader(boolean write)
{
    if(write)
        beginSegment(DAMSEG_HEADER);
    else
        assertSegment(DAMSEG_HEADER);

    if(write)
    {
        writeLong(DAM_VERSION);
    }
    else
    {
        mapFileVersion = readLong();
    }

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static boolean doArchiveMap(GameMap* map, const char* path, boolean write)
{
    if(NULL == path || !path[0])
        return false;

    // Open the file.
    if(!openMapFile(path, write))
        return false; // Hmm, invalid path?

    Con_Message("DAM_MapRead: %s cached map %s.\n", write? "Saving" : "Loading", path);

    /*materialDict = M_Calloc(sizeof(*materialDict));
    if(write)
        initMaterialDict(map, materialDict);*/

    archiveHeader(write);
    archiveRelocationTables(write);
    archiveSymbolTables(write);
    archiveMap(map, write);

    // Close the file.
    closeMapFile();

    //M_Free(materialDict);

    return true;
}

boolean DAM_MapWrite(GameMap* map, const char* path)
{
    return doArchiveMap(map, path, true);
}

boolean DAM_MapRead(GameMap* map, const char* path)
{
    return doArchiveMap(map, path, false);
}

boolean DAM_MapIsValid(const char* cachedMapPath, lumpnum_t markerLumpNum)
{
    if(NULL != cachedMapPath && !cachedMapPath[0] && markerLumpNum >= 0)
    {
        uint sourceTime = F_GetLastModified(F_LumpSourceFile(markerLumpNum));
        uint buildTime = F_GetLastModified(cachedMapPath);

        if(F_Access(cachedMapPath) && !(buildTime < sourceTime))
        {
            // Ok, lets check the header.
            if(openMapFile(cachedMapPath, false))
            {
                archiveHeader(false);
                closeMapFile();
                if(mapFileVersion == DAM_VERSION)
                    return true; // Its good.
            }
        }
    }
    return false;
}
