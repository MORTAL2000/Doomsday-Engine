/**\file
 *\section License
 * License: GPL
 * Online License Link: http://www.gnu.org/licenses/gpl.html
 *
 *\author Copyright © 2006-2008 Daniel Swanson <danij@dengine.net>
 *\author Copyright © 2006-2007 Jamie Jones <jamie_jones_au@yahoo.com.au>
 *\author Copyright © 2000-2007 Andrew Apted <ajapted@gmail.com>
 *\author Copyright © 1998-2000 Colin Reed <cph@moria.org.uk>
 *\author Copyright © 1998-2000 Lee Killough <killough@rsn.hp.com>
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
 * bsp_main.c: GL-friendly BSP node builder.
 *
 * Based on glBSP 2.24 (in turn, based on BSP 2.3), which is hosted on
 * SourceForge: http://sourceforge.net/projects/glbsp/
 */

// HEADER FILES ------------------------------------------------------------

#include "de_base.h"
#include "de_bsp.h"
#include "de_misc.h"

#include "p_mapdata.h"

// MACROS ------------------------------------------------------------------

// TYPES -------------------------------------------------------------------

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

// PUBLIC DATA DEFINITIONS -------------------------------------------------

int bspFactor = 7;

// PRIVATE DATA DEFINITIONS ------------------------------------------------

// CODE --------------------------------------------------------------------

/**
 * Register the ccmds and cvars of the BSP builder. Called during engine
 * startup
 */
void BSP_Register(void)
{
    C_VAR_INT("bsp-factor", &bspFactor, CVF_NO_MAX, 0, 0);
}

/**
 * Initially create all half-edges, one for each side of a linedef.
 *
 * \pre Blockmap must be initialized before this is called!
 *
 * @return              The list of created half-edges.
 */
static superblock_t *createInitialHEdges(gamemap_t *map)
{
    uint            startTime = Sys_GetRealTime();

    uint            i;
    int             bw, bh;
    hedge_t        *back, *front;
    superblock_t   *block;

    block = BSP_SuperBlockCreate();

    BSP_GetBMapBounds(&block->bbox[BOXLEFT], &block->bbox[BOXBOTTOM],
                      &bw, &bh);

    block->bbox[BOXRIGHT] = block->bbox[BOXLEFT]   + 128 * M_CeilPow2(bw);
    block->bbox[BOXTOP]   = block->bbox[BOXBOTTOM] + 128 * M_CeilPow2(bh);

    // Step through linedefs and get side numbers.
    for(i = 0; i < map->numLineDefs; ++i)
    {
        linedef_t         *line = &map->lineDefs[i];

        front = back = NULL;

        // Ignore zero-length, overlapping and polyobj lines.
        if(!(line->buildData.mlFlags & MLF_ZEROLENGTH) && !line->buildData.overlap &&
           !(line->buildData.mlFlags & MLF_POLYOBJ))
        {
            // Check for Humungously long lines.
            if(ABS(line->v[0]->buildData.pos[VX] - line->v[1]->buildData.pos[VX]) >= 10000 ||
               ABS(line->v[0]->buildData.pos[VY] - line->v[1]->buildData.pos[VY]) >= 10000)
            {
                if(3000 >=
                   M_Length(line->v[0]->buildData.pos[VX] - line->v[1]->buildData.pos[VX],
                            line->v[0]->buildData.pos[VY] - line->v[1]->buildData.pos[VY]))
                {
                    Con_Message("Linedef #%d is VERY long, it may cause problems\n",
                                line->buildData.index);
                }
            }

            if(line->sideDefs[FRONT])
            {
                sidedef_t     *side = line->sideDefs[FRONT];

                // Check for a bad sidedef.
                if(!side->sector)
                    Con_Message("Bad sidedef on linedef #%d (Z_CheckHeap error)\n",
                                line->buildData.index);

                front = HEdge_Create(line, line, line->v[0], line->v[1],
                                     side->sector, false);
                BSP_AddHEdgeToSuperBlock(block, front);
            }
            else
                Con_Message("Linedef #%d has no front sidedef!\n",
                            line->buildData.index);

            if(line->sideDefs[BACK])
            {
                sidedef_t     *side = line->sideDefs[BACK];

                // Check for a bad sidedef.
                if(!side->sector)
                    Con_Message("Bad sidedef on linedef #%d (Z_CheckHeap error)\n",
                                line->buildData.index);

                back = HEdge_Create(line, line, line->v[1], line->v[0],
                                    side->sector, true);
                BSP_AddHEdgeToSuperBlock(block, back);

                if(front)
                {
                    // Half-edges always maintain a one-to-one relationship
                    // with their twins, so if one gets split, the other
                    // must be split also.
                    back->twin = front;
                    front->twin = back;
                }
            }
            else
            {
                if(line->buildData.mlFlags & MLF_TWOSIDED)
                {
                    Con_Message("Linedef #%d is 2s but has no back sidedef\n",
                                line->buildData.index);
                    line->buildData.mlFlags &= ~MLF_TWOSIDED;
                }

                // Handle the 'One-Sided Window' trick.
                if(line->buildData.windowEffect)
                {
                    hedge_t    *other;

                    other = HEdge_Create(front->lineDef, line,
                                         line->v[1], line->v[0],
                                         line->buildData.windowEffect, true);

                    BSP_AddHEdgeToSuperBlock(block, other);

                    // Setup the twin-ing (it's very strange to have a mini
                    // and a normal partnered together).
                    other->twin = front;
                    front->twin = other;
                }
            }
        }

        // \todo edge tips should be created when half-edges are created.
        {
        double x1 = line->v[0]->buildData.pos[VX];
        double y1 = line->v[0]->buildData.pos[VY];
        double x2 = line->v[1]->buildData.pos[VX];
        double y2 = line->v[1]->buildData.pos[VY];

        BSP_CreateVertexEdgeTip(line->v[0], x2 - x1, y2 - y1, back, front);
        BSP_CreateVertexEdgeTip(line->v[1], x1 - x2, y1 - y2, front, back);
        }
    }

    // How much time did we spend?
    VERBOSE(Con_Message
            ("createInitialHEdges: Done in %.2f seconds.\n",
             (Sys_GetRealTime() - startTime) / 1000.0f));

    return block;
}

static boolean C_DECL freeBSPNodeData(binarytree_t *tree, void *data)
{
    void               *bspData = BinaryTree_GetData(tree);

    if(bspData)
    {
        M_Free(bspData);
    }

    BinaryTree_SetData(tree, NULL);

    return true; // Continue iteration.
}

/**
 * Build the BSP for the given map.
 *
 * @param map           The map to build the BSP for.
 * @param vertexes      Editable vertex (ptr) array.
 * @param numVertexes   Number of vertexes in the array.
 * @return              @c true, if completed successfully.
 */
boolean BSP_Build(gamemap_t *map, vertex_t ***vertexes, uint *numVertexes)
{
    boolean             builtOK;
    uint                startTime;
    superblock_t       *hEdgeList;
    binarytree_t       *rootNode;

    if(verbose >= 1)
    {
        Con_Message("BSP_Build: Processing map using tunable "
                    "factor of %d...\n", bspFactor);
    }

    // It begins...
    startTime = Sys_GetRealTime();

    BSP_InitSuperBlockAllocator();
    BSP_InitIntersectionAllocator();
    BSP_InitHEdgeAllocator();

    BSP_InitForNodeBuild(map);
    BSP_InitAnalyzer(map);

    BSP_DetectOverlappingLines(map);
    BSP_DetectWindowEffects(map);

    // Create initial half-edges.
    hEdgeList = createInitialHEdges(map);

    // Build the BSP.
    {
    uint                buildStartTime = Sys_GetRealTime();
    cutlist_t          *cutList;

    cutList = BSP_CutListCreate();

    // Recursively create nodes.
    rootNode = NULL;
    builtOK = BuildNodes(hEdgeList, &rootNode, 0, cutList);

    // The cutlist data is no longer needed.
    BSP_CutListDestroy(cutList);

    // How much time did we spend?
    VERBOSE(Con_Message
            ("BuildNodes: Done in %.2f seconds.\n",
             (Sys_GetRealTime() - buildStartTime) / 1000.0f));
    }

    BSP_SuperBlockDestroy(hEdgeList);

    if(builtOK)
    {   // Success!
        // Wind the BSP tree and link to the map.
        ClockwiseBspTree(rootNode);
        SaveMap(map, rootNode, vertexes, numVertexes);

        Con_Message("BSP_Build: Built %d Nodes, %d Subsectors, %d Segs, %d Vertexes\n",
                    map->numNodes, map->numSSectors, map->numSegs,
                    map->numVertexes);

        if(rootNode && !BinaryTree_IsLeaf(rootNode))
        {
            long            rHeight, lHeight;

            rHeight = (long)
                BinaryTree_GetHeight(BinaryTree_GetChild(rootNode, RIGHT));
            lHeight = (long)
                BinaryTree_GetHeight(BinaryTree_GetChild(rootNode, LEFT));

            Con_Message("  Balance %+ld (l%ld - r%ld).\n", lHeight - rHeight,
                        lHeight, rHeight);
        }
    }

    // We are finished with the BSP build data.
    if(rootNode)
    {
        BinaryTree_PostOrder(rootNode, freeBSPNodeData, NULL);
        BinaryTree_Destroy(rootNode);
    }
    rootNode = NULL;

    // Free temporary storage.
    BSP_ShutdownHEdgeAllocator();
    BSP_ShutdownIntersectionAllocator();
    BSP_ShutdownSuperBlockAllocator();

    // How much time did we spend?
    VERBOSE(Con_Message("  Done in %.2f seconds.\n",
                        (Sys_GetRealTime() - startTime) / 1000.0f));

    return builtOK;
}
