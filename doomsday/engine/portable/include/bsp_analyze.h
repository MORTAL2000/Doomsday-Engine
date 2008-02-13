/**\file
 *\section License
 * License: GPL
 * Online License Link: http://www.gnu.org/licenses/gpl.html
 *
 *\author Copyright © 2007-2008 Daniel Swanson <danij@dengine.net>
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
 * bsp_analyze.h: BSP node builder. Analyzing level structures.
 *
 * Based on glBSP 2.24 (in turn, based on BSP 2.3), which is hosted on
 * SourceForge: http://sourceforge.net/projects/glbsp/
 */

#ifndef __BSP_ANALYZE_H__
#define __BSP_ANALYZE_H__

void        BSP_InitAnalyzer(gamemap_t *map);

void        BSP_GetBMapBounds(int *x, int *y, int *w, int *h);

// Detection routines (for finding map errors and editing tricks).
void        BSP_DetectOverlappingLines(gamemap_t *map);
void        BSP_DetectWindowEffects(gamemap_t *map);

// Flags for BSP_PruneRedundantMapData().
#define     PRUNE_LINEDEFS      0x1
#define     PRUNE_VERTEXES      0x2
#define     PRUNE_SIDEDEFS      0x4
#define     PRUNE_SECTORS       0x8
#define     PRUNE_ALL           (PRUNE_LINEDEFS|PRUNE_VERTEXES|PRUNE_SIDEDEFS|PRUNE_SECTORS)

void        BSP_PruneRedundantMapData(editmap_t *map, int flags);
#endif
