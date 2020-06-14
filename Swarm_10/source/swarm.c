/*
	swarm.c
	Swarm saver module C source file version 1.0   21 / 1 / 94
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

#include "swarm.h"

// ===== preprocessor definitions

#define MODULEVERSION		   0x00010001
#define STACKSIZE		         32000
#define SAVER_NAME_MAXLEN	   32
#define FUNCTION_CONFIGURE	   1
#define FUNCTION_STARTSAVER	2
#define FUNCTION_STOPSAVER	   3
#define FUNCTION_QUERYNAME	   4
#define FUNCTION_QUERYENABLED	5
#define FUNCTION_SETENABLED	6

#define CONFIGURATION_MINIMUM_COUNT  		   1
#define CONFIGURATION_DEFAULT_COUNT  		   100
#define CONFIGURATION_MAXIMUM_COUNT  	   	1000

/* acceleration of bees */
#define CONFIGURATION_MINIMUM_BEE_ACC 		   1
#define CONFIGURATION_DEFAULT_BEE_ACC  	   3
#define CONFIGURATION_MAXIMUM_BEE_ACC       	30

/* maximum bee velocity */
#define CONFIGURATION_MINIMUM_BEE_VEL 		   1
#define CONFIGURATION_DEFAULT_BEE_VEL  	   12
#define CONFIGURATION_MAXIMUM_BEE_VEL        40

/* maximum acceleration of wasp */
#define CONFIGURATION_MINIMUM_WASP_ACC 	   1
#define CONFIGURATION_DEFAULT_WASP_ACC  	   5
#define CONFIGURATION_MAXIMUM_WASP_ACC       30

/* maximum wasp velocity */
#define CONFIGURATION_MINIMUM_WASP_VEL 	   1
#define CONFIGURATION_DEFAULT_WASP_VEL  	   13
#define CONFIGURATION_MAXIMUM_WASP_VEL     	40

#define TIMES	   4		/* number of time positions recorded */
#define BORDER	   50	   /* wasp won't go closer than this to the edge */

/* Macros */

#define X(t,b)	   (x[(t)*beecount+(b)])
#define Y(t,b)	   (y[(t)*beecount+(b)])
#define RANDOM(v)	((rand()%(v))-((v)/2))	/* random number around 0 */

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
   int      bee_count;
   int      bee_velocity;
   int      bee_acceleration;
   int      wasp_velocity;
   int      wasp_acceleration;
} configuration_data;


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
	static	HWND  hwndBeeVel,hwndBeeAcc;
	static	HWND	hwndWaspVel,hwndWaspAcc;

	switch(msg)
   {
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
		WinSendMsg(hwndCount, SPBM_SETCURRENTVALUE, MPFROMSHORT(configuration_data.bee_count), MPVOID);

		hwndBeeVel = WinWindowFromID(hwnd, IDC_BEE_MAX_VEL);
		WinSendMsg(hwndBeeVel, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_BEE_VEL, (MPARAM)CONFIGURATION_MINIMUM_BEE_VEL);
		WinSendMsg(hwndBeeVel, SPBM_SETCURRENTVALUE, MPFROMSHORT(configuration_data.bee_velocity), MPVOID);

		hwndBeeAcc = WinWindowFromID(hwnd, IDC_BEE_ACCEL);
		WinSendMsg(hwndBeeAcc, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_BEE_ACC, (MPARAM)CONFIGURATION_MINIMUM_BEE_ACC);
		WinSendMsg(hwndBeeAcc, SPBM_SETCURRENTVALUE, MPFROMSHORT(configuration_data.bee_acceleration), MPVOID);

		hwndWaspVel = WinWindowFromID(hwnd, IDC_WASP_MAX_VEL);
		WinSendMsg(hwndWaspVel, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_WASP_VEL, (MPARAM)CONFIGURATION_MINIMUM_WASP_VEL);
		WinSendMsg(hwndWaspVel, SPBM_SETCURRENTVALUE, MPFROMSHORT(configuration_data.wasp_velocity), MPVOID);

		hwndWaspAcc = WinWindowFromID(hwnd, IDC_WASP_ACCEL);
		WinSendMsg(hwndWaspAcc, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_WASP_ACC, (MPARAM)CONFIGURATION_MINIMUM_WASP_ACC);
		WinSendMsg(hwndWaspAcc, SPBM_SETCURRENTVALUE, MPFROMSHORT(configuration_data.wasp_acceleration), MPVOID);

		// return FALSE since we did not change the focus		
      break;

	case WM_COMMAND:
		switch(SHORT1FROMMP(mp1))
         {
		case IDC_OK:
			// OK button was pressed. query the control settings
			configuration_data.enabled = SHORT1FROMMR(WinSendMsg(hwndEnabled, BM_QUERYCHECK, MPVOID, MPVOID));

         WinSendMsg(hwndCount,   SPBM_QUERYVALUE, MPFROMP(&configuration_data.bee_count), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
         WinSendMsg(hwndBeeAcc,  SPBM_QUERYVALUE, MPFROMP(&configuration_data.bee_acceleration), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
         WinSendMsg(hwndBeeVel,  SPBM_QUERYVALUE, MPFROMP(&configuration_data.bee_velocity), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
         WinSendMsg(hwndWaspAcc, SPBM_QUERYVALUE, MPFROMP(&configuration_data.wasp_acceleration), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
         WinSendMsg(hwndWaspVel, SPBM_QUERYVALUE, MPFROMP(&configuration_data.wasp_velocity), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));

			// write all configuration data to INI-file
			PrfWriteProfileData(HINI_USER, (PSZ)application_name, (PSZ)modulename, (PSZ)&configuration_data, sizeof(configuration_data));
			// end dialog
			WinDismissDlg(hwnd, TRUE);
			break;

		case IDC_CANCEL:
			// dialog was cancelled; end it
			WinDismissDlg(hwnd, FALSE);
			break;

      case IDC_DEFAULT:
		   WinSendMsg(hwndCount, SPBM_SETCURRENTVALUE, MPFROMSHORT(CONFIGURATION_DEFAULT_COUNT), MPVOID);
		   WinSendMsg(hwndBeeVel, SPBM_SETCURRENTVALUE, MPFROMSHORT(CONFIGURATION_DEFAULT_BEE_VEL), MPVOID);
		   WinSendMsg(hwndBeeAcc, SPBM_SETCURRENTVALUE, MPFROMSHORT(CONFIGURATION_DEFAULT_BEE_ACC), MPVOID);
		   WinSendMsg(hwndWaspVel, SPBM_SETCURRENTVALUE, MPFROMSHORT(CONFIGURATION_DEFAULT_WASP_VEL), MPVOID);
		   WinSendMsg(hwndWaspAcc, SPBM_SETCURRENTVALUE, MPFROMSHORT(CONFIGURATION_DEFAULT_WASP_ACC), MPVOID);
         
         WinSendMsg(hwndCount,   SPBM_QUERYVALUE, MPFROMP(&configuration_data.bee_count), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
         WinSendMsg(hwndBeeAcc,  SPBM_QUERYVALUE, MPFROMP(&configuration_data.bee_acceleration), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
         WinSendMsg(hwndBeeVel,  SPBM_QUERYVALUE, MPFROMP(&configuration_data.bee_velocity), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
         WinSendMsg(hwndWaspAcc, SPBM_QUERYVALUE, MPFROMP(&configuration_data.wasp_acceleration), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
         WinSendMsg(hwndWaspVel, SPBM_QUERYVALUE, MPFROMP(&configuration_data.wasp_velocity), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));

			// write all configuration data to INI-file
			PrfWriteProfileData(HINI_USER, (PSZ)application_name, (PSZ)modulename, (PSZ)&configuration_data, sizeof(configuration_data));

         break;
      }
   
   default:
	   return WinDefDlgProc(hwnd, msg, mp1, mp2);

	}
   return (MRESULT) FALSE;
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
			
         configuration_data.version           = MODULEVERSION;
			configuration_data.enabled           = TRUE;
			configuration_data.bee_count         = CONFIGURATION_DEFAULT_COUNT;
         configuration_data.bee_velocity      = CONFIGURATION_DEFAULT_BEE_VEL;
         configuration_data.bee_acceleration  = CONFIGURATION_DEFAULT_BEE_ACC;
         configuration_data.wasp_velocity     = CONFIGURATION_DEFAULT_WASP_VEL;
         configuration_data.wasp_acceleration = CONFIGURATION_DEFAULT_WASP_ACC;         

			PrfWriteProfileData(HINI_USER, (PSZ)application_name, (PSZ)modulename, (PSZ)&configuration_data, sizeof(configuration_data));
		}
		configuration_data_loaded = TRUE;
	}
}

/*
	draw_thread
	This is the drawing-thread.
	You have a valid presentation space handle (hps), a valid window
	handle (hwndSaver) and the configuration_data structure is loaded.
	The screen size is store in "screenSizeX" and "screenSizeY".
	IMPORTANT NOTE 1:
	You must check the "stop_draw_thread" flag regularly. If it is set,
	free all resources you have allocated and call DosExit (or just
	return) to end the drawing-thread.
	IMPORTANT NOTE 2:
	If the "low_priority" flag is NOT set (that means you run with
	regular priority, sharing CPU usage with other programs), you should
	call DosSleep(x) with "x" set at least to 1 as often as possible, to
	allow other programs to do their work. A screen saver should not eat
	up other program's CPU time!
	IMPORTANT NOTE 3:
	For some of the PM calls to work properly, your thread needs an
	own HAB and maybe even a message queue. You have to get and release
	both of them here if you use those PM calls.

	The following sample code is from the "Pyramids" module that comes
	with the ScreenSaver distribution.
	It selects a random color and a random point on the screen, then
	draws lines in the selected color from each corner of the screen
	to the selected point (looks somewhat like a pyramid).
	It remembers a number of points (this number can be set in the
	configuration dialog). Having filled the point memory, it redraws
	the "oldest" visible pyramid in black. This has the effect that more
	and more pixels on the screen get black, only a few constantly
	changing colored lines remain.

*/

void	draw_thread(void *args)
{
   static long          b,index;
	static int           distance,dx,dy;
   static RECTL         ScreenSize;
   static long          Color,ColorCount = 0;
   static DATETIME	   DateTime;
   static POINTL        p1,p2,op1,op2;
   static POINTL        ptl[3];
   static long          width,height;
   static long          beecount;
   static POINTL        *segs,*old_segs;  
   static long          *x,*y,*xv,*yv;
   static long          wx[3],wy[3],wxv,wyv;
   static int           waspAcc,waspVel;
   static int           beeAcc,beeVel;

   HAB	   drawingthread_hab = WinInitialize(0);
   HMQ	   drawingthread_hmq = WinCreateMsgQueue(drawingthread_hab, 0);

   // Settings swarm parameter.
   beecount = configuration_data.bee_count;
   width    = screenSizeX;
   height   = screenSizeY;
   waspAcc  = configuration_data.wasp_acceleration;
   waspVel  = configuration_data.wasp_velocity;
   beeAcc   = configuration_data.bee_acceleration;
   beeVel   = configuration_data.bee_velocity;

   ColorCount = 0;

   // Clear the background.
   WinQueryWindowRect( hwndSaver, &ScreenSize );
   WinFillRect( hps, &ScreenSize, CLR_BLACK );

   // Allocate memory.
   segs     = (POINTL *) ( malloc ( sizeof ( POINTL ) * beecount * 2 ) );
   old_segs = (POINTL *) ( malloc ( sizeof ( POINTL ) * beecount * 2 ) );
   
   xv = (long * ) malloc ( sizeof ( long ) * beecount );
   x  = (long * ) malloc ( sizeof ( long ) * beecount * TIMES);
   y  = (long * ) malloc ( sizeof ( long ) * beecount * TIMES);
   yv = (long * ) malloc ( sizeof ( long ) * beecount );

   /* Initialize point positions, velocities, etc. */

   /* wasp */
   wx[0] = BORDER + rand() % (width - 2 * BORDER);
   wy[0] = BORDER + rand() % (height - 2 * BORDER);
   wx[1] = wx[0];
   wy[1] = wy[0];
   wxv = 0;
   wyv = 0;
   
   /* bees */
   for (b = 0; b < beecount; b++) {
      X(0, b) = rand() % width;
      X(1, b) = X(0, b);
      Y(0, b) = rand() % height;
      Y(1, b) = Y(0, b);
      xv[b] = RANDOM(7);
      yv[b] = RANDOM(7);
      }
   
   // select random color
   ColorCount = ((unsigned) rand() % 2000);
   Color = ((unsigned)rand()) % 16;      
   if ((Color==CLR_NEUTRAL) || (Color==CLR_BACKGROUND) || (Color==CLR_BLACK))
      Color++;
   GpiSetColor(hps, Color );

   while(!stop_draw_thread)
      {
      DosGetDateTime ( &DateTime );
      srand ( DateTime.hundredths * 320 );

      ColorCount--;
      
      /* <=- Wasp -=> */    
      
      /* Age the arrays. */
      wx[2] = wx[1];
      wx[1] = wx[0];
      wy[2] = wy[1];
      wy[1] = wy[0];
      
      /* Accelerate */
      wxv += RANDOM(waspAcc);
      wyv += RANDOM(waspAcc);

      /* Speed Limit Checks */
      if (wxv > waspVel )
	      wxv = waspVel;
      if (wxv < -waspVel)
	      wxv = -waspVel;
      if (wyv > waspVel)
	      wyv = waspVel;
      if (wyv < -waspVel)
	      wyv = -waspVel;

      /* Move */
      wx[0] = wx[1] + wxv;
      wy[0] = wy[1] + wyv;

      /* Bounce Checks */
      if (( wx[0] < BORDER) || ( wx[0] > width - BORDER - 1))
         {
	      wxv = -wxv;
	      wx[0] += wxv;
         }
      if ((wy[0] < BORDER) || (wy[0] > height - BORDER - 1))
         {
         wyv = -wyv;
	      wy[0] += wyv;
         }

      /* Don't let things settle down. */
      xv[rand() % beecount] += RANDOM(3);
      yv[rand() % beecount] += RANDOM(3);

      ptl[0].x = wx[0];
      ptl[0].y = wy[0];
      ptl[1].x = wx[1];
      ptl[1].y = wy[1];
      ptl[2].x = wx[2];
      ptl[2].y = wy[2];
      
      GpiSetColor ( hps , CLR_GREEN );
      GpiMove ( hps , &ptl[0] );
      GpiLine ( hps , &ptl[1] );                
      GpiSetColor ( hps , CLR_BLACK );
      GpiLine ( hps , &ptl[2] );

      /* <=- Bees -=> */
      for (b = 0; b < beecount; b++)
         {

	      /* Age the arrays. */
	      X(2, b) = X(1, b);
	      X(1, b) = X(0, b);
	      Y(2, b) = Y(1, b);
      	Y(1, b) = Y(0, b);

	      /* Accelerate */
	      dx = wx[1] - X(1, b);
	      dy = wy[1] - Y(1, b);

	      distance = abs(dx) + abs(dy);	/* approximation */
	      
         if (distance == 0)
	         distance = 1;
	      
         xv[b] += (dx * beeAcc) / distance;
	      yv[b] += (dy * beeAcc) / distance;

	      /* Speed Limit Checks */
	      if (xv[b] > beeVel)
	         xv[b] = beeVel;
	      if (xv[b] < -beeVel)
	         xv[b] = -beeVel;
	      if (yv[b] > beeVel)
	         yv[b] = beeVel;
         if (yv[b] < -beeVel)
	         yv[b] = -beeVel;

	      /* Move */
	      X(0, b) = X(1, b) + xv[b];
	      Y(0, b) = Y(1, b) + yv[b];

         /* Bounce Checks */
         
         if (( x[b] < 0) || ( x[b] > width - 1))
            {
	         xv [b] = -xv [b];
            x[b] += xv[b];
            }

         if ((y[b] < 0) || (y[b] > height - 1))
            {    
            yv[b] = -yv[b];
	         y[b] += yv[b];
            }

	      /* Fill the segment lists. */
         index = b * 2;
         segs[index].x = X(0, b);
         segs[index].y = Y(0, b);
         p1.x = X(1,b);
         p1.y = Y(1,b);         
         old_segs[index] = p1;
         index++;
         segs[index] = p1;
         old_segs[index].x = X(2, b);
         old_segs[index].y = Y(2, b);
         }

      if ( ColorCount == 0 )
         {
         Color = ((unsigned)rand()) % 16;      
         if ((Color==CLR_NEUTRAL) || (Color==CLR_BACKGROUND) || (Color==CLR_BLACK))
            Color++;
         ColorCount = ((unsigned) rand() % 1000);
         }

      GpiSetColor ( hps , Color );
      GpiPolyLineDisjoint ( hps ,beecount * 2 , segs );
      GpiSetColor ( hps , CLR_BLACK );
      GpiPolyLineDisjoint ( hps ,beecount * 2 , old_segs );

      // sleep a while if necessary
	   if(low_priority == FALSE)
		   DosSleep(1);        	   
      }
   
   free ( segs );
   free ( old_segs );
   free ( x );
   free ( y );
   free ( xv );
   free ( yv );

   WinDestroyMsgQueue(drawingthread_hmq);
   WinTerminate(drawingthread_hab);
}


