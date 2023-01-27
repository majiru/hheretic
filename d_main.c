// D_main.c

// HEADER FILES ------------------------------------------------------------

#include "h2stdinc.h"
#if defined(__WATCOMC__) && defined(_DOS)
#include <dos.h>
#include <graph.h>
#include <direct.h>
#endif
#include "doomdef.h"
#include "p_local.h"
#include "soundst.h"
#include "v_compat.h"
#include "i_system.h"

// MACROS ------------------------------------------------------------------

#define MAXWADFILES		20
#define SHAREWAREWADNAME	"heretic1.wad"

#ifdef RENDER3D
#define V_DrawPatch(x,y,p)		OGL_DrawPatch((x),(y),(p))
#define V_DrawRawScreen(a)		OGL_DrawRawScreen((a))
#endif

// TYPES -------------------------------------------------------------------

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

void D_CheckNetGame(void);
void G_BuildTiccmd(ticcmd_t *cmd);
void F_Drawer(void);
boolean F_Responder(event_t *ev);
void R_ExecuteSetViewSize(void);

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

void D_ProcessEvents(void);
void D_DoAdvanceDemo(void);
void D_AdvanceDemo (void);
void D_PageDrawer (void);

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

extern boolean MenuActive;
extern boolean askforquit;

// PUBLIC DATA DEFINITIONS -------------------------------------------------

char *basePath = DUMMY_BASEPATH;
boolean shareware = false;		// true if only episode 1 present
boolean ExtendedWAD = false;	// true if episodes 4 and 5 present

boolean nomonsters;			// checkparm of -nomonsters
boolean respawnparm;			// checkparm of -respawn
boolean debugmode;			// checkparm of -debug
boolean ravpic;				// checkparm of -ravpic
boolean singletics;			// debug flag to cancel adaptiveness
boolean noartiskip;			// whether shift-enter skips an artifact

skill_t startskill;
int startepisode;
int startmap;
boolean autostart;
boolean advancedemo;
FILE *debugfile;
event_t events[MAXEVENTS];
int eventhead;
int eventtail;

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static int demosequence;
static int pagetic;
static const char *pagename;
static char basedefault[1024];

static const char *wadfiles[MAXWADFILES + 1] =
{
	"heretic.wad",
	NULL	/* the last entry MUST be NULL */
};

// CODE --------------------------------------------------------------------

#if !(defined(__WATCOMC__) || defined(__DJGPP__) || defined(_WIN32))
char *strlwr (char *str)
{
	char	*c;
	c = str;
	while (*c)
	{
		*c = tolower(*c);
		c++;
	}
	return str;
}

char *strupr (char *str)
{
	char	*c;
	c = str;
	while (*c)
	{
		*c = toupper(*c);
		c++;
	}
	return str;
}

int filelength(int handle)
{
	Dir *d;
	int length;

	d = dirfstat(handle);
	if (d == nil)
	{
		I_Error("Error fstating");
	}
	length = d->length;
	free(d);
	return length;
}
#endif

//==========================================================================
//
// Fixed Point math
//
//==========================================================================

#if defined(_HAVE_FIXED_ASM)

#if defined(__i386__) || defined(__386__) || defined(_M_IX86)
#if defined(__GNUC__) && !defined(_INLINE_FIXED_ASM)
fixed_t	FixedMul (fixed_t a, fixed_t b)
{
	fixed_t retval;
	__asm__ __volatile__(
		"imull  %%edx			\n\t"
		"shrdl  $16, %%edx, %%eax	\n\t"
		: "=a" (retval)
		: "a" (a), "d" (b)
		: "cc"
	);
	return retval;
}

fixed_t	FixedDiv2 (fixed_t a, fixed_t b)
{
	fixed_t retval;
	__asm__ __volatile__(
		"cdq				\n\t"
		"shldl  $16, %%eax, %%edx	\n\t"
		"sall   $16, %%eax		\n\t"
		"idivl  %%ebx			\n\t"
		: "=a" (retval)
		: "a" (a), "b" (b)
		: "%edx", "cc"
	);
	return retval;
}
#endif	/* GCC and !_INLINE_FIXED_ASM */
#endif	/* x86 */

#else	/* C-only versions */

fixed_t FixedMul (fixed_t a, fixed_t b)
{
	return ((int64_t) a * (int64_t) b) >> 16;
}

fixed_t FixedDiv2 (fixed_t a, fixed_t b)
{
	if (!b)
		return 0;
	return (fixed_t) (((double) a / (double) b) * FRACUNIT);
}
#endif

fixed_t FixedDiv (fixed_t a, fixed_t b)
{
	if ((abs(a) >> 14) >= abs(b))
	{
		return ((a^b) < 0 ? H2MININT : H2MAXINT);
	}
	return (FixedDiv2(a, b));
}

//==========================================================================
//
// Byte swap functions
//
//==========================================================================

int16_t ShortSwap (int16_t x)
{
	return (int16_t) (((uint16_t)x << 8) | ((uint16_t)x >> 8));
}

int32_t LongSwap (int32_t x)
{
	return (int32_t) (((uint32_t)x << 24) | ((uint32_t)x >> 24) |
			  (((uint32_t)x & (uint32_t)0x0000ff00UL) << 8) |
			  (((uint32_t)x & (uint32_t)0x00ff0000UL) >> 8));
}

/*
===============================================================================

							EVENT HANDLING

Events are asyncronous inputs generally generated by the game user.

Events can be discarded if no responder claims them

===============================================================================
*/

//---------------------------------------------------------------------------
//
// PROC D_PostEvent
//
// Called by the I/O functions when input is detected.
//
//---------------------------------------------------------------------------

QLock		eventlock;


//
// D_PostEvent
// Called by the I/O functions when input is detected
//
void D_PostEvent (event_t* ev)
{
    int next;

retry:
    qlock(&eventlock);
    next = (eventhead+1)&(MAXEVENTS-1);
    if(next == eventtail){
        qunlock(&eventlock);
        if(ev->type != ev_keydown && ev->type != ev_keyup)
            return;
        sleep(1);
        goto retry;
    }
    events[eventhead] = *ev;
    eventhead = next;
    qunlock(&eventlock);
}

//---------------------------------------------------------------------------
//
// PROC D_ProcessEvents
//
// Send all the events of the given timestamp down the responder chain.
//
//---------------------------------------------------------------------------

void D_ProcessEvents(void)
{
	event_t *ev;

	while (eventtail != eventhead)
	{
		ev = &events[eventtail];
		if (F_Responder(ev))
		{
			goto _next_ev;
		}
		if (MN_Responder(ev))
		{
			goto _next_ev;
		}
		G_Responder(ev);
	_next_ev:
		eventtail = (eventtail + 1) & (MAXEVENTS - 1);
	}
}

//---------------------------------------------------------------------------
//
// PROC DrawMessage
//
//---------------------------------------------------------------------------

void DrawMessage(void)
{
	player_t *player;

	player = &players[consoleplayer];
	if (player->messageTics <= 0 || !player->message)
	{ // No message
		return;
	}
	MN_DrTextA(player->message, 160 - MN_TextAWidth(player->message)/2, 1);
}

//---------------------------------------------------------------------------
//
// PROC D_Display
//
// Draw current display, possibly wiping it from the previous.
//
//---------------------------------------------------------------------------

void D_Display(void)
{
	// Change the view size if needed
	if (setsizeneeded)
	{
		R_ExecuteSetViewSize();
	}

//
// do buffered drawing
//
	switch (gamestate)
	{
	case GS_LEVEL:
		if (!gametic)
			break;
#if  AM_TRANSPARENT
		R_RenderPlayerView (&players[displayplayer]);
#endif
		if (automapactive)
			AM_Drawer ();
#if !AM_TRANSPARENT
		else
		{
			R_RenderPlayerView (&players[displayplayer]);
		}
#endif
		CT_Drawer();
		UpdateState |= I_FULLVIEW;
		SB_Drawer();
		break;
	case GS_INTERMISSION:
		IN_Drawer ();
		break;
	case GS_FINALE:
		F_Drawer ();
		break;
	case GS_DEMOSCREEN:
		D_PageDrawer ();
		break;
	}

	if (paused && !MenuActive && !askforquit)
	{
		if (!netgame)
		{
			V_DrawPatch(160, viewwindowy + 5, (PATCH_REF)WR_CacheLumpName("PAUSED", PU_CACHE));
		}
		else
		{
			V_DrawPatch(160, 70, (PATCH_REF)WR_CacheLumpName("PAUSED", PU_CACHE));
		}
	}

#ifdef RENDER3D
	if (OGL_DrawFilter())
		BorderNeedRefresh = true;
#endif

	// Handle player messages
	DrawMessage();

	// Menu drawing
	MN_Drawer();

	// Send out any new accumulation
	NetUpdate();

	// Flush buffered stuff to screen
	I_Update();
}

//---------------------------------------------------------------------------
//
// PROC D_DoomLoop
//
//---------------------------------------------------------------------------

void D_DoomLoop(void)
{
	if (M_CheckParm("-debugfile"))
	{
		char filename[20];
		snprintf(filename, sizeof(filename), "debug%i.txt", consoleplayer);
		debugfile = fopen(filename,"w");
	}
	I_InitGraphics();
	while (1)
	{
		// Frame syncronous IO operations
		I_StartFrame();

		// Process one or more tics
		if (singletics)
		{
			I_StartTic();
			D_ProcessEvents();
			G_BuildTiccmd(&netcmds[consoleplayer][maketic%BACKUPTICS]);
			if (advancedemo)
				D_DoAdvanceDemo ();
			G_Ticker();
			gametic++;
			maketic++;
		}
		else
		{
			// Will run at least one tic
			TryRunTics();
		}

		// Move positional sounds
		S_UpdateSounds(players[consoleplayer].mo);
		D_Display();
	}
}

/*
===============================================================================

						DEMO LOOP

===============================================================================
*/

/*
================
=
= D_PageTicker
=
= Handles timing for warped projection
=
================
*/

void D_PageTicker (void)
{
	if (--pagetic < 0)
		D_AdvanceDemo ();
}


/*
================
=
= D_PageDrawer
=
================
*/

extern boolean MenuActive;

void D_PageDrawer(void)
{
	V_DrawRawScreen((BYTE_REF)WR_CacheLumpName(pagename, PU_CACHE));
	if (demosequence == 1)
	{
		V_DrawPatch(4, 160, (PATCH_REF)WR_CacheLumpName("ADVISOR", PU_CACHE));
	}
	UpdateState |= I_FULLSCRN;
}

/*
=================
=
= D_AdvanceDemo
=
= Called after each demo or intro demosequence finishes
=================
*/

void D_AdvanceDemo (void)
{
	advancedemo = true;
}

void D_DoAdvanceDemo (void)
{
	players[consoleplayer].playerstate = PST_LIVE; // don't reborn
	advancedemo = false;
	usergame = false; // can't save / end game here
	paused = false;
	gameaction = ga_nothing;
	demosequence = (demosequence + 1) % 7;
	switch (demosequence)
	{
	case 0:
		pagetic = 210;
		gamestate = GS_DEMOSCREEN;
		pagename = "TITLE";
		S_StartSong(mus_titl, false);
		break;
	case 1:
		pagetic = 140;
		gamestate = GS_DEMOSCREEN;
		pagename = "TITLE";
		break;
	case 2:
		BorderNeedRefresh = true;
		UpdateState |= I_FULLSCRN;
		G_DeferedPlayDemo ("demo1");
		break;
	case 3:
		pagetic = 200;
		gamestate = GS_DEMOSCREEN;
		pagename = "CREDIT";
		break;
	case 4:
		BorderNeedRefresh = true;
		UpdateState |= I_FULLSCRN;
		G_DeferedPlayDemo ("demo2");
		break;
	case 5:
		pagetic = 200;
		gamestate = GS_DEMOSCREEN;
		if (shareware)
		{
			pagename = "ORDER";
		}
		else
		{
			pagename = "CREDIT";
		}
		break;
	case 6:
		BorderNeedRefresh = true;
		UpdateState |= I_FULLSCRN;
		G_DeferedPlayDemo ("demo3");
		break;
	}
}


/*
=================
=
= D_StartTitle
=
=================
*/

void D_StartTitle (void)
{
	gameaction = ga_nothing;
	demosequence = -1;
	D_AdvanceDemo ();
}


/*
==============
=
= D_CheckRecordFrom
=
= -recordfrom <savegame num> <demoname>
==============
*/

void D_CheckRecordFrom (void)
{
	int p;

	p = M_CheckParm ("-recordfrom");
	if (!p || p >= myargc - 2)
		return;
	G_LoadGame(atoi(myargv[p + 1]));
	G_DoLoadGame(); // load the gameskill etc info from savegame
	G_RecordDemo(gameskill, 1, gameepisode, gamemap, myargv[p + 2]);
	D_DoomLoop(); // never returns
}

/*
===============
=
= D_AddFile
=
===============
*/

void D_AddFile(const char *file)
{
	int i = 0;
	char *newwad;

	while (wadfiles[i])
	{
		if (++i == MAXWADFILES)
			I_Error ("MAXWADFILES reached for %s", file);
	}
	newwad = (char *) malloc(strlen(file) + 1);
	strcpy(newwad, file);
	wadfiles[i] = newwad;
	if (++i <= MAXWADFILES)
		wadfiles[i] = NULL;
}

//==========================================================
//
//  Startup Thermo code
//  FIXME : MOVE OR REMOVE THIS...
//==========================================================
#if defined(__WATCOMC__) && defined(_DOS)

#define MSG_Y		9
/*
#define THERM_X		15
#define THERM_Y		16
#define THERMCOLOR	3
*/
#define THERM_X		14
#define THERM_Y		14

static int thermMax;
static int thermCurrent;

//
//  Heretic startup screen shit
//
static byte *hscreen;
static char *startup;	// * to text screen
static char smsg[80];	// status bar line
static char tmsg[300];

static void hgotoxy(int x,int y)
{
	hscreen = (byte *)(0xb8000 + y*160 + x*2);
}

static void hput(unsigned char c, unsigned char a)
{
	*hscreen++ = c;
	*hscreen++ = a;
}

static void hprintf(const char *string, unsigned char a)
{
	int i;

	if (debugmode)
	{
		puts(string);
		return;
	}
	for (i = 0; i < strlen(string); i++)
	{
		hput(string[i], a);
	}
}

static void drawstatus(void)
{
	if(debugmode)
	{
		return;
	}
	_settextposition(25, 2);
	_setbkcolor(1);
	_settextcolor(15);
	_outtext(smsg);
	_settextposition(25, 1);
}

static void status(const char *string)
{
	strcat(smsg, string);
	drawstatus();
}

static void DrawThermo(void)
{
	unsigned char *screen;
	int progress;
	int i;

	if (debugmode)
	{
		return;
	}
#if 0
	progress = (98*thermCurrent)/thermMax;
	screen = (char *)0xb8000 + (THERM_Y*160 + THERM_X*2);
	for (i = 0; i < progress/2; i++)
	{
		switch(i)
		{
		case 4:
		case 9:
		case 14:
		case 19:
		case 29:
		case 34:
		case 39:
		case 44:
			*screen++ = 0xb3;
			*screen++ = (THERMCOLOR<<4)+15;
			break;
		case 24:
			*screen++ = 0xba;
			*screen++ = (THERMCOLOR<<4)+15;
			break;
		default:
			*screen++ = 0xdb;
			*screen++ = 0x40 + THERMCOLOR;
			break;
		}
	}
	if (progress & 1)
	{
		*screen++ = 0xdd;
		*screen++ = 0x40 + THERMCOLOR;
	}
#else
	progress = (50*thermCurrent)/thermMax + 2;
//	screen = (char *)0xb8000 + (THERM_Y*160 + THERM_X*2);
	hgotoxy(THERM_X,THERM_Y);
	for (i = 0; i < progress; i++)
	{
//		*screen++ = 0xdb;
//		*screen++ = 0x2a;
		hput(0xdb, 0x2a);
	}
#endif
}

static void blitStartup(void)
{
	byte *textScreen;

	if(debugmode)
	{
		return;
	}

	// Blit main screen
	textScreen = (byte *)0xb8000;
	memcpy(textScreen, startup, 4000);

	// Print version string
	_setbkcolor(4); // Red
	_settextcolor(14); // Yellow
	_settextposition(3, 47);
	_outtext(VERSION_TEXT);

	// Hide cursor
	_settextcursor(0x2000);
}

void tprintf(const char *msg, int initflag)
{
# if 0
	char temp[80];
	int i, start, add;

	if (debugmode)
	{
		printf(msg);
		return;
	}
	if (initflag)
		tmsg[0] = 0;
	strcat(tmsg,msg);
	blitStartup();
	DrawThermo();
	_setbkcolor(4);
	_settextcolor(15);
	for (add = start = i = 0; i <= strlen(tmsg); i++)
	{
		if ((tmsg[i] == '\n') || (!tmsg[i]))
		{
			memset(temp,0,80);
			strncpy(temp,tmsg+start,i-start);
			_settextposition(MSG_Y+add,40-strlen(temp)/2);
			_outtext(temp);
			start = i+1;
			add++;
		}
	}
	_settextposition(25,1);
	drawstatus();
# endif
}

void CheckAbortStartup(void)
{
#if defined(__WATCOMC__) && defined(_DOS)
	extern int lastpress;
	if (lastpress == 1)
	{ // Abort if escape pressed
		union REGS regs;
		I_ShutdownKeyboard();
		regs.x.eax = 0x3;
		int386(0x10, &regs, &regs);
		printf("Exited from HERETIC.\n");
		exit(1);
	}
#endif
}

void IncThermo(void)
{
	thermCurrent++;
	DrawThermo();
	CheckAbortStartup();
}

void InitThermo(int max)
{
	thermMax = max;
	thermCurrent = 0;
}

#else

#define hgotoxy(x,y)	do {} while (0)
#define hprintf(s,a)	puts((s))
#define status(s)	puts((s))
#define DrawThermo()	do {} while (0)
void tprintf(const char *msg, int initflag)
{
# if 0
	printf(msg);
# endif
}
void CheckAbortStartup(void) {}
void IncThermo(void) {}
void InitThermo(int x) {}
#endif

//---------------------------------------------------------------------------
//
// PROC D_DoomMain
//
//---------------------------------------------------------------------------

void D_DoomMain(void)
{
	int p, e, m, i;
	char file[MAX_OSPATH];
	boolean devMap;
	char *slash;
	char *wadloc[nelem(wadfiles)];

	M_FindResponseFile();
	setbuf(stdout, NULL);
	nomonsters = M_CheckParm("-nomonsters");
	respawnparm = M_CheckParm("-respawn");
	ravpic = M_CheckParm("-ravpic");
	noartiskip = M_CheckParm("-noartiskip");
	debugmode = M_CheckParm("-debug");
	startskill = sk_medium;
	startepisode = 1;
	startmap = 1;
	autostart = false;

	// -FILE [filename] [filename] ...
	// Add files to the wad list.
	p = M_CheckParm("-file");
	if (p)
	{	// the parms after p are wadfile/lump names, until end of parms
		// or another - preceded parm
		while (++p != myargc && myargv[p][0] != '-')
		{
			D_AddFile(myargv[p]);
		}
	}

	// -DEVMAP <episode> <map>
	// Adds a map wad from the development directory to the wad list,
	// and sets the start episode and the start map.
	devMap = false;
	p = M_CheckParm("-devmap");
	if (p && p < myargc-2)
	{
		e = myargv[p+1][0];
		m = myargv[p+2][0];
		snprintf(file, sizeof(file), "%se%cm%c.wad", DEVMAPDIR, e, m);
		D_AddFile(file);
		printf("DEVMAP: Episode %c, Map %c.\n", e, m);
		startepisode = e-'0';
		startmap = m-'0';
		autostart = true;
		devMap = true;
	}

	p = M_CheckParm("-playdemo");
	if (!p)
	{
		p = M_CheckParm("-timedemo");
	}
	if (p && p < myargc-1)
	{
		snprintf(file, sizeof(file), "%s.lmp", myargv[p+1]);
		D_AddFile(file);
		printf("Playing demo %s.lmp.\n", myargv[p+1]);
	}

//
// get skill / episode / map from parms
//
	if (M_CheckParm("-deathmatch"))
	{
		deathmatch = true;
	}

	p = M_CheckParm("-skill");
	if (p && p < myargc-1)
	{
		startskill = myargv[p+1][0]-'1';
		autostart = true;
	}

	p = M_CheckParm("-episode");
	if (p && p < myargc-1)
	{
		startepisode = myargv[p+1][0]-'0';
		startmap = 1;
		autostart = true;
	}

	p = M_CheckParm("-warp");
	if (p && p < myargc-2)
	{
		startepisode = myargv[p+1][0]-'0';
		startmap = myargv[p+2][0]-'0';
		autostart = true;
	}

//
// init subsystems
//
	printf("V_Init: allocate screens.\n");
	V_Init();

	printf("Z_Init: Init zone memory allocation daemon.\n");
	Z_Init();

	printf("W_Init: Init WADfiles.\n");
	I_SetupPath(wadfiles);
	W_InitMultipleFiles(wadfiles);

	if (W_CheckNumForName("E2M1") == -1)
	{ // Can't find episode 2 maps, must be the shareware WAD
		shareware = true;
	}
	else if (W_CheckNumForName("EXTENDED") != -1)
	{ // Found extended lump, must be the extended WAD
		ExtendedWAD = true;
	}

	// Load defaults before initing other systems
	printf("M_LoadDefaults: Load system defaults.\n");
	M_LoadDefaults("cfg");

#if defined(__WATCOMC__) && defined(_DOS)
	I_StartupKeyboard();
	I_StartupJoystick();
	startup = (char *) W_CacheLumpName("LOADING", PU_CACHE);
	blitStartup();

	//  Build status bar line!
	smsg[0] = 0;
#endif
	if (deathmatch)
		status("DeathMatch...");
	if (nomonsters)
		status("No Monsters...");
	if (respawnparm)
		status("Respawning...");
	if (autostart)
	{
		char temp[64];
		snprintf(temp, sizeof(temp), "Warp to Episode %d, Map %d, Skill %d ",
					startepisode, startmap, startskill + 1);
		status(temp);
	}

	tprintf("MN_Init: Init menu system.\n",1);
	MN_Init();

	CT_Init();

	tprintf("R_Init: Init Heretic refresh daemon.",1);
	hgotoxy(17,7);
	hprintf("Loading graphics",0x3f);
	R_Init();

	tprintf("P_Init: Init Playloop state.",1);
	hgotoxy(17,8);
	hprintf("Init game engine.",0x3f);
	P_Init();
	IncThermo();

	tprintf("I_Init: Setting up machine state.\n",1);
	I_Init();
	IncThermo();

	tprintf("D_CheckNetGame: Checking network game status.\n",1);
	hgotoxy(17,9);
	hprintf("Checking network game status.", 0x3f);
	D_CheckNetGame();
	IncThermo();

#if defined(__WATCOMC__) && defined(_DOS)
	I_CheckExternDriver(); // Check for an external device driver
#endif

	tprintf("SB_Init: Loading patches.\n",1);
	SB_Init();
	IncThermo();

//
// start the apropriate game based on parms
//

	D_CheckRecordFrom();

	p = M_CheckParm("-record");
	if (p && p < myargc-1)
	{
		G_RecordDemo(startskill, 1, startepisode, startmap, myargv[p+1]);
		D_DoomLoop(); // Never returns
	}

	p = M_CheckParm("-playdemo");
	if (p && p < myargc-1)
	{
		singledemo = true; // Quit after one demo
		G_DeferedPlayDemo(myargv[p+1]);
		D_DoomLoop(); // Never returns
	}

	p = M_CheckParm("-timedemo");
	if (p && p < myargc-1)
	{
		G_TimeDemo(myargv[p+1]);
		D_DoomLoop(); // Never returns
	}

	p = M_CheckParm("-loadgame");
	if (p && p < myargc-1)
	{
		G_LoadGame(atoi(myargv[p+1]));
	}

	// Check valid episode and map
	if ((autostart || netgame) && (devMap == false))
	{
		if (M_ValidEpisodeMap(startepisode, startmap) == false)
		{
			startepisode = 1;
			startmap = 1;
		}
	}

	if (gameaction != ga_loadgame)
	{
		UpdateState |= I_FULLSCRN;
		BorderNeedRefresh = true;
		if (autostart || netgame)
		{
			G_InitNew(startskill, startepisode, startmap);
		}
		else
		{
			D_StartTitle();
		}
	}
#if defined(__WATCOMC__) && defined(_DOS)
	_settextcursor(0x0607); // bring the cursor back
#endif
	D_DoomLoop(); // Never returns
}

