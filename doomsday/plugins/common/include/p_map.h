/**\file
 *\section License
 * License: GPL
 * Online License Link: http://www.gnu.org/licenses/gpl.html
 *
 *\author Copyright © 2006-2012 Daniel Swanson <danij@dengine.net>
 *\author Copyright © 1993-1996 by id Software, Inc.
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
 * p_map.h : Common play/maputil functions.
 */

#ifndef __COMMON_P_LOCAL__
#define __COMMON_P_LOCAL__

extern float attackRange;

// If "floatOk" true, move would be ok if within "tmFloorZ - tmCeilingZ".
extern boolean floatOk;
extern float tmFloorZ;
extern float tmCeilingZ;
extern material_t* tmFloorMaterial;

extern LineDef* ceilingLine, *floorLine;
extern LineDef* blockLine;
extern mobj_t* lineTarget; // Who got hit (or NULL).
extern mobj_t* tmThing;

#if __JHEXEN__
extern mobj_t* puffSpawned;
extern mobj_t* blockingMobj;
#endif

extern AABoxf tmBox;
extern boolean fellDown;

boolean         P_CheckSight(const mobj_t* from, const mobj_t* to);

boolean         P_CheckPosition2f(mobj_t* thing, float x, float y);
boolean         P_CheckPosition3f(mobj_t* thing, float x, float y, float z);
boolean         P_CheckPosition3fv(mobj_t* thing, const float pos[3]);

#if __JHEXEN__
void P_RadiusAttack(mobj_t* spot, mobj_t* source, int damage, int distance,
                    boolean canDamageSource);
#else
void P_RadiusAttack(mobj_t* spot, mobj_t* source, int damage, int distance);
#endif

boolean         P_TryMove3f(mobj_t* thing, float x, float y, float z);

#if !__JHEXEN__
boolean         P_TryMove(mobj_t* thing, float x, float y,
                          boolean dropoff, boolean slide);
#else
boolean         P_TryMove(mobj_t* thing, float x, float y);
#endif

boolean         P_TeleportMove(mobj_t* thing, float x, float y,
                               boolean alwaysStomp);
void            P_SlideMove(mobj_t* mo);

void            P_UseLines(player_t* player);

boolean         P_ChangeSector(sector_t* sector, boolean crunch);
void            P_HandleSectorHeightChange(int sectorIdx);

float           P_AimLineAttack(mobj_t* t1, angle_t angle, float distance);
void            P_LineAttack(mobj_t* t1, angle_t angle, float distance,
                             float slope, int damage);

float           P_GetGravity(void);

boolean         P_CheckSides(mobj_t* actor, float x, float y);

#if __JHEXEN__
boolean         P_TestMobjLocation(mobj_t* mobj);
void            PIT_ThrustSpike(mobj_t* actor);
boolean         P_UsePuzzleItem(player_t* player, int itemType);
#endif

#endif
