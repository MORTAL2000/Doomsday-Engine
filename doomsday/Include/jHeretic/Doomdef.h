
// DoomDef.h

#ifndef __DOOMDEF__
#define __DOOMDEF__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "version.h"

#ifdef WIN32
#pragma warning (disable:4244 4761)
#define stricmp _stricmp
#define strnicmp _strnicmp
#define strupr _strupr
#define strlwr _strlwr
#endif

#include "../doomsday.h"
#include "../dd_api.h"
#include "g_dgl.h"
#include "p_ticcmd.h"
#include "H_Action.h"

#define Get		DD_GetInteger
#define Set		DD_SetInteger

extern game_import_t gi;
extern game_export_t gx;

#define NUM_XHAIRS		6

// Data tables.
#define mobjinfo	(*gi.mobjinfo)
#define states		(*gi.states)

// if rangecheck is undefined, most parameter validation debugging code
// will not be compiled
//#define RANGECHECK

// all external data is defined here
#include "Doomdata.h"

// all important printed strings
#include "Dstrings.h"

// header generated by multigen utility
#include "Info.h"

enum { VX, VY, VZ };			   // Vertex indices.

#define MAXCHAR ((char)0x7f)
#define MAXSHORT ((short)0x7fff)
#define MAXINT	((int)0x7fffffff)  /* max pos 32-bit int */
#define MAXLONG ((long)0x7fffffff)

#define MINCHAR ((char)0x80)
#define MINSHORT ((short)0x8000)
#define MININT 	((int)0x80000000)  /* max negative 32-bit integer */
#define MINLONG ((long)0x80000000)

#define	FINEANGLES			8192
#define	FINEMASK			(FINEANGLES-1)
#define	ANGLETOFINESHIFT	19	   // 0x100000000 to 0x2000

/*
 * GLOBAL TYPES
 */

#ifdef WIN32
// I don't want MSVC bitching about macro redefinition...
#pragma warning (disable:4005)
#endif

#define NUMARTIFCTS	28
#define MAXPLAYERS	16
#define TICRATE		35			   // number of tics / second
#define TICSPERSEC	35

#ifdef WIN32
#pragma warning (default:4005)
#endif

typedef enum {
	sk_baby,
	sk_easy,
	sk_medium,
	sk_hard,
	sk_nightmare
} skill_t;

#if 0
typedef struct {
	char            forwardMove;   // *2048 for move
	char            sideMove;	   // *2048 for move
	unsigned short  angle;		   // <<16 for angle
	short           pitch;		   // view pitch
	byte            actions;
	byte            lookfly;	   // look/fly up/down/centering
	byte            arti;		   // artitype_t to use
} ticcmd_t;

#define	BT_ATTACK		1
#define	BT_USE			2
#define	BT_CHANGE		4		   // if true, the next 3 bits hold weapon num
#define	BT_WEAPONMASK	(8+16+32)
#define	BT_WEAPONSHIFT	3

#define BT_SPECIAL		128		   // game events, not really buttons
//#define   BTS_SAVEMASK    (4+8+16)
//#define   BTS_SAVESHIFT   2
#define	BT_SPECIALMASK	3
#define	BTS_PAUSE		1		   // pause the game
//#define   BTS_SAVEGAME    2       // save the game at each console
// savegame slot numbers occupy the second byte of buttons

// The high bits of arti are used for special flags.
#define AFLAG_JUMP		0x80
#define AFLAG_MASK		0x7F
#endif

typedef enum {
	GS_LEVEL,
	GS_INTERMISSION,
	GS_FINALE,
	GS_DEMOSCREEN,
	GS_WAITING,
	GS_INFINE
} gamestate_t;

typedef enum {
	ga_nothing,
	ga_loadlevel,
	ga_newgame,
	ga_loadgame,
	ga_savegame,
	ga_playdemo,
	ga_completed,
	ga_victory,
	ga_worlddone,
	ga_screenshot
} gameaction_t;

typedef enum {
	wipe_0,
	wipe_1,
	wipe_2,
	wipe_3,
	wipe_4,
	NUMWIPES,
	wipe_random
} wipe_t;

/*
   ===============================================================================

   MAPOBJ DATA

   ===============================================================================
 */

struct player_s;

typedef struct mobj_s {
	// Defined in dd_share.h; required mobj elements.
	DD_BASE_MOBJ_ELEMENTS() mobjinfo_t *info;	// &mobjinfo[mobj->type]
	int             damage;		   // For missiles
	int             flags;
	int             flags2;		   // Heretic flags
	int             special1;	   // Special info
	int             special2;	   // Special info
	int             health;
	int             movedir;	   // 0-7
	int             movecount;	   // when 0, select a new dir
	struct mobj_s  *target;		   // thing being chased/attacked (or NULL)
	// also the originator for missiles
	int             reactiontime;  // if non 0, don't attack yet
	// used by player to freeze a bit after
	// teleporting
	int             threshold;	   // if >0, the target will be chased
	// no matter what (even if shot)
	struct player_s *player;	   // only valid if type == MT_PLAYER
	int             lastlook;	   // player number last looked for

	mapthing_t      spawnpoint;	   // for nightmare respawn
	int             turntime;	   // $visangle-facetarget
	int             corpsetics;	   // $vanish: how long has this been dead?
} mobj_t;

// each sector has a degenmobj_t in it's center for sound origin purposes
/*typedef struct
   {
   thinker_t        thinker;        // not used for anything
   fixed_t          x,y,z;
   } degenmobj_t; */

// Most damage defined using HITDICE
#define HITDICE(a) ((1+(P_Random()&7))*a)

//
// frame flags
//
#define	FF_FULLBRIGHT	0x8000	   // flag in thing->frame
#define FF_FRAMEMASK	0x7fff

// --- mobj.flags ---

#define	MF_SPECIAL		1		   // call P_SpecialThing when touched
#define	MF_SOLID		2
#define	MF_SHOOTABLE	4
#define	MF_NOSECTOR		8		   // don't use the sector links
									// (invisible but touchable)
#define	MF_NOBLOCKMAP	16		   // don't use the blocklinks
									// (inert but displayable)
#define	MF_AMBUSH		32
#define	MF_JUSTHIT		64		   // try to attack right back
#define	MF_JUSTATTACKED	128		   // take at least one step before attacking
#define	MF_SPAWNCEILING	256		   // hang from ceiling instead of floor
#define	MF_NOGRAVITY	512		   // don't apply gravity every tic

// movement flags
#define	MF_DROPOFF		0x400	   // allow jumps from high places
#define	MF_PICKUP		0x800	   // for players to pick up items
#define	MF_NOCLIP		0x1000	   // player cheat
#define	MF_SLIDE		0x2000	   // keep info about sliding along walls
#define	MF_FLOAT		0x4000	   // allow moves to any height, no gravity
#define	MF_TELEPORT		0x8000	   // don't cross lines or look at heights
#define MF_MISSILE		0x10000	   // don't hit same species, explode on block

#define	MF_DROPPED		0x20000	   // dropped by a demon, not level spawned
#define	MF_SHADOW		0x40000	   // use fuzzy draw (shadow demons / invis)
#define	MF_NOBLOOD		0x80000	   // don't bleed when shot (use puff)
#define	MF_CORPSE		0x100000   // don't stop moving halfway off a step
#define	MF_INFLOAT		0x200000   // floating to a height for a move, don't
									// auto float to target's height

#define	MF_COUNTKILL	0x400000   // count towards intermission kill total
#define	MF_COUNTITEM	0x800000   // count towards intermission item total

#define	MF_SKULLFLY		0x1000000  // skull in flight
#define	MF_NOTDMATCH	0x2000000  // don't spawn in death match (key cards)

#define	MF_TRANSLATION	0xc000000  // if 0x4 0x8 or 0xc, use a translation
#define	MF_TRANSSHIFT	26		   // table for player colormaps

#define MF_LOCAL			0x10000000	// Won't be sent to clients.
#define MF_BRIGHTSHADOW		0x20000000
#define MF_BRIGHTEXPLODE	0x40000000	// Make this brightshadow when exploding.
#define MF_VIEWALIGN		0x80000000

// --- mobj.flags2 ---

#define MF2_LOGRAV			0x00000001	// alternate gravity setting
#define MF2_WINDTHRUST		0x00000002	// gets pushed around by the wind
										// specials
#define MF2_FLOORBOUNCE		0x00000004	// bounces off the floor
#define MF2_THRUGHOST		0x00000008	// missile will pass through ghosts
#define MF2_FLY				0x00000010	// fly mode is active
#define MF2_FOOTCLIP		0x00000020	// if feet are allowed to be clipped
#define MF2_SPAWNFLOAT		0x00000040	// spawn random float z
#define MF2_NOTELEPORT		0x00000080	// does not teleport
#define MF2_RIP				0x00000100	// missile rips through solid
										// targets
#define MF2_PUSHABLE		0x00000200	// can be pushed by other moving
										// mobjs
#define MF2_SLIDE			0x00000400	// slides against walls
#define MF2_ONMOBJ			0x00000800	// mobj is resting on top of another
										// mobj
#define MF2_PASSMOBJ		0x00001000	// Enable z block checking.  If on,
										// this flag will allow the mobj to
										// pass over/under other mobjs.
#define MF2_CANNOTPUSH		0x00002000	// cannot push other pushable mobjs
#define MF2_FEETARECLIPPED	0x00004000	// a mobj's feet are now being cut
#define MF2_BOSS			0x00008000	// mobj is a major boss
#define MF2_FIREDAMAGE		0x00010000	// does fire damage
#define MF2_NODMGTHRUST		0x00020000	// does not thrust target when
										// damaging
#define MF2_TELESTOMP		0x00040000	// mobj can stomp another
#define MF2_FLOATBOB		0x00080000	// use float bobbing z movement
#define MF2_DONTDRAW		0X00100000	// don't generate a vissprite

//=============================================================================
typedef enum {
	PST_LIVE,					   // playing
	PST_DEAD,					   // dead on the ground
	PST_REBORN					   // ready to restart
} playerstate_t;

// psprites are scaled shapes directly on the view screen
// coordinates are given for a 320*200 view screen
typedef enum {
	ps_weapon,
	ps_flash,
	NUMPSPRITES
} psprnum_t;

typedef struct {
	state_t        *state;		   // a NULL state means not active
	int             tics;
	fixed_t         sx, sy;
} pspdef_t;

typedef enum {
	key_yellow,
	key_green,
	key_blue,
	NUMKEYS
} keytype_t;

typedef enum {
	wp_staff,
	wp_goldwand,
	wp_crossbow,
	wp_blaster,
	wp_skullrod,
	wp_phoenixrod,
	wp_mace,
	wp_gauntlets,
	wp_beak,
	NUMWEAPONS,
	wp_nochange
} weapontype_t;

#define AMMO_GWND_WIMPY 10
#define AMMO_GWND_HEFTY 50
#define AMMO_CBOW_WIMPY 5
#define AMMO_CBOW_HEFTY 20
#define AMMO_BLSR_WIMPY 10
#define AMMO_BLSR_HEFTY 25
#define AMMO_SKRD_WIMPY 20
#define AMMO_SKRD_HEFTY 100
#define AMMO_PHRD_WIMPY 1
#define AMMO_PHRD_HEFTY 10
#define AMMO_MACE_WIMPY 20
#define AMMO_MACE_HEFTY 100

typedef enum {
	am_goldwand,
	am_crossbow,
	am_blaster,
	am_skullrod,
	am_phoenixrod,
	am_mace,
	NUMAMMO,
	am_noammo					   // staff, gauntlets
} ammotype_t;

typedef struct {
	ammotype_t      ammo;
	int             upstate;
	int             downstate;
	int             readystate;
	int             atkstate;
	int             holdatkstate;
	int             flashstate;
} weaponinfo_t;

extern weaponinfo_t wpnlev1info[NUMWEAPONS];
extern weaponinfo_t wpnlev2info[NUMWEAPONS];

typedef enum {
	arti_none,
	arti_invulnerability,
	arti_invisibility,
	arti_health,
	arti_superhealth,
	arti_tomeofpower,
	arti_torch,
	arti_firebomb,
	arti_egg,
	arti_fly,
	arti_teleport,
	NUMARTIFACTS
} artitype_t;

typedef enum {
	pw_None,
	pw_invulnerability,
	pw_invisibility,
	pw_allmap,
	pw_infrared,
	pw_weaponlevel2,
	pw_flight,
	pw_shield,
	pw_health2,
	NUMPOWERS
} powertype_t;

#define	INVULNTICS (30*35)
#define	INVISTICS (60*35)
#define	INFRATICS (120*35)
#define	IRONTICS (60*35)
#define WPNLEV2TICS (40*35)
#define FLIGHTTICS (60*35)

#define CHICKENTICS (40*35)

#define MESSAGETICS (4*35)
#define BLINKTHRESHOLD (4*32)

#define NUMINVENTORYSLOTS	14
typedef struct {
	int             type;
	int             count;
} inventory_t;

/*
   ================
   =
   = player_t
   =
   ================
 */

typedef struct player_s {
	ddplayer_t     *plr;		   // Pointer to the engine's player data.
	playerstate_t   playerstate;
	ticcmd_t        cmd;

	//fixed_t       deltaviewheight;        // squat speed
	fixed_t         bob;		   // bounded/scaled total momentum

	int             flyheight;
	boolean         centering;
	int             health;		   // only used between levels, mo->health
	// is used during levels
	int             armorpoints, armortype;	// armor type is 0-2

	inventory_t     inventory[NUMINVENTORYSLOTS];
	artitype_t      readyArtifact;
	int             artifactCount;
	int             inventorySlotNum;
	int             powers[NUMPOWERS];
	boolean         keys[NUMKEYS];
	boolean         backpack;
	signed int      frags[MAXPLAYERS];	// kills of other players
	weapontype_t    readyweapon;
	weapontype_t    pendingweapon; // wp_nochange if not changing
	boolean         weaponowned[NUMWEAPONS];
	int             ammo[NUMAMMO];
	int             maxammo[NUMAMMO];
	int             attackdown, usedown;	// true if button down last tic
	int             cheats;		   // bit flags

	int             refire;		   // refired shots are less accurate

	int             killcount, itemcount, secretcount;	// for intermission
	char           *message;	   // hint messages
	int             messageTics;   // counter for showing messages
	int             damagecount, bonuscount;	// for screen flashing
	int             flamecount;	   // for flame thrower duration
	mobj_t         *attacker;	   // who did damage (NULL for floors)
	int             colormap;	   // 0-3 for which color to draw player
	pspdef_t        psprites[NUMPSPRITES];	// view sprites (gun, etc)
	boolean         didsecret;	   // true if secret level has been done
	int             chickenTics;   // player is a chicken if > 0
	int             chickenPeck;   // chicken peck countdown
	mobj_t         *rain1;		   // active rain maker 1
	mobj_t         *rain2;		   // active rain maker 2
	int             jumpTics;	   // when zero, the player can jump
	int             update;		   // update flags for servers
	int             startspot;	   // which playerstart to use?
	int             viewlock;	   // $democam
} player_t;

#define CF_NOCLIP		1
#define	CF_GODMODE		2
#define	CF_NOMOMENTUM	4		   // not really a cheat, just a debug aid

//#define   BACKUPTICS      12      // CHANGED FROM 12 !?!?

#define	SBARHEIGHT	42			   // status bar height at bottom of screen

/*
   ===============================================================================

   GLOBAL VARIABLES

   ===============================================================================
 */

#define TELEFOGHEIGHT (32*FRACUNIT)

#define MAXEVENTS 64

extern fixed_t  finesine[5 * FINEANGLES / 4];
extern fixed_t *finecosine;

extern gameaction_t gameaction;

extern boolean  paused;

extern boolean  shareware;		   // true if main WAD is the shareware version
extern boolean  ExtendedWAD;	   // true if main WAD is the extended version

extern boolean  nomonsters;		   // checkparm of -nomonsters

extern boolean  respawnparm;	   // checkparm of -respawn

extern boolean  debugmode;		   // checkparm of -debug

extern boolean  usergame;		   // ok to save / end game

extern boolean  ravpic;			   // checkparm of -ravpic

extern boolean  altpal;			   // checkparm to use an alternate palette routine

extern boolean  cdrom;			   // true if cd-rom mode active ("-cdrom")

extern boolean  deathmatch;		   // only if started as net death

#define consoleplayer	Get(DD_CONSOLEPLAYER)
#define displayplayer	Get(DD_DISPLAYPLAYER)

extern int      viewangleoffset;   // ANG90 = left side, ANG270 = right

extern player_t players[MAXPLAYERS];

//extern int        PlayerColor[MAXPLAYERS];

extern boolean  singletics;		   // debug flag to cancel adaptiveness

extern boolean  DebugSound;		   // debug flag for displaying sound info

extern int      maxammo[NUMAMMO];

//extern    boolean     demoplayback;
extern int      skytexture;

extern gamestate_t gamestate;
extern skill_t  gameskill;
extern boolean  respawnmonsters;
extern int      gameepisode;
extern int      gamemap;
extern int      prevmap;
extern int      totalkills, totalitems, totalsecret;	// for intermission
extern int      levelstarttic;	   // gametic at level start
extern int      leveltime;		   // tics in game play for par
extern int      actual_leveltime;

#define ticdup			1		   //gi.Get(DD_TICDUP)

extern int      rndindex;

#define gametic		Get(DD_GAMETIC)
#define maketic		Get(DD_MAKETIC)

#define SAVESTRINGSIZE 24

extern mapthing_t *deathmatch_p;
extern mapthing_t deathmatchstarts[16];

extern int      viewwindowx;
extern int      viewwindowy;
extern int      viewwidth;
extern int      scaledviewwidth;

//extern int viewheight;

extern int      mouseSensitivity;

extern boolean  precache;		   // if true, load all graphics at level load

extern boolean  singledemo;		   // quit after playing a demo from cmdline

extern FILE    *debugfile;
extern int      bodyqueslot;
extern skill_t  startskill;
extern int      startepisode;
extern int      startmap;
extern boolean  autostart;

/*
   ===============================================================================

   GLOBAL FUNCTIONS

   ===============================================================================
 */

//----------
//BASE LEVEL
//----------
void            H_IdentifyVersion(void);
void            D_DoomMain(void);
void            IncThermo(void);
void            InitThermo(int max);
void            tprintf(char *string, int initflag);

// not a globally visible function, just included for source reference
// calls all startup code
// parses command line options
// if not overrided, calls N_AdvanceDemo

void            D_DoomLoop(void);

// not a globally visible function, just included for source reference
// called by D_DoomMain, never exits
// manages timing and IO
// calls all ?_Responder, ?_Ticker, and ?_Drawer functions
// calls I_GetTime, I_StartFrame, and I_StartTic

void            D_PostEvent(event_t *ev);

// called by IO functions when input is detected

void            NetUpdate(void);

// create any new ticcmds and broadcast to other players

void            D_QuitNetGame(void);

// broadcasts special packets to other players to notify of game exit

void            TryRunTics(void);

char           *G_Get(int id);

//---------
//SYSTEM IO
//---------
#if 1
#define	SCREENWIDTH		320
#define	SCREENHEIGHT	200
#else
#define	SCREENWIDTH		560
#define	SCREENHEIGHT	375
#endif

void            I_StartFrame(void);

// called by D_DoomLoop
// called before processing any tics in a frame (just after displaying a frame)
// time consuming syncronous operations are performed here (joystick reading)
// can call D_PostEvent

void            I_StartTic(void);

// called by D_DoomLoop
// called before processing each tic in a frame
// quick syncronous operations are performed here
// can call D_PostEvent

// asyncronous interrupt functions should maintain private ques that are
// read by the syncronous functions to be converted into events

void            I_Init(void);

// called by D_DoomMain
// determines the hardware configuration and sets up the video mode

void            I_BeginRead(void);
void            I_EndRead(void);

//----
//GAME
//----

void            G_DeathMatchSpawnPlayer(int playernum);

void            G_InitNew(skill_t skill, int episode, int map);

void            G_DeferedInitNew(skill_t skill, int episode, int map);

// can be called by the startup code or M_Responder
// a normal game starts at map 1, but a warp test can start elsewhere

void            G_DeferedPlayDemo(char *demo);

void            G_LoadGame(char *name);

// can be called by the startup code or M_Responder
// calls P_SetupLevel or W_EnterWorld
void            G_DoLoadGame(void);

void            G_SaveGame(int slot, char *description);

void            G_DoReborn(int playernum);

void            G_StopDemo(void);
void            G_DemoEnds(void);
void            G_DemoAborted(void);

void            G_RecordDemo(skill_t skill, int numplayers, int episode,
							 int map, char *name);
// only called by startup code

void            G_PlayDemo(char *name);
void            G_TimeDemo(char *name);

void            G_ExitLevel(void);
void            G_SecretExitLevel(void);

void            G_WorldDone(void);

void            G_Ticker(void);
boolean         G_Responder(event_t *ev);

void            G_ScreenShot(void);

//-----
//PLAY
//-----

void            P_Ticker(void);

// called by C_Ticker
// can call G_PlayerExited
// carries out all thinking of monsters and players

void            P_SetupLevel(int episode, int map, int playermask,
							 skill_t skill);
// called by W_Ticker

void            P_Init(void);

// called by startup code

void            R_InitTranslationTables(void);

// called by startup code

void            R_SetViewSize(int blocks, int detail);

// called by M_Responder

//----
//MISC
//----
#define myargc	Argc()

void            strcatQuoted(char *dest, char *src);

boolean         M_ValidEpisodeMap(int episode, int map);

// returns true if the episode/map combo is valid for the current
// game configuration

void            M_ForceUppercase(char *text);

// Changes a string to uppercase

//int M_Random (void);
// returns a number from 0 to 255
int             P_Random(void);

// as M_Random, but used only by the play simulation

void            M_ClearRandom(void);

// fix randoms for demos

void            M_FindResponseFile(void);

void            M_ClearBox(fixed_t *box);
void            M_AddToBox(fixed_t *box, fixed_t x, fixed_t y);

// bounding box functions

int             M_DrawText(int x, int y, boolean direct, char *string);

//----------------------
// Interlude (IN_lude.c)
//----------------------

extern boolean  intermission;

void            IN_Start(void);
void            IN_Stop(void);
void            IN_Ticker(void);
void            IN_Drawer(void);

//----------------------
// Chat mode (CT_chat.c)
//----------------------

void            CT_Init(void);
void            CT_Drawer(void);
boolean         CT_Responder(event_t *ev);
void            CT_Ticker(void);
char            CT_dequeueChatChar(void);

extern boolean  chatmodeon;
extern boolean  ultimatemsg;

#if 0
//--------------------
// Finale (F_finale.c)
//--------------------

void            F_Drawer(void);
void            F_Ticker(void);
void            F_StartFinale(void);
#endif

//----------------------
// STATUS BAR (SB_bar.c)
//----------------------

void            SB_Init(void);
boolean         SB_Responder(event_t *event);
void            SB_Ticker(void);
void            SB_Drawer(void);
void            cht_GodFunc(player_t *player);
void            cht_NoClipFunc(player_t *player);

//-----------------
// MENU (MN_menu.c)
//-----------------

void            MN_Init(void);
void            MN_ActivateMenu(void);
void            MN_DeactivateMenu(void);
boolean         MN_Responder(event_t *event);
void            MN_Ticker(void);
void            MN_Drawer(void);
void            MN_DrTextA(char *text, int x, int y);
int             MN_TextAWidth(char *text);
void            MN_DrTextB(char *text, int x, int y);
int             MN_TextBWidth(char *text);
void            MN_TextFilter(char *text);
int             MN_FilterChar(int ch);

// Drawing text in the Current State.
void            MN_DrTextA_CS(char *text, int x, int y);
void            MN_DrTextAGreen_CS(char *text, int x, int y);
void            MN_DrTextB_CS(char *text, int x, int y);

extern byte     gammatable[5][256];
extern int      usegamma;

#include "Sounds.h"

#define IS_SERVER		Get(DD_SERVER)
#define IS_CLIENT		Get(DD_CLIENT)
#define IS_NETGAME		Get(DD_NETGAME)
#define IS_DEDICATED	Get(DD_DEDICATED)

#include "../Common/d_net.h"
#include "../Common/g_common.h"

#endif							// __DOOMDEF__
