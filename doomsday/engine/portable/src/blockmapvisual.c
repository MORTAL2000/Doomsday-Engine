/**
 * @file blockmapvisual.c
 * Graphical Blockmap Visual. @ingroup map
 *
 * @authors Copyright © 2003-2012 Jaakko Keränen <jaakko.keranen@iki.fi>
 * @authors Copyright © 2006-2012 Daniel Swanson <danij@dengine.net>
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

#include <math.h>

#include "de_base.h"
#include "de_system.h"
#include "de_console.h"
#include "de_graphics.h"
#include "de_refresh.h"
#include "de_render.h"
#include "de_play.h"
#include "de_misc.h"
#include "de_ui.h"

byte bmapShowDebug = 0; // 1 = mobjs, 2 = linedefs, 3 = BSP leafs, 4 = polyobjs.
float bmapDebugSize = 1.5f;

#if 0

static int rendMobj(mobj_t* mo, void* paramaters)
{
    vec2_t start, end;
    V2_Set(start, mo->pos[VX] - mo->radius, mo->pos[VY] - mo->radius);
    V2_Set(end,   mo->pos[VX] + mo->radius, mo->pos[VY] + mo->radius);
    glVertex2f(start[VX], start[VY]);
    glVertex2f(  end[VX], start[VY]);
    glVertex2f(  end[VX],   end[VY]);
    glVertex2f(start[VX],   end[VY]);
    return false; // Continue iteration.
}

static int rendLineDef(LineDef* line, void* paramaters)
{
    glVertex2f(line->L_v1pos[VX], line->L_v1pos[VY]);
    glVertex2f(line->L_v2pos[VX], line->L_v2pos[VY]);
    return false; // Continue iteration.
}

static int rendBspLeaf(BspLeaf* bspLeaf, void* paramaters)
{
    const float scale = MAX_OF(bmapDebugSize, 1);
    const float width = (theWindow->geometry.size.width / 16) / scale;
    float length, dx, dy, normal[2], unit[2];
    HEdge** hedgeIter, *hedge;
    vec2_t start, end;

    for(hedgeIter = bspLeaf->hedges; *hedgeIter; hedgeIter++)
    {
        hedge = *hedgeIter;

        V2_Set(start, hedge->HE_v1pos[VX], hedge->HE_v1pos[VY]);
        V2_Set(end,   hedge->HE_v2pos[VX], hedge->HE_v2pos[VY]);

        glBegin(GL_LINES);
            glVertex2fv(start);
            glVertex2fv(end);
        glEnd();

        dx = end[VX] - start[VX];
        dy = end[VY] - start[VY];
        length = sqrt(dx * dx + dy * dy);
        if(length > 0)
        {
            unit[VX] = dx / length;
            unit[VY] = dy / length;
            normal[VX] = -unit[VY];
            normal[VY] = unit[VX];

            GL_BindTextureUnmanaged(GL_PrepareLSTexture(LST_DYNAMIC), GL_LINEAR);
            glEnable(GL_TEXTURE_2D);

            GL_BlendOp(GL_FUNC_ADD);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);

            glBegin(GL_QUADS);
                glTexCoord2f(0.75f, 0.5f);
                glVertex2fv(start);
                glTexCoord2f(0.75f, 0.5f);
                glVertex2fv(end);
                glTexCoord2f(0.75f, 1);
                glVertex2f(end[VX] - normal[VX] * width,
                           end[VY] - normal[VY] * width);
                glTexCoord2f(0.75f, 1);
                glVertex2f(start[VX] - normal[VX] * width,
                           start[VY] - normal[VY] * width);
            glEnd();

            glDisable(GL_TEXTURE_2D);
            GL_BlendMode(BM_NORMAL);
        }

        // Draw the bounding box.
        V2_Set(start, bspLeaf->aaBox.minX, bspLeaf->aaBox.minY);
        V2_Set(end,   bspLeaf->aaBox.maxX, bspLeaf->aaBox.maxY);

        glBegin(GL_LINES);
            glVertex2f(start[VX], start[VY]);
            glVertex2f(  end[VX], start[VY]);
            glVertex2f(  end[VX], start[VY]);
            glVertex2f(  end[VX],   end[VY]);
            glVertex2f(  end[VX],   end[VY]);
            glVertex2f(start[VX],   end[VY]);
            glVertex2f(start[VX],   end[VY]);
            glVertex2f(start[VX], start[VY]);
        glEnd();
    }
    return false; // Continue iteration.
}

int rendCellLineDefs(void* cellPtr, void* paramaters)
{
    BlockmapCell* cell = (BlockmapCell*)cellPtr;
    if(cell && cell->ringNodes)
    {
        bmapiterparams_t biParams;
        biParams.localValidCount = validCount;
        biParams.func = rendLineDef;
        biParams.param = paramaters;

        glBegin(GL_LINES);
            BlockmapCell_IterateObjects(cell, blockmapCellLinesIterator, (void*)&biParams);
        glEnd();
    }
    return false; // Continue iteration.
}

int rendCellPolyobjs(void* cellPtr, void* paramaters)
{
    BlockmapCell* cell = (BlockmapCell*)cellPtr;
    if(cell && cell->ringNodes)
    {
        bmappoiterparams_t bpiParams;
        poiterparams_t piParams;

        piParams.func = rendLineDef;
        piParams.param = paramaters;

        bpiParams.localValidCount = validCount;
        bpiParams.func = PTR_PolyobjLines;
        bpiParams.param = &piParams;

        glBegin(GL_LINES);
            BlockmapCell_IterateObjects(cell, blockmapCellPolyobjsIterator, (void*)&bpiParams);
        glEnd();
    }
    return false; // Continue iteration.
}

int rendCellMobjs(void* cellPtr, void* paramaters)
{
    BlockmapCell* cell = (BlockmapCell*)cellPtr;
    if(cell && cell->ringNodes)
    {
        bmapmoiterparams_t bmiParams;
        bmiParams.localValidCount = validCount;
        bmiParams.func = rendMobj;
        bmiParams.param = paramaters;

        glBegin(GL_QUADS);
            BlockmapCell_IterateObjects(cell, blockmapCellMobjsIterator, (void*)&bmiParams);
        glEnd();
    }
    return false; // Continue iteration.
}

int rendCellBspLeafs(void* cellPtr, void* paramaters)
{
    BlockmapCell* cell = (BlockmapCell*)cellPtr;
    if(cell && cell->ringNodes)
    {
        bmapbspleafiterateparams_t bsiParams;
        bsiParams.localValidCount = validCount;
        bsiParams.func = rendBspLeaf;
        bsiParams.param = paramaters;
        bsiParams.sector = NULL;
        bsiParams.box = NULL;

        BlockmapCell_IterateObjects(cell, blockmapCellBspLeafsIterator, (void*)&bsiParams);
    }
    return false; // Continue iteration.
}

void rendBlockmapBackground(Blockmap* blockmap)
{
    vec2_t start, end;
    uint x, y, bmapSize[2];
    assert(blockmap);

    Gridmap_Size(blockmap->gridmap, bmapSize);

    // Scale modelview matrix so we can express cell geometry
    // using a cell-sized unit coordinate space.
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glScalef(blockmap->cellSize[VX], blockmap->cellSize[VY], 1);

    /**
     * Draw the translucent quad which represents the "used" cells.
     */
    V2_Set(start, 0, 0);
    V2_Set(end, bmapSize[VX], bmapSize[VY]);
    glColor4f(.25f, .25f, .25f, .66f);
    glBegin(GL_QUADS);
        glVertex2f(start[VX], start[VY]);
        glVertex2f(  end[VX], start[VY]);
        glVertex2f(  end[VX],   end[VY]);
        glVertex2f(start[VX],   end[VY]);
    glEnd();

    /**
     * Draw the "null cells" over the top.
     */
    glColor4f(0, 0, 0, .95f);
    for(y = 0; y < bmapSize[VY]; ++y)
    for(x = 0; x < bmapSize[VX]; ++x)
    {
        // If this cell has user data its not null.
        if(Gridmap_CellXY(blockmap->gridmap, x, y, false)) continue;

        glBegin(GL_QUADS);
            glVertex2f(x,   y);
            glVertex2f(x+1, y);
            glVertex2f(x+1, y+1);
            glVertex2f(x,   y+1);
        glEnd();
    }

    // Restore previous GL state.
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

static void drawCellInfo(const Point2Raw* _origin, const char* info)
{
    Point2Raw origin;
    Size2Raw size;
    assert(_origin);

    glEnable(GL_TEXTURE_2D);

    FR_SetFont(fontFixed);
    FR_LoadDefaultAttrib();
    FR_SetShadowOffset(UI_SHADOW_OFFSET, UI_SHADOW_OFFSET);
    FR_SetShadowStrength(UI_SHADOW_STRENGTH);
    size.width  = FR_TextWidth(info)  + 16;
    size.height = FR_SingleLineHeight(info) + 16;

    origin.x = _origin->x;
    origin.y = _origin->y;

    origin.x -= size.width / 2;
    UI_GradientEx(&origin, &size, 6, UI_Color(UIC_BG_MEDIUM), UI_Color(UIC_BG_LIGHT), .5f, .5f);
    UI_DrawRectEx(&origin, &size, 6, false, UI_Color(UIC_BRD_HI), NULL, .5f, -1);

    origin.x += 8;
    origin.y += size.height / 2;
    UI_SetColor(UI_Color(UIC_TEXT));
    UI_TextOutEx2(info, &origin, UI_Color(UIC_TITLE), 1, ALIGN_LEFT, DTF_ONLY_SHADOW);

    glDisable(GL_TEXTURE_2D);
}

static void drawBlockmapInfo(const Point2Raw* _origin, Blockmap* blockmap)
{
    uint bmapSize[2];
    Point2Raw origin;
    Size2Raw size;
    char buf[80];
    int th;
    assert(blockmap);

    glEnable(GL_TEXTURE_2D);

    origin.x = _origin->x;
    origin.y = _origin->y;

    FR_SetFont(fontFixed);
    FR_LoadDefaultAttrib();
    FR_SetShadowOffset(UI_SHADOW_OFFSET, UI_SHADOW_OFFSET);
    FR_SetShadowStrength(UI_SHADOW_STRENGTH);
    size.width = 16 + FR_TextWidth("(+000.0,+000.0)(+000.0,+000.0)");
    th = FR_SingleLineHeight("Info");
    size.height = th * 4 + 16;

    origin.x -= size.width;
    origin.y -= size.height;
    UI_GradientEx(&origin, &size, 6, UI_Color(UIC_BG_MEDIUM), UI_Color(UIC_BG_LIGHT), .5f, .5f);
    UI_DrawRectEx(&origin, &size, 6, false, UI_Color(UIC_BRD_HI), NULL, .5f, -1);

    origin.x += 8;
    origin.y += 8 + th/2;

    UI_TextOutEx2("Blockmap", &origin, UI_Color(UIC_TITLE), 1, ALIGN_LEFT, DTF_ONLY_SHADOW);
    origin.y += th;

    Blockmap_Size(blockmap, bmapSize);
    dd_snprintf(buf, 80, "Dimensions:[%u,%u] #%li", bmapSize[VX], bmapSize[VY],
        (long) bmapSize[VY] * bmapSize[VX]);
    UI_TextOutEx2(buf, &origin, UI_Color(UIC_TEXT), 1, ALIGN_LEFT, DTF_ONLY_SHADOW);
    origin.y += th;

    dd_snprintf(buf, 80, "Cellsize:[%.3f,%.3f]", Blockmap_CellWidth(blockmap), Blockmap_CellHeight(blockmap));
    UI_TextOutEx2(buf, &origin, UI_Color(UIC_TEXT), 1, ALIGN_LEFT, DTF_ONLY_SHADOW);
    origin.y += th;

    dd_snprintf(buf, 80, "(%+06.0f,%+06.0f)(%+06.0f,%+06.0f)",
        Blockmap_Bounds(blockmap)->minX, Blockmap_Bounds(blockmap)->minY,
        Blockmap_Bounds(blockmap)->maxX, Blockmap_Bounds(blockmap)->maxY);
    UI_TextOutEx2(buf, &origin, UI_Color(UIC_TEXT), 1, ALIGN_LEFT, DTF_ONLY_SHADOW);

    glDisable(GL_TEXTURE_2D);
}

static int countCellObject(void* object, void* paramaters)
{
    uint* count = (uint*)paramaters;
    *count += 1;
    return false; // Continue iteration.
}

static void drawLineDefCellInfoBox(Blockmap* blockmap, const Point2Raw* origin, uint coords[2])
{
    uint count = 0;
    char info[160];

    validCount++;
    Blockmap_IterateCellObjects(blockmap, coords, countCellObject, (void*)&count);

    dd_snprintf(info, 160, "Cell:[%u,%u] LineDefs:#%u", coords[VX], coords[VY], count);
    drawCellInfo(origin, info);
}

static void drawMobjCellInfoBox(Blockmap* blockmap, const Point2Raw* origin, uint coords[2])
{
    uint count = 0;
    char info[160];

    validCount++;
    Blockmap_IterateCellObjects(blockmap, coords, countCellObject, (void*)&count);

    dd_snprintf(info, 160, "Cell:[%u,%u] Mobjs:#%u", coords[VX], coords[VY], count);
    drawCellInfo(origin, info);
}

static void drawPolyobjCellInfoBox(Blockmap* blockmap, const Point2Raw* origin, uint coords[2])
{
    uint count = 0;
    char info[160];

    validCount++;
    Blockmap_IterateCellObjects(blockmap, coords, countCellObject, (void*)&count);

    dd_snprintf(info, 160, "Cell:[%u,%u] Polyobjs:#%u", coords[VX], coords[VY], count);
    drawCellInfo(origin, info);
}

static void drawBspLeafCellInfoBox(Blockmap* blockmap, const Point2Raw* origin, uint coords[2])
{
    uint count = 0;
    char info[160];

    validCount++;
    Blockmap_IterateCellObjects(blockmap, coords, countCellObject, (void*)&count);

    dd_snprintf(info, 160, "Cell:[%u,%u] BspLeafs:#%u", coords[VX], coords[VY], count);
    drawCellInfo(origin, info);
}

/**
 * @param blockmap  Blockmap to be rendered.
 * @param followMobj  Mobj to center/focus the visual on. Can be @c NULL.
 * @param cellDrawer  Blockmap cell content drawing callback. Can be @a NULL.
 */
static void rendBlockmap(Blockmap* blockmap, mobj_t* followMobj,
    int (*cellDrawer) (void* cellPtr, void* paramaters))
{
    uint x, y, vCoords[2];
    GridmapBlock vBlockCoords;
    vec2_t start, end, cellSize;
    void* cell;

    V2_Copy(cellSize, Blockmap_CellSize(blockmap));

    if(followMobj)
    {
        // Determine the followed Mobj's blockmap coords.
        if(Blockmap_CellCoords(blockmap, vCoords, followMobj->pos))
            followMobj = NULL; // Outside the blockmap.

        if(followMobj)
        {
            // Determine the extended blockmap coords for the followed
            // Mobj's "touch" range.
            const float radius = followMobj->radius + DDMOBJ_RADIUS_MAX * 2;
            AABoxf aaBox;
            V2_Set(start, followMobj->pos[VX] - radius, followMobj->pos[VY] - radius);
            V2_Set(end,   followMobj->pos[VX] + radius, followMobj->pos[VY] + radius);
            V2_InitBox(aaBox.arvec2, start);
            V2_AddToBox(aaBox.arvec2, end);
            Blockmap_CellBlockCoords(blockmap, &vBlockCoords, &aaBox);
        }
    }

    if(followMobj)
    {
        // Orient on the center of the followed Mobj.
        V2_Set(start, vCoords[VX] * cellSize[VX],
                      vCoords[VY] * cellSize[VY]);
        glTranslatef(-start[VX], -start[VY], 0);
    }
    else
    {
        // Orient on center of the Blockmap.
        glTranslatef(-(cellSize[VX] * Blockmap_Width(blockmap))/2,
                     -(cellSize[VY] * Blockmap_Height(blockmap))/2, 0);
    }

    // First we'll draw a background showing the "null" cells.
    rendBlockmapBackground(blockmap);
    if(followMobj)
    {
        // Highlight cells the followed Mobj "touches".

        for(y = vBlockCoords.minY; y <= vBlockCoords.maxY; ++y)
        for(x = vBlockCoords.minX; x <= vBlockCoords.maxX; ++x)
        {
            if(x == vCoords[VX] && y == vCoords[VY])
            {
                // The cell the followed Mobj is actually in.
                glColor4f(.66f, .66f, 1, .66f);
            }
            else
            {
                // A cell within the followed Mobj's extended collision range.
                glColor4f(.33f, .33f, .66f, .33f);
            }

            V2_Set(start, x * cellSize[VX], y * cellSize[VY]);
            V2_Set(end, cellSize[VX], cellSize[VY]);
            V2_Sum(end, end, start);

            glBegin(GL_QUADS);
                glVertex2f(start[VX], start[VY]);
                glVertex2f(  end[VX], start[VY]);
                glVertex2f(  end[VX],   end[VY]);
                glVertex2f(start[VX],   end[VY]);
            glEnd();
        }

    }

    /**
     * Draw the Gridmap visual.
     * \note Gridmap uses a cell unit size of [width:1,height:1], so we
     * need to scale it up so it aligns correctly.
     */
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glScalef(cellSize[VX], cellSize[VY], 1);

    Gridmap_Debug(blockmap->gridmap);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    /**
     * Draw the blockmap-linked data.
     * Translate the modelview matrix so that objects can be drawn using
     * the map space coordinates directly.
     */
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(-Blockmap_Origin(blockmap)[VX], -Blockmap_Origin(blockmap)[VY], 0);

    if(cellDrawer)
    {
        if(followMobj)
        {
            /**
             * Draw cell contents color coded according to their range
             * from the followed Mobj.
             */

            // First, the cells outside the "touch" range (crimson).
            validCount++;
            glColor4f(.33f, 0, 0, .75f);
            for(y = 0; y < Blockmap_Height(blockmap); ++y)
            for(x = 0; x < Blockmap_Width(blockmap); ++x)
            {
                if(x >= vBlockCoords.minX && x <= vBlockCoords.maxX &&
                   y >= vBlockCoords.minY && y <= vBlockCoords.maxY)
                    continue;

                cell = Gridmap_CellXY(blockmap->gridmap, x, y, false);
                if(cell)
                {
                    cellDrawer(cell, NULL/*no params*/);
                }
            }

            // Next, the cells within the "touch" range (orange).
            validCount++;
            glColor3f(1, .5f, 0);
            for(y = vBlockCoords.minY; y <= vBlockCoords.maxY; ++y)
            for(x = vBlockCoords.minX; x <= vBlockCoords.maxX; ++x)
            {
                if(x == vCoords[VX] && y == vCoords[VY]) continue;

                cell = Gridmap_CellXY(blockmap->gridmap, x, y, false);
                if(cell)
                {
                    cellDrawer(cell, NULL/*no params*/);
                }
            }

            // Lastly, the cell the followed Mobj is in (yellow).
            validCount++;
            glColor3f(1, 1, 0);
            cell = Gridmap_Cell(blockmap->gridmap, vCoords, false);
            if(cell)
            {
                cellDrawer(cell, NULL/*no params*/);
            }
        }
        else
        {
            // Draw all cells without color coding.
            validCount++;
            glColor4f(.33f, 0, 0, .75f);
            Gridmap_Iterate(blockmap->gridmap, cellDrawer);
        }
    }

    /**
     * Draw the followed mobj, if any.
     */
    if(followMobj)
    {
        glColor3f(0, 1, 0);
        glBegin(GL_QUADS);
            rendMobj(followMobj, NULL/*no params*/);
        glEnd();
    }

    // Undo the map coordinate space translation.
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

#endif

void Rend_BlockmapDebug(void)
{
#if 0
    int (*cellDrawer) (void* cellPtr, void* paramaters);
    void (*cellInfoDrawer) (Blockmap* blockmap, const Point2Raw* origin, uint coords[2]);
    mobj_t* followMobj = NULL;
    Blockmap* blockmap;
    Point2Raw origin;
    GameMap* map;
    float scale;

    // Disabled?
    if(!bmapShowDebug || bmapShowDebug > 4) return;

    map = theMap;
    if(!map) return;

    switch(bmapShowDebug)
    {
    default: // MobjLinks.
        if(!map->mobjBlockmap) return;

        blockmap = map->mobjBlockmap;
        cellDrawer = rendCellMobjs;
        cellInfoDrawer = drawMobjCellInfoBox;
        break;

    case 2: // LineDefs.
        if(!map->lineDefBlockmap) return;

        blockmap = map->lineDefBlockmap;
        cellDrawer = rendCellLineDefs;
        cellInfoDrawer = drawLineDefCellInfoBox;
        break;

    case 3: // BspLeafs.
        if(!map->bspLeafBlockmap) return;

        blockmap = map->bspLeafBlockmap;
        cellDrawer = rendCellBspLeafs;
        cellInfoDrawer = drawBspLeafCellInfoBox;
        break;

    case 4: // PolyobjLinks.
        if(!map->polyobjBlockmap) return;

        blockmap = map->polyobjBlockmap;
        cellDrawer = rendCellPolyobjs;
        cellInfoDrawer = drawPolyobjCellInfoBox;
        break;
    }

    LIBDENG_ASSERT_IN_MAIN_THREAD();

    /**
     * Draw the blockmap.
     */
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, theWindow->geometry.size.width, theWindow->geometry.size.height, 0, -1, 1);
    // Orient on the center of the window.
    glTranslatef((theWindow->geometry.size.width / 2), (theWindow->geometry.size.height / 2), 0);

    // Uniform scaling factor for this visual.
    scale = bmapDebugSize / MAX_OF(theWindow->geometry.size.height / 100, 1);
    glScalef(scale, -scale, 1);

    // If possible we'll tailor what we draw relative to the viewPlayer.
    if(viewPlayer && viewPlayer->shared.mo)
        followMobj = viewPlayer->shared.mo;

    // Draw!
    rendBlockmap(blockmap, followMobj, cellDrawer);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    /**
     * Draw HUD info.
     */
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, theWindow->geometry.size.width, theWindow->geometry.size.height, 0, -1, 1);

    if(followMobj)
    {
        // About the cell the followed Mobj is in.
        uint coords[2];
        if(!Blockmap_CellCoords(blockmap, coords, followMobj->pos))
        {
            origin.x = theWindow->geometry.size.width / 2;
            origin.y = 30;
            cellInfoDrawer(blockmap, &origin, coords);
        }
    }

    // About the Blockmap itself.
    origin.x = theWindow->geometry.size.width  - 10;
    origin.y = theWindow->geometry.size.height - 10;
    drawBlockmapInfo(&origin, blockmap);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
#endif
}
