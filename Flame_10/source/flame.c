/*
	flame.c
	Flame saver module C source file version 1.0   19 / 2 / 94
	(C) 1994 Toniolo Emanuele

*/
                                                                  
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_DOSDATETIME
#define INCL_WIN
#define INCL_GPI
#define INCL_PM
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <process.h>
#include <math.h>

#include "flame.h"

// ===== preprocessor definitions

#define MODULEVERSION		   0x00010001
#define STACKSIZE		         128000
#define SAVER_NAME_MAXLEN	   32
#define FUNCTION_CONFIGURE	   1
#define FUNCTION_STARTSAVER	2
#define FUNCTION_STOPSAVER	   3
#define FUNCTION_QUERYNAME	   4
#define FUNCTION_QUERYENABLED	5
#define FUNCTION_SETENABLED	6

#define CONFIGURATION_MINIMUM_COUNT  	5
#define CONFIGURATION_DEFAULT_COUNT  	10
#define CONFIGURATION_MAXIMUM_COUNT  	30
#define MAXRAND                         32000

// ===== prototypes

void	EXPENTRY SAVER_PROC(int function, HAB _hab, HWND hwndOwner, char *appname, void *buffer);
static	MRESULT EXPENTRY SaverWindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
static	MRESULT	EXPENTRY ConfigureDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
#if defined(__IBMC__)
static	void	_Optlink draw_thread(void *args);
static	void	_Optlink priority_thread(void *args);
#elif defined(__BORLANDC__)
static	void	_USERENTRY draw_thread(void *args);
static	void	_USERENTRY priority_thread(void *args);
#else
static	void	draw_thread(void *args);
static	void	priority_thread(void *args);
#endif
static	void	load_configuration_data(void);


// ===== global data

static	LONG	   screenSizeX = 0;             // screen size x
static	LONG	   screenSizeY = 0;             // screen size y
static	HWND	   hwndSaver = NULLHANDLE;	     // saver window handle
static	HMODULE	hmodDLL = NULLHANDLE;        // saver module dll handle
static	char	   *application_name;	   	  // name of ScreenSaver app
static	TID	   tidDraw;			              // drawing-thread ID
static	HPS	   hps;				              // presentation space handle
static	HAB	   hab;			            	  // anchor block handle
static	BOOL	   low_priority = TRUE;		     // low-priority flag
static	BOOL	   configuration_data_loaded = FALSE;  // config data loaded flag
static	volatile BOOL stop_draw_thread;	     // stop flag
static	char	   modulename[SAVER_NAME_MAXLEN+1];    // module name buffer

static	struct	_configuration_data {
	ULONG	   version;
	BOOL	   enabled;
   int      count;
} configuration_data;

#define MAXSCREENS   	1
#define MAXTOTAL	30000
#define MAXBATCH	50
#define MAXLEV		4

typedef struct {
    double      f[2][3][MAXLEV];/* three non-homogeneous transforms */
    int         max_levels;
    int         cur_level;
    int         snum;
    int         anum;
    int         width, height;
    int         num_points;
    int         total_points;
    POINTL      pts[MAXBATCH * 2 + 1];
} flamestruct;

static flamestruct flames;

BOOL Recur (HPS hps , flamestruct *fs,double x,double y,long l);
static long halfrandom(long mv);

// ===== code

/*
	SAVER_PROC
	This is the entry point into the saver module that is called by
	the ScreenSaver program.
	There should be no reason to alter the code.
	Depending on the requested function, the following tasks
	are performed:
	* call the configuration dialog of the saver module
	* copy the name of the saver module into the supplied buffer
	* tell if the saver module is enabled
	* set the "enabled" state of the saver module
	* start the saver
	* stop the saver
	Note that before any processing is done, module configuration data is
	loaded from the INI-files.
*/
void	EXPENTRY SAVER_PROC(int function, HAB _hab, HWND hwndOwner, char *appname, void *buffer)
{
#if defined(__BORLANDC__)
	extern ULONG _os2hmod;
	hmodDLL = _os2hmod;
#endif	
	hab = _hab;
	application_name = appname;
	// load all configuration data from INI-file
	load_configuration_data();
	switch(function){
	case FUNCTION_CONFIGURE:
		// call the configuration dialog
		WinDlgBox(HWND_DESKTOP, hwndOwner, ConfigureDlgProc,
		  hmodDLL, IDD_CONFIGURE, application_name);
		return;
	case FUNCTION_STARTSAVER:
		// start the saver
		// get "low priority" state from supplied buffer (BOOL *)
		low_priority = *((BOOL *)buffer);
		// random seed
		srand(WinGetCurrentTime(hab));
		// query size of screen
		screenSizeX = WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN);
		screenSizeY = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
		// register window class for the saver window
		WinRegisterClass(hab, modulename,
		  (PFNWP)SaverWindowProc, 0, 0);
		// create the saver window
		hwndSaver = WinCreateWindow(HWND_DESKTOP, modulename,
		  (PSZ)NULL, WS_VISIBLE, 0, 0, screenSizeX, screenSizeY,
		  HWND_DESKTOP, HWND_TOP, 0, NULL, NULL);
		return;
	case FUNCTION_STOPSAVER:
		// stop the saver
		if(hwndSaver != NULLHANDLE){
			// move saver window to front
			WinSetWindowPos(hwndSaver, HWND_TOP,
			  0, 0, 0, 0, SWP_ZORDER);
			// destroy saver window
			WinDestroyWindow(hwndSaver);
			hwndSaver = NULLHANDLE;
		}
		return;
	case FUNCTION_QUERYNAME:
		// copy module name to supplied buffer (CHAR *)
		strcpy(buffer, modulename);
		return;
	case FUNCTION_QUERYENABLED:
		// copy "enabled" state to supplied buffer (BOOL *)
		*((BOOL *)buffer) = configuration_data.enabled;
		return;
	case FUNCTION_SETENABLED:
		// get new "enabled" state from supplied buffer (BOOL *)
		configuration_data.enabled = *((BOOL *)buffer);
		PrfWriteProfileData(HINI_USER, (PSZ)application_name, (PSZ)modulename, (PSZ)&configuration_data, sizeof(configuration_data));
		return;
	}

	// illegal function request
	WinAlarm(HWND_DESKTOP, WA_ERROR);
	return;
}

#if !defined(__BORLANDC__)
/*
	_DLL_InitTerm
	This procedure is called at DLL initialization and termination.
	There should be no reason to alter the code.
*/
ULONG	_DLL_InitTerm(HMODULE hmod, ULONG flag)
{
	switch(flag){
	case 0:	// initializing DLL
		hmodDLL = hmod;
		return 1;
	case 1: // terminating DLL
		return 1;
	default:
		// return error
		return 0;
	}
}
#endif

/*
	ConfigureDlgProc
	This is the dialog procedure for the module configuration dialog.
	The dialog contains a check box for enabling/disabling the module
	and two push buttons ("OK" and "Cancel") to close/cancel the dialog.
	This is enough for simple saver modules, but can easily be expanded
	for more saver modules that need more settings.
*/
MRESULT	EXPENTRY ConfigureDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
	char	buf[sizeof(modulename)+20];
	static	HWND	hwndEnabled;
	static	HWND	hwndCount;

	switch(msg){
	case WM_INITDLG:
		// set titlebar of the dialog window
		// to "MODULENAME configuration"
		strcpy(buf, modulename);
		strcat(buf, " configuration");
		WinSetWindowText(hwnd, buf);

		// get  window handles of the dialog controls
		// and set initial state of the controls
		hwndEnabled = WinWindowFromID(hwnd, IDC_ENABLED);
		WinSendMsg(hwndEnabled, BM_SETCHECK,MPFROMSHORT(configuration_data.enabled), MPVOID);

		hwndCount = WinWindowFromID(hwnd, IDC_COUNT);
		WinSendMsg(hwndCount, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_COUNT, (MPARAM)CONFIGURATION_MINIMUM_COUNT);
		WinSendMsg(hwndCount, SPBM_SETCURRENTVALUE, MPFROMSHORT(configuration_data.count), MPVOID);

		// return FALSE since we did not change the focus		
      		return (MRESULT)FALSE;


	case WM_COMMAND:
		switch(SHORT1FROMMP(mp1)){
		case IDC_OK:
			// OK button was pressed. query the control settings
			configuration_data.enabled = SHORT1FROMMR(WinSendMsg(hwndEnabled, BM_QUERYCHECK, MPVOID, MPVOID));

         WinSendMsg(hwndCount, SPBM_QUERYVALUE, MPFROMP(&configuration_data.count), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));

			// write all configuration data to INI-file
			PrfWriteProfileData(HINI_USER, (PSZ)application_name, (PSZ)modulename, (PSZ)&configuration_data, sizeof(configuration_data));
			// end dialog
			WinDismissDlg(hwnd, TRUE);
			return (MRESULT)0;

		case IDC_CANCEL:
			// dialog was cancelled; end it
			WinDismissDlg(hwnd, FALSE);
			return (MRESULT)0;

		default:
			return (MRESULT)0;
		}
	}
	return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

/*
	SaverWindowProc
	This is the window procedure of the screen-size window that is
	created when the saver starts.
	There should be no reason to alter the code.
	Note that we do not process WM_PAINT messages. They are forwarded to
	the default window procedure, which just validates the window area
	and does no drawing. All drawing to the window should be done in
	the drawing-thread. Therefore, if you want to blank the screen before
	drawing on it for instance, issue a WinFillRect call at the beginning
	of your drawing-thread.
*/
MRESULT EXPENTRY SaverWindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
	static	TID	tidPriority;

	switch(msg){
	case WM_CREATE:
		// reset the "stop" flag
		stop_draw_thread = FALSE;
		// store window handle
		hwndSaver = hwnd;
		// get presentation space
		hps = WinGetPS(hwnd);
		// start the drawing-thread
/*
		$$$$$ note $$$$$
		Some compilers use another parameter ordering for
		_beginthread. The _beginthread call below works with EMX,
		ICC and BCC. Check your compiler docs for other compilers.
*/
// !!!!! code for Borland C++ added since version 1.0
#if defined(__BORLANDC__)
		// for Borland C++
		tidDraw = _beginthread(draw_thread, STACKSIZE, NULL);
#elif defined(__EMX__) || defined(__IBMC__)
		// for EMX and ICC
		tidDraw = _beginthread(draw_thread, NULL, STACKSIZE, NULL);
#endif

		// !!!!! next 3 lines added since version 1.0, some code deleted
		// create thread to control priority of drawing thread
		if(low_priority)
			DosCreateThread(&tidPriority, (PFNTHREAD)priority_thread, 0, 2L, 1000);
		return (MRESULT)FALSE;

	case WM_DESTROY:
		if(low_priority)
			DosKillThread(tidPriority);
		// tell drawing-thread to stop
		stop_draw_thread = TRUE;
		if(DosWaitThread(&tidDraw, DCWW_NOWAIT) == ERROR_THREAD_NOT_TERMINATED){
			// if priority of drawing-thread was set to idle time
			// priority, set it back to normal value
			DosSetPriority(PRTYS_THREAD, PRTYC_REGULAR, PRTYD_MAXIMUM, tidDraw);
			// wait until drawing-thread has ended
			DosWaitThread(&tidDraw, DCWW_WAIT);
		}
		// release the presentation space
		WinReleasePS(hps);
		break;
	case WM_PAINT:
		return WinDefWindowProc(hwnd, msg, mp1, mp2);
		if(0){
			// just validate the update area. all drawing is done
			// in the drawing-thread.
			RECTL	rclUpdate;
			HPS	hps = WinBeginPaint(hwnd, NULLHANDLE, &rclUpdate);
			WinEndPaint(hps);
		}
		return (MRESULT)0;
	}
	return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

/*
	priority_thread
	This thread controls the priority of the drawing thread.
	With these changes, if a saver module runs on low priority (this is
	the default setting), it rises to normal priority twice a second
	for 0.1 seconds. This should solve the problem that, when very
	time-consuming processes were running, the module seemed not to become
	active at all (in fact it became active, but did not get enough CPU
	time to do its saver action).
	There should be no reason to alter the code.
*/
void	priority_thread(void *args)
{
	DosSetPriority(PRTYS_THREAD, PRTYC_TIMECRITICAL, 0, 0);
	for(;;){
		DosSetPriority(PRTYS_THREAD, PRTYC_REGULAR, 0, tidDraw);
		DosSleep(100);
		DosSetPriority(PRTYS_THREAD, PRTYC_IDLETIME, 0, tidDraw);
		DosSleep(400);
	}
}


/*
	load_configuration_data
	Load all module configuration data from the INI-file into the
	configuration_data structure, if not done already loaded.
*/
void	load_configuration_data()
{
	if(configuration_data_loaded == FALSE){
		// data not loaded yet
		ULONG	size;
		BOOL	fSuccess;

		// get name of the saver module (stored as resource string)
		if(WinLoadString(hab, hmodDLL, IDS_MODULENAME,
		  SAVER_NAME_MAXLEN, modulename) == 0){
			// resource string not found. indicate error by
			// setting module name to empty string
			strcpy(modulename, "");
			return;
		}

		// load data from INI-file. the key name is the name of the
		// saver module
		size = sizeof(configuration_data);
		fSuccess = PrfQueryProfileData(HINI_USER,
		  (PSZ)application_name, (PSZ)modulename,
		  (PSZ)&configuration_data, &size);
		if(!fSuccess || size != sizeof(configuration_data) || configuration_data.version != MODULEVERSION){
			// if entry is not found or entry has invalid size or
			// entry has wrong version number, create a new entry
			// with default values and write it to the INI-file
			
         		configuration_data.version   = MODULEVERSION;
			configuration_data.enabled   = TRUE;
			configuration_data.count     = CONFIGURATION_DEFAULT_COUNT;

			PrfWriteProfileData(HINI_USER, (PSZ)application_name, (PSZ)modulename, (PSZ)&configuration_data, sizeof(configuration_data));
		}
		configuration_data_loaded = TRUE;
	}
}

static long halfrandom(long mv)
{
    static short lasthalf = 0;
    unsigned long r;

    if (lasthalf)
      {
	   r = lasthalf;
	   lasthalf = 0;
      }
   else
      {
	   r = rand();
	   lasthalf = r >> 16;
      }
   return r % mv;
}

BOOL Recur (HPS hps , flamestruct *fs,double x,double y,long l)
{
    long        xp, yp, i , index;
    double      nx, ny;
    POINTL      ptl;

    if (l == fs->max_levels)
      {
	   fs->total_points++;
	   if (fs->total_points > MAXTOTAL)	/* how long each fractal runs */
	      return FALSE;

	   if (x > -1.0 && x < 1.0 && y > -1.0 && y < 1.0)
         {
	      ptl.x = (( fs->width ) * (x + 1.0));
	      ptl.y = (( fs->height ) * (y + 1.0));
         index = fs -> num_points * 2;
         fs -> pts [ index ] = fs -> pts [ index + 1 ] = ptl;
	      fs->num_points++;
	      if (fs->num_points > MAXBATCH)
            {
            /* point buffer size */
            GpiPolyLineDisjoint ( hps , fs -> num_points * 2 , fs -> pts );
		      fs->num_points = 0;
	         }
      	}
      }
   else
      {
	   for (i = 0; i < fs -> snum ; i++)
         {
         nx = fs->f[0][0][i] * x + fs->f[0][1][i] * y + fs->f[0][2][i];
	      ny = fs->f[1][0][i] * x + fs->f[1][1][i] * y + fs->f[1][2][i];
	      if (i < fs -> anum)
            {
            nx = sin(nx);
		      ny = sin(ny);
	         }
	      if (!Recur(hps , fs, nx, ny, l + 1))
		      return FALSE;
	      }
      }
   return TRUE;
}

/*
	draw_thread
	This is the drawing-thread.
*/

void	draw_thread(void *args)
{
   static RECTL         ScreenSize;
   static long          Color,Color_Count = 0;
   static DATETIME	   DateTime;
   static flamestruct   *fs = &flames;
   static long          i, j, k;
   static BOOL          alt = FALSE, first = TRUE;

   HAB	   drawingthread_hab = WinInitialize(0);
   HMQ	   drawingthread_hmq = WinCreateMsgQueue(drawingthread_hab, 0);

   // Settings hopalong parameter.

   fs -> width  = screenSizeX / 2;
   fs -> height = screenSizeY / 2;
   fs -> max_levels = configuration_data.count;

   WinQueryWindowRect( hwndSaver, &ScreenSize );
   WinFillRect( hps, &ScreenSize, CLR_BLACK );
   
   DosGetDateTime ( &DateTime );
   srand ( DateTime.hundredths * 320 );
   Color = ((unsigned)rand()) % 16;      
   if ((Color==CLR_NEUTRAL) || (Color==CLR_BACKGROUND) || (Color==CLR_BLACK))
      Color++;
   GpiSetColor(hps, Color );

   first =- TRUE;

   while(!stop_draw_thread)
      {
      if (!(fs->cur_level++ % fs->max_levels))
         {
	      if ( first )
	         first = FALSE;
	      else
            DosSleep ( 3000 );
            
         WinQueryWindowRect( hwndSaver, &ScreenSize );
         WinFillRect( hps, &ScreenSize, CLR_BLACK );

         alt = !alt;
         }
      else
         {
         Color = rand () % 16;
         GpiSetColor ( hps , Color );
         }
             
      /* number of functions */
      fs->snum = 2 + (fs->cur_level % (MAXLEV - 1));

      /* how many of them are of alternate form */
      if (alt)
	      fs->anum = 0;
      else
         fs->anum = halfrandom ( fs -> snum ) + 2;

      /* 6 coefs per function */
      for (k = 0; k < fs->snum; k++)
         {
	      for (i = 0; i < 2; i++)
	         for (j = 0; j < 3; j++)
               {
		         fs->f[i][j][k] = ((double) (rand() & 1023) / 512.0 - 1.0);
               }
         }
      fs->num_points = 0;
      fs->total_points = 0;

      (void) Recur (hps , fs, 0.0, 0.0, 0);

      GpiPolyLineDisjoint ( hps , fs -> num_points * 2 , fs -> pts );
      
      // sleep a while if necessary
	   if(low_priority == FALSE)
		   DosSleep(1);

      }
	   
   WinDestroyMsgQueue(drawingthread_hmq);
   WinTerminate(drawingthread_hab);
}



