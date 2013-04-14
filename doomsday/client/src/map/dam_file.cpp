/** @file dam_file.cpp
 *
 * @authors Copyright © 2007-2013 Daniel Swanson <danij@dengine.net>
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

/**
 * Doomsday Archived Map (DAM) reader/writer.
 */

#if 0

#include <lzss.h>
#include <stdlib.h>

#include "de_base.h"
#include "de_console.h"
#include "de_dam.h"
#include "de_defs.h"
#include "de_misc.h"
#include "de_filesys.h"

#include "map/gamemap.h"
#include "map/p_mapdata.h"

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
    DAMSEG_BSPLEAFS,
    DAMSEG_HEDGES,
    DAMSEG_BSPNODES,
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

/*
static void writeNBytes(void* data, long len)
{
    lzWrite(data, len, mapFile);
}
*/

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

/*
static void readNBytes(void *ptr, long len)
{
    lzRead(ptr, len, mapFile);
}
*/

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

static void writeVertex(GameMap const *map, uint idx)
{
    Vertex const *v = &map->vertexes[idx];

    writeFloat(v->_origin[VX]);
    writeFloat(v->_origin[VY]);
    writeLong((long) v->_numLineOwners);

    if(v->_numLineOwners > 0)
    {
        LineOwner *base = &(v->_lineOwners)->prev();
        LineOwner *own = base;
        do
        {
            writeLong((long) (map->lineIndex(&own->line()) + 1));
            writeLong((long) own->angle());

            own = &own->prev();
        } while(own != base);
    }
}

static void readVertex(GameMap *map, uint idx)
{
    Vertex *v = &map->vertexes[idx];

    v->_origin[VX] = readFloat();
    v->_origin[VY] = readFloat();
    v->_numLineOwners = (uint) readLong();

    if(v->_numLineOwners > 0)
    {
        v->_lineOwners = NULL;
        for(uint i = 0; i < v->_numLineOwners; ++i)
        {
            LineOwner *own = (LineOwner *) Z_Malloc(sizeof(LineOwner), PU_MAP, 0);
            own->_line = &map->lines[(unsigned) (readLong() - 1)];
            own->_angle = (binangle_t) readLong();

            own->_link[LineOwner::Next] = v->_lineOwners;
            v->_lineOwners = own;
        }

        LineOwner *own = v->_lineOwners;
        do
        {
            LineOwner *next = &own->next();
            next->_link[LineOwner::Previous] = own;
            own = next;
        } while(own);
        own->_link[LineOwner::Previous] = v->_lineOwners;
    }
}

static void archiveVertexes(GameMap *map, boolean write)
{
    int i;

    if(write)
        beginSegment(DAMSEG_VERTEXES);
    else
        assertSegment(DAMSEG_VERTEXES);

    if(write)
    {
        writeLong(map->vertexes.size());
        for(i = 0; i < map->vertexes.size(); ++i)
            writeVertex(map, i);
    }
    else
    {
        map->vertexes.clearAndResize(readLong());
        for(i = 0; i < map->vertexes.size(); ++i)
            readVertex(map, i);
    }

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void writeLine(GameMap *map, uint idx)
{
    DENG_ASSERT(map);

    Line *l = &map->lines[idx];

    writeLong((long) (map->vertexIndex(static_cast<Vertex const *>(l->_v[0])) + 1));
    writeLong((long) (map->vertexIndex(static_cast<Vertex const *>(l->_v[1])) + 1));
    writeLong(l->_flags);
    writeByte(l->_inFlags);
    writeFloat(l->_direction[VX]);
    writeFloat(l->_direction[VY]);
    writeFloat(l->_aaBox.minX);
    writeFloat(l->_aaBox.minY);
    writeFloat(l->_aaBox.maxX);
    writeFloat(l->_aaBox.maxY);
    writeFloat(l->_length);
    writeLong((long) l->_angle);
    for(int i = 0; i < DDMAXPLAYERS; ++i)
    {
        writeByte(l->_mapped[i]? 1 : 0);
    }

    for(int i = 0; i < 2; ++i)
    {
        Line::Side &side = l->side(i);

        writeLong(side._sector? (map->sectorIndex(static_cast<Sector *>(side._sector)) + 1) : 0);
        writeLong(side._sideDef? (map->sideDefIndex(side._sideDef) + 1) : 0);

        writeLong(side._leftHEdge? (map->hedgeIndex(side._leftHEdge)  + 1) : 0);
        writeLong(side._rightHEdge? (map->hedgeIndex(side._rightHEdge) + 1) : 0);
    }
}

static void readLine(GameMap *map, uint idx)
{
    DENG_ASSERT(map);

    Line *l = &map->lines[idx];

    l->_v[0] = &map->vertexes[(unsigned) (readLong() - 1)];
    l->_v[1] = &map->vertexes[(unsigned) (readLong() - 1)];
    l->_flags = (int) readLong();
    l->_inFlags = readByte();
    l->_direction[VX] = readFloat();
    l->_direction[VY] = readFloat();
    l->_slopeType = M_SlopeType(l->_direction);
    l->_aaBox.minX = readFloat();
    l->_aaBox.minY = readFloat();
    l->_aaBox.maxX = readFloat();
    l->_aaBox.maxY = readFloat();
    l->_length = readFloat();
    l->_angle = (binangle_t) readLong();
    for(int i = 0; i < DDMAXPLAYERS; ++i)
    {
        l->_mapped[i] = (readByte()? true : false);
    }

    for(int i = 0; i < 2; ++i)
    {
        Line::Side &side = l->side(i);
        long index;

        index = readLong();
        side._sector = (index? map->sectors().at(index-1) : NULL);

        index = readLong();
        side._sideDef = (index? map->sideDefs().at(index-1) : NULL);

        index = readLong();
        side._leftHEdge  = (index? map->hedges().at(index-1) : NULL);

        index = readLong();
        side._rightHEdge = (index? map->hedges().at(index-1) : NULL);
    }
}

static void archiveLines(GameMap* map, boolean write)
{
    uint i;

    if(write)
        beginSegment(DAMSEG_LINES);
    else
        assertSegment(DAMSEG_LINES);

    if(write)
    {
        writeLong(map->lineCount());
        for(i = 0; i < map->lineCount(); ++i)
            writeLine(map, i);
    }
    else
    {
        map->lines.clearAndResize(readLong());
        for(i = 0; i < map->lineCount(); ++i)
            readLine(map, i);
    }

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void writeSide(GameMap *map, uint idx)
{
    DENG_ASSERT(map);

    SideDef *s = &map->sideDefs[idx];

    for(uint i = 0; i < 3; ++i)
    {
        Surface *suf = &s->surface(i);

        writeLong(suf->_flags);
        //writeLong(getMaterialDictID(materialDict, suf->material));
        writeLong((long) suf->_blendMode);
        writeFloat(suf->_normal[VX]);
        writeFloat(suf->_normal[VY]);
        writeFloat(suf->_normal[VZ]);
        writeFloat(suf->_offset[VX]);
        writeFloat(suf->_offset[VY]);
        writeFloat(suf->_colorAndAlpha[CR]);
        writeFloat(suf->_colorAndAlpha[CG]);
        writeFloat(suf->_colorAndAlpha[CB]);
        writeFloat(suf->_colorAndAlpha[CA]);
    }
    writeShort(s->_flags);
}

static void readSide(GameMap *map, uint idx)
{
    DENG_ASSERT(map);

    vec2f_t newOrigin;
    vec4f_t newColorAndAlpha;
    SideDef *s = &map->sideDefs[idx];

    for(uint i = 0; i < 3; ++i)
    {
        Surface *suf = &s->surface(i);

        suf->_flags = (int) readLong();
        //suf->setMaterial(lookupMaterialFromDict(materialDict, readLong()));
        suf->setBlendMode(blendmode_t(readLong()));
        suf->_normal[VX] = readFloat();
        suf->_normal[VY] = readFloat();
        suf->_normal[VZ] = readFloat();
        newOrigin[VX] = readFloat();
        newOrigin[VY] = readFloat();
        suf->setMaterialOrigin(newOrigin);
        newColorAndAlpha[CR] = readFloat();
        newColorAndAlpha[CG] = readFloat();
        newColorAndAlpha[CB] = readFloat();
        newColorAndAlpha[CA] = readFloat();
        suf->setColorAndAlpha(newColorAndAlpha);
    }
    s->_flags = readShort();

    s->updateSoundEmitterOrigins();
}

static void archiveSides(GameMap *map, boolean write)
{
    if(write)
        beginSegment(DAMSEG_SIDES);
    else
        assertSegment(DAMSEG_SIDES);

    if(write)
    {
        writeLong(map->sideDefs.size());
        for(int i = 0; i < map->sideDefs.size(); ++i)
        {
            writeSide(map, i);
        }
    }
    else
    {
        map->sideDefs.clearAndResize(readLong());
        for(int i = 0; i < map->sideDefs.size(); ++i)
        {
            readSide(map, i);
        }
    }

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void writeSector(GameMap *map, uint idx)
{
    DENG_ASSERT(map);

    Sector *s = &map->sectors[idx];

    writeFloat(s->_lightLevel);
    writeFloat(s->_lightColor[CR]);
    writeFloat(s->_lightColor[CG]);
    writeFloat(s->_lightColor[CB]);
    writeLong((long) s->_planes.count());
    foreach(Plane *plane, s->_planes)
    {
        writeFloat(plane->_height);
        writeFloat(plane->_targetHeight);
        writeFloat(plane->_speed);
        writeFloat(plane->_visHeight);
        writeFloat(plane->_visHeightDelta);

        Surface &surface = plane->surface();
        writeLong((long) surface._flags);
        //writeLong(getMaterialDictID(materialDict, p->surface().material));
        writeLong((long) surface._blendMode);
        writeFloat(surface._normal[VX]);
        writeFloat(surface._normal[VY]);
        writeFloat(surface._normal[VZ]);
        writeFloat(surface._offset[VX]);
        writeFloat(surface._offset[VY]);
        writeFloat(surface._colorAndAlpha[CR]);
        writeFloat(surface._colorAndAlpha[CG]);
        writeFloat(surface._colorAndAlpha[CB]);
        writeFloat(surface._colorAndAlpha[CA]);
    }

    writeFloat(s->_aaBox.minX);
    writeFloat(s->_aaBox.minY);
    writeFloat(s->_aaBox.maxX);
    writeFloat(s->_aaBox.maxY);

    for(uint i = 0; i < NUM_REVERB_DATA; ++i)
        writeFloat(s->_reverb[i]);

    // Lightgrid block indices.
    writeLong((long) s->_lightGridData.changedBlockCount);
    writeLong((long) s->_lightGridData.blockCount);
    for(uint i = 0; i < s->_lightGridData.blockCount; ++i)
        writeShort(s->_lightGridData.blocks[i]);

    // Line list.
    writeLong((long) s->_lines.count());
    foreach(Line *line, s->_lines)
    {
        writeLong(map->lineIndex(line) + 1);
    }

    // BspLeaf list.
    writeLong((long) s->_bspLeafs.count());
    foreach(BspLeaf *bspLeaf, s->_bspLeafs)
    {
        writeLong(map->bspLeafIndex(bspLeaf) + 1);
    }

    // Reverb BSP leaf attributors.
    writeLong((long) s->_reverbBspLeafs.count());
    foreach(BspLeaf *bspLeaf, s->_reverbBspLeafs)
    {
        writeLong(map->bspLeafIndex(bspLeaf) + 1);
    }
}

static void readSector(GameMap *map, uint idx)
{
    DENG_ASSERT(map);

    Sector *s = &map->sectors[idx];

    s->_lightLevel = readFloat();
    s->_lightColor[CR] = readFloat();
    s->_lightColor[CG] = readFloat();
    s->_lightColor[CB] = readFloat();

    uint numPlanes = (uint) readLong();
    vec2f_t newOrigin;
    vec4f_t newColorAndAlpha;
    for(uint i = 0; i < numPlanes; ++i)
    {
        Plane *p = R_NewPlaneForSector(s);

        p->_height = readFloat();
        p->_targetHeight = readFloat();
        p->_speed = readFloat();
        p->_visHeight = readFloat();
        p->_visHeightDelta = readFloat();

        p->_surface._flags = (int) readLong();
        //p->_surface.setMaterial(lookupMaterialFromDict(materialDict, readLong()));
        p->_surface.setBlendMode(blendmode_t(readLong()));
        p->_surface._normal[VX] = readFloat();
        p->_surface._normal[VY] = readFloat();
        p->_surface._normal[VZ] = readFloat();

        newOrigin[VX] = readFloat();
        newOrigin[VY] = readFloat();
        p->_surface.setMaterialOrigin(newOrigin);

        newColorAndAlpha[CR] = readFloat();
        newColorAndAlpha[CG] = readFloat();
        newColorAndAlpha[CB] = readFloat();
        newColorAndAlpha[CA] = readFloat();
        p->_surface.setColorAndAlpha(newColorAndAlpha);
    }

    s->_aaBox.minX = readFloat();
    s->_aaBox.minY = readFloat();
    s->_aaBox.maxX = readFloat();
    s->_aaBox.maxY = readFloat();

    s->updateSoundEmitterOrigin();
    for(uint i = 0; i < numPlanes; ++i)
    {
        Plane *pln = s->_planes[i];
        pln->updateSoundEmitterOrigin();
    }

    for(uint i = 0; i < NUM_REVERB_DATA; ++i)
    {
        s->_reverb[i] = readFloat();
    }

    // Lightgrid block indices.
    s->_lightGridData.changedBlockCount = (uint) readLong();
    s->_lightGridData.blockCount = (uint) readLong();
    s->_lightGridData.blocks = (ushort *) Z_Malloc(sizeof(ushort) * s->_lightGridData.blockCount, PU_MAP, 0);
    for(uint i = 0; i < s->_lightGridData.blockCount; ++i)
    {
        s->_lightGridData.blocks[i] = readShort();
    }

    // Line list.
    s->_lines.clear();
    int lineCount = readLong();
#ifdef DENG2_QT_4_7_OR_NEWER
    s->_lines.reserve(lineCount);
#endif
    for(int i = 0; i < lineCount; ++i)
    {
        Line *line = map->lines().at(readLong() - 1);
        // Ownership of the line is not given to the sector.
        s->_lines.append(line);
    }

    // BspLeaf list.
    s->_bspLeafs.clear();
    int bspLeafCount = readLong();
#ifdef DENG2_QT_4_7_OR_NEWER
    s->_bspLeafs.reserve(bspLeafCount);
#endif
    for(int i = 0; i < bspLeafCount; ++i)
    {
        BspLeaf *bspLeaf = map->bspLeafs().at(readLong() - 1);
        // Ownership of the BSP leaf is not given to the sector.
        s->_bspLeafs.append(bspLeaf);
    }

    // Reverb BSP leaf attributors.
    s->_reverbBspLeafs.clear();
    int reverbBspLeafCount = readLong();
#ifdef DENG2_QT_4_7_OR_NEWER
    s->_reverbBspLeafs.reserve(reverbBspLeafCount);
#endif
    for(int i = 0; i < reverbBspLeafCount; ++i)
    {
        BspLeaf *bspLeaf = map->bspLeafs().at(readLong() - 1);
        // Ownership of the BSP leaf is not given to the sector.
        s->_reverbBspLeafs.append(bspLeaf);
    }
}

static void archiveSectors(GameMap *map, boolean write)
{
    DENG_ASSERT(map);

    if(write)
        beginSegment(DAMSEG_SECTORS);
    else
        assertSegment(DAMSEG_SECTORS);

    if(write)
    {
        writeLong(map->sectorCount());
        for(uint i = 0; i < map->sectorCount(); ++i)
            writeSector(map, i);
    }
    else
    {
        map->sectors.clearAndResize(readLong());
        for(uint i = 0; i < map->sectorCount(); ++i)
            readSector(map, i);
    }

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void writeBspLeaf(GameMap *map, BspLeaf *s)
{
    DENG_ASSERT(map && s);

    writeFloat(s->_aaBox.minX);
    writeFloat(s->_aaBox.minY);
    writeFloat(s->_aaBox.maxX);
    writeFloat(s->_aaBox.maxY);
    writeFloat(s->_center[VX]);
    writeFloat(s->_center[VY]);
    writeLong(s->_sector? ((map->sectorIndex(static_cast<Sector *>(s->_sector))) + 1) : 0);
    writeLong(s->_polyObj? (s->_polyObj->idx + 1) : 0);

    // BspLeaf reverb.
    for(uint i = 0; i < NUM_REVERB_DATA; ++i)
        writeLong((long) s->_reverb[i]);

    // BspLeaf hedges list.
    writeLong((long) s->_hedgeCount);
    if(!s->_hedge) return;

    HEdge const *base = s->_hedge;
    HEdge const *hedge = base;
    do
    {
        writeLong(map->hedgeIndex(hedge) + 1);
    } while((hedge = hedge->next) != base);
}

static void readBspLeaf(GameMap *map, BspLeaf *s)
{
    DENG_ASSERT(map && s);

    long obIdx;

    s->_aaBox.minX = readFloat();
    s->_aaBox.minY = readFloat();
    s->_aaBox.maxX = readFloat();
    s->_aaBox.maxY = readFloat();
    s->_center[VX] = readFloat();
    s->_center[VY] = readFloat();
    obIdx = readLong();
    s->_sector = (obIdx == 0? NULL : &map->sectors[(unsigned) obIdx - 1]);
    obIdx = readLong();
    s->_polyObj = (obIdx == 0? NULL : map->polyObjs[(unsigned) obIdx - 1]);

    // BspLeaf reverb.
    for(uint i = 0; i < NUM_REVERB_DATA; ++i)
        s->_reverb[i] = (uint) readLong();

    // BspLeaf hedges list.
    s->_hedgeCount = (uint) readLong();
    if(!s->_hedgeCount)
    {
        s->_hedge = 0;
        return;
    }

    HEdge *prevHEdge = 0;
    for(uint i = 0; i < s->_hedgeCount; ++i)
    {
        HEdge *hedge = map->hedges().at((unsigned) readLong() - 1);
        if(!prevHEdge)
        {
            s->_hedge  = hedge;
            prevHEdge = hedge;
        }
        else
        {
            prevHEdge->next = hedge;
            hedge->prev = prevHEdge;
            prevHEdge = hedge;
        }
    }

    s->_hedge->prev = prevHEdge;
}

static void archiveBspLeafs(GameMap* map, boolean write)
{
    //uint i;
    DENG2_UNUSED(map);

    if(write)
        beginSegment(DAMSEG_BSPLEAFS);
    else
        assertSegment(DAMSEG_BSPLEAFS);

    /*if(write)
    {
        writeLong(map->numBspLeafs);
        for(i = 0; i < map->numBspLeafs; ++i)
            writeBspLeaf(map, i);
    }
    else
    {
        map->numBspLeafs = readLong();
        for(i = 0; i < map->numBspLeafs; ++i)
            readBspLeaf(map, i);
    }*/

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void writeSeg(GameMap *map, HEdge *s)
{
    DENG_ASSERT(map && s);

    writeLong(map->vertexIndex(static_cast<Vertex const *>(s->v[0])) + 1);
    writeLong(map->vertexIndex(static_cast<Vertex const *>(s->v[1])) + 1);
    writeFloat(s->length);
    writeFloat(s->offset);
    writeLong(s->line? (map->lineIndex(s->line) + 1) : 0);
    writeLong(s->sector? (map->sectorIndex(s->sector) + 1) : 0);
    writeLong(s->bspLeaf? (map->bspLeafIndex(s->bspLeaf) + 1) : 0);
    writeLong(s->twin? (map->hedgeIndex(s->twin) + 1) : 0);
    writeLong((long) s->angle);
    writeByte(s->side);
    writeLong(s->next? (map->hedgeIndex(s->next) + 1) : 0);
    writeLong(s->twin? (map->hedgeIndex(s->prev) + 1) : 0);
}

static void readSeg(GameMap *map, HEdge *s)
{
    DENG_ASSERT(map && s);
    long obIdx;

    s->v[0] = &map->vertexes[(unsigned) readLong() - 1];
    s->v[1] = &map->vertexes[(unsigned) readLong() - 1];
    s->length = readFloat();
    s->offset = readFloat();
    obIdx = readLong();
    s->line = (obIdx == 0? NULL : &map->lines[(unsigned) obIdx - 1]);
    obIdx = readLong();
    s->sector = (obIdx == 0? NULL : map->sectors().at((unsigned) obIdx - 1));
    obIdx = readLong();
    s->bspLeaf = (obIdx == 0? NULL : map->bspLeafs().at((unsigned) obIdx - 1));
    obIdx = readLong();
    s->twin = (obIdx == 0? NULL : map->hedges().at((unsigned) obIdx - 1));
    s->angle = (angle_t) readLong();
    s->side = readByte();
    obIdx = readLong();
    s->next = (obIdx == 0? NULL : map->hedges().at((unsigned) obIdx - 1));
    obIdx = readLong();
    s->prev = (obIdx == 0? NULL : map->hedges().at((unsigned) obIdx - 1));
}

static void archiveSegs(GameMap *map, boolean write)
{
    DENG_UNUSED(map);
    //uint                i;

    if(write)
        beginSegment(DAMSEG_HEDGES);
    else
        assertSegment(DAMSEG_HEDGES);

    /*if(write)
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
    }*/

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

#define NF_LEAF            0x80000000

static void writeBspReference(GameMap* map, de::MapElement* bspRef)
{
    assert(map);
    if(bspRef->type() == DMU_BSPLEAF)
        writeLong((long)(map->bspLeafIndex(bspRef->castTo<BspLeaf>()) | NF_LEAF));
    else
        writeLong((long)map->bspNodeIndex(bspRef->castTo<BspNode>()));
}

static MapElement *readBspReference(GameMap *map)
{
    DENG_ASSERT(map);
    long idx = readLong();
    if(idx & NF_LEAF)
    {
        return map->bspLeaf().at(idx & ~NF_LEAF);
    }
    return map->bspNodes().at(idx);
}

#undef NF_LEAF

static void writeNode(GameMap *map, BspNode *n)
{
    DENG_ASSERT(map && n);

    writeFloat(n->_partition._origin[VX]);
    writeFloat(n->_partition._origin[VY]);
    writeFloat(n->_partition._direction[VX]);
    writeFloat(n->_partition._direction[VY]);
    writeFloat(n->_aaBox[RIGHT].minX);
    writeFloat(n->_aaBox[RIGHT].minY);
    writeFloat(n->_aaBox[RIGHT].maxX);
    writeFloat(n->_aaBox[RIGHT].maxY);
    writeFloat(n->_aaBox[LEFT ].minX);
    writeFloat(n->_aaBox[LEFT ].minY);
    writeFloat(n->_aaBox[LEFT ].maxX);
    writeFloat(n->_aaBox[LEFT ].maxY);
    writeBspReference(map, n->_children[RIGHT]);
    writeBspReference(map, n->_children[LEFT]);
}

static void readNode(GameMap *map, BspNode *n)
{
    DENG_ASSERT(map && n);

    n->_partition._origin[VX] = readFloat();
    n->_partition._origin[VY] = readFloat();
    n->_partition._direction[VX] = readFloat();
    n->_partition._direction[VY] = readFloat();
    n->_aaBox[RIGHT].minX = readFloat();
    n->_aaBox[RIGHT].minY = readFloat();
    n->_aaBox[RIGHT].maxX = readFloat();
    n->_aaBox[RIGHT].maxY = readFloat();
    n->_aaBox[LEFT ].minX = readFloat();
    n->_aaBox[LEFT ].minY = readFloat();
    n->_aaBox[LEFT ].maxX = readFloat();
    n->_aaBox[LEFT ].maxY = readFloat();
    n->_children[RIGHT] = readBspReference(map);
    n->_children[LEFT]  = readBspReference(map);
}

static void archiveNodes(GameMap* map, boolean write)
{
    DENG_UNUSED(map);
    //uint i;

    if(write)
        beginSegment(DAMSEG_BSPNODES);
    else
        assertSegment(DAMSEG_BSPNODES);

    /*if(write)
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
    }*/

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void archiveBlockmap(GameMap *map, boolean write)
{
    DENG_UNUSED(map);

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
    DENG_UNUSED(map);

    if(write)
        beginSegment(DAMSEG_REJECT);
    else
        assertSegment(DAMSEG_REJECT);

    if(write)
        endSegment();
    else
        assertSegment(DAMSEG_END);
}

static void writePolyobj(GameMap *map, uint idx)
{
    DENG_ASSERT(map);

    Polyobj *p = map->_polyobjs[idx];

    writeLong((long) p->idx);
    writeFloat(p->origin[VX]);
    writeFloat(p->origin[VY]);
    writeFloat(p->origin[VZ]);
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
    for(uint i = 0; i < p->lineCount; ++i)
    {
        Line *line = p->lines[i];
        HEdge *he = line->front()._leftHEdge;

        writeLong(map->vertexIndex(he->_v[0]) + 1);
        writeLong(map->vertexIndex(he->_v[1]) + 1);
        writeFloat(he->_length);
        writeFloat(he->_lineOffset);
        writeLong(he->_line? (map->lineIndex(he->_line) + 1) : 0);
        writeByte(he->_lineSide);
        writeLong(he->_sector? (map->sectorIndex(he->_sector) + 1) : 0);
        writeLong((long) he->_angle);
    }
}

static void readPolyobj(GameMap *map, uint idx)
{
    DENG_ASSERT(map);

    Polyobj *p = map->_polyobjs[idx];

    p->idx = (uint) readLong();
    p->origin[VX] = readFloat();
    p->origin[VY] = readFloat();
    p->origin[VZ] = readFloat();
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

    HEdge *hedges = (HEdge *) Z_Calloc(sizeof(HEdge) * p->lineCount, PU_MAP, 0);
    p->lines = (Line **) Z_Malloc(sizeof(Line *) * (p->lineCount + 1), PU_MAP, 0);
    for(uint i = 0; i < p->lineCount; ++i)
    {
        HEdge *he = hedges + i;

        he->_v[0] = map->_vertexes[(unsigned) readLong() - 1];
        he->_v[1] = map->_vertexes[(unsigned) readLong() - 1];
        he->_length = readFloat();
        he->_lineOffset = readFloat();

        long obIdx = readLong();
        DENG_ASSERT(obIdx);
        he->_line = map->_lines[(unsigned) obIdx - 1];
        he->_lineSide = (readByte()? 1 : 0);

        obIdx = readLong();
        he->_sector = (obIdx == 0? NULL : map->_sectors[(unsigned) obIdx - 1]);

        he->_angle = (angle_t) readLong();

        Line &line = *he->_line;
        line.front()._leftHEdge = line.front()._rightHEdge = he;

        p->lines[i] = &line;
    }
    p->lines[p->lineCount] = NULL; // Terminate.
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
            gx.SetupForMapData(DMU_VERTEX, map->vertexCount());
            gx.SetupForMapData(DMU_LINE, map->lineCount());
            gx.SetupForMapData(DMU_SIDEDEF, map->sideDefCount());
            gx.SetupForMapData(DMU_SECTOR, map->sectorCount());
        }
    }

    archivePolyobjs(map, write);
    archiveVertexes(map, write);
    archiveLines(map, write); // Must follow vertexes (lineowner nodes).
    archiveSides(map, write);
    archiveSectors(map, write);
    archiveBspLeafs(map, write);
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

    Con_Message("DAM_MapRead: %s cached map %s.", write? "Saving" : "Loading", path);

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

boolean DAM_MapIsValid(char const* cachedMapPath, lumpnum_t markerLumpNum)
{
    if(cachedMapPath && !cachedMapPath[0] && markerLumpNum >= 0)
    {
        uint const sourceTime = F_LumpLastModified(markerLumpNum);
        uint const buildTime  = F_GetLastModified(cachedMapPath);

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

#endif
