/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997, 1998, 1999  Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@libsdl.org
*/



#ifdef SAVE_RCSID
static char rcsid =
 "@(#) $Id: SDL_sysevents.c,v 1.25 2004/02/16 21:09:23 slouken Exp $";
#endif

#include <stdlib.h>
#include <stdio.h>
#include <windows.h>

#include "SDL_getenv.h"
#include "SDL_events.h"
#include "SDL_video.h"
#include "SDL_error.h"
#include "SDL_syswm.h"
#include "SDL_sysevents.h"
#include "SDL_events_c.h"
#include "SDL_sysvideo.h"
#include "SDL_lowvideo.h"
#include "SDL_syswm_c.h"
#include "SDL_main.h"

#ifdef _WIN32_WCE
#include "../gapi/SDL_gapivideo.h"
#endif

#include "../../../../../gameEngine/dlmalloc.h" //maks



#ifdef WMMSG_DEBUG
#include "wmmsg.h"
#endif

#ifdef _WIN32_WCE
#define NO_GETKEYBOARDSTATE
#define NO_CHANGEDISPLAYSETTINGS
#endif

#ifdef _USE_POCKET_HAL_
extern void AdjustMouseCoords(Sint16 *x, Sint16 *y);
#endif



/* The window we use for everything... */
#ifdef _WIN32_WCE
LPWSTR SDL_Appname = NULL;
#else
LPSTR SDL_Appname = NULL;
static char fileName[256]; //maks
#endif

TCHAR lpFilename[256]; //maks

HINSTANCE SDL_Instance = NULL;
HWND SDL_Window = NULL;
RECT SDL_bounds = {0, 0, 0, 0};
int SDL_windowX = 0;
int SDL_windowY = 0;
int SDL_resizing = 0;
int mouse_relative = 0;
int posted = 0;
Uint8 button = 0; //maks
#ifndef NO_CHANGEDISPLAYSETTINGS
DEVMODE SDL_fullscreen_mode;
#endif
WORD *gamma_saved = NULL;



/* Functions called by the message processing function */
LONG
(*HandleMessage)(_THIS, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)=NULL;
void (*WIN_RealizePalette)(_THIS);
void (*WIN_PaletteChanged)(_THIS, HWND window);
void (*WIN_WinPAINT)(_THIS, HDC hdc);
extern void DIB_SwapGamma(_THIS);

/* JC 14 Mar 2006
   This is used all over the place, in the windib driver and in the dx5 driver
   So we may as well stick it here instead of having multiple copies scattered
   about
*/
void WIN_FlushMessageQueue()
{
	MSG  msg;
	while ( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) ) {
		if ( msg.message == WM_QUIT ) break;
		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}
}

static void SDL_RestoreGameMode(void)
{
#ifndef NO_CHANGEDISPLAYSETTINGS
	ShowWindow(SDL_Window, SW_RESTORE);
	ChangeDisplaySettings(&SDL_fullscreen_mode, CDS_FULLSCREEN);
#endif
}
static void SDL_RestoreDesktopMode(void)
{
#ifndef NO_CHANGEDISPLAYSETTINGS
	ShowWindow(SDL_Window, SW_MINIMIZE);
	ChangeDisplaySettings(NULL, 0);
#endif
}

#ifdef WM_MOUSELEAVE
/* 
   Special code to handle mouse leave events - this sucks...
   http://support.microsoft.com/support/kb/articles/q183/1/07.asp

   TrackMouseEvent() is only available on Win98 and WinNT.
   _TrackMouseEvent() is available on Win95, but isn't yet in the mingw32
   development environment, and only works on systems that have had IE 3.0
   or newer installed on them (which is not the case with the base Win95).
   Therefore, we implement our own version of _TrackMouseEvent() which
   uses our own implementation if TrackMouseEvent() is not available.
*/
static BOOL (WINAPI *_TrackMouseEvent)(TRACKMOUSEEVENT *ptme) = NULL;

static VOID CALLBACK
TrackMouseTimerProc(HWND hWnd, UINT uMsg, UINT idEvent, DWORD dwTime)
{
	RECT rect;
	POINT pt;

	GetClientRect(hWnd, &rect);
	MapWindowPoints(hWnd, NULL, (LPPOINT)&rect, 2);
	GetCursorPos(&pt);
	if ( !PtInRect(&rect, pt) || (WindowFromPoint(pt) != hWnd) ) {
		if ( !KillTimer(hWnd, idEvent) ) {
			/* Error killing the timer! */
		}
		PostMessage(hWnd, WM_MOUSELEAVE, 0, 0);
	}
}
static BOOL WINAPI WIN_TrackMouseEvent(TRACKMOUSEEVENT *ptme)
{
	if ( ptme->dwFlags == TME_LEAVE ) {
		return SetTimer(ptme->hwndTrack, ptme->dwFlags, 100,
		                (TIMERPROC)TrackMouseTimerProc);
	}
	return FALSE;
}
#endif /* WM_MOUSELEAVE */

/* Function to retrieve the current keyboard modifiers */
/*static*/ void WIN_GetKeyboardState(void) //maks
{
#ifndef NO_GETKEYBOARDSTATE
	SDLMod state;
	BYTE keyboard[256];
	Uint8 *kstate = SDL_GetKeyState(NULL);

	state = KMOD_NONE;
	if ( GetKeyboardState(keyboard) ) {
		if ( keyboard[VK_LSHIFT] & 0x80) {
			state |= KMOD_LSHIFT;
		}
		if ( keyboard[VK_RSHIFT] & 0x80) {
			state |= KMOD_RSHIFT;
		}
		if ( keyboard[VK_LCONTROL] & 0x80) {
			state |= KMOD_LCTRL;
		}
		if ( keyboard[VK_RCONTROL] & 0x80) {
			state |= KMOD_RCTRL;
		}
		if ( keyboard[VK_LMENU] & 0x80) {
			state |= KMOD_LALT;
		}
		if ( keyboard[VK_RMENU] & 0x80) {
			state |= KMOD_RALT;
		}
		if ( keyboard[VK_NUMLOCK] & 0x01) {
			state |= KMOD_NUM;
			kstate[SDLK_NUMLOCK] = SDL_PRESSED;
		}
		if ( keyboard[VK_CAPITAL] & 0x01) {
			state |= KMOD_CAPS;
			kstate[SDLK_CAPSLOCK] = SDL_PRESSED;
		}
	}
	SDL_SetModState(state);
#endif /* !NO_GETKEYBOARDSTATE */
}

/* The main Win32 event handler
DJM: This is no longer static as (DX5/DIB)_CreateWindow needs it
*/
int bInWinMessage = 0; //maks
LONG CALLBACK WinMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	SDL_VideoDevice *this = current_video;
	static int mouse_pressed = 0;
	static int in_window = 0;
#ifdef WMMSG_DEBUG
	fprintf(stderr, "Received windows message:  ");
	if ( msg > MAX_WMMSG ) {
		fprintf(stderr, "%d", msg);
	} else {
		fprintf(stderr, "%s", wmtab[msg]);
	}
	fprintf(stderr, " -- 0x%X, 0x%X\n", wParam, lParam);
#endif



	switch (msg) {

		case WM_ACTIVATE: {
			SDL_VideoDevice *this = current_video;

			if(this) //maks: avoid crash in PocketPC when exit game
			{
				BOOL minimized;
				Uint8 appstate;
				
				minimized = HIWORD(wParam);
				if ( !minimized && (LOWORD(wParam) != WA_INACTIVE) ) {
					/* Gain the following states */
					appstate = SDL_APPACTIVE|SDL_APPINPUTFOCUS;
					if ( this->input_grab != SDL_GRAB_OFF ) {
						WIN_GrabInput(this, SDL_GRAB_ON);
					}
					if ( !(SDL_GetAppState()&SDL_APPINPUTFOCUS) ) 
					{
						if ( ! DDRAW_FULLSCREEN() ) {
#ifndef _WIN32_WCE //maks
							DIB_SwapGamma(this);
#endif
						}
						
						if ( WINDIB_FULLSCREEN() ) {
							SDL_RestoreGameMode();
						}
					}
					posted = SDL_PrivateAppActive(1, appstate);
					WIN_GetKeyboardState();
				} else {
					/* Lose the following states */
					appstate = SDL_APPINPUTFOCUS;

					/////////////////////////////////
					//maks
					if(mouse_pressed)
					{
						SDL_PrivateMouseButton(SDL_RELEASED, button, 0, 0);
						ReleaseCapture();
						mouse_pressed = 0;
					}
					/////////////////////////////////

					if ( minimized ) {
						appstate |= SDL_APPACTIVE;
					}
					if ( this->input_grab != SDL_GRAB_OFF ) {
						WIN_GrabInput(this, SDL_GRAB_OFF);
					}
					if ( SDL_GetAppState() & SDL_APPINPUTFOCUS ) {
						if ( ! DDRAW_FULLSCREEN() ) {
#ifndef _WIN32_WCE //maks
							DIB_SwapGamma(this);
#endif
						}
						if ( WINDIB_FULLSCREEN() ) {
							SDL_RestoreDesktopMode();
						}
					}
					posted = SDL_PrivateAppActive(0, appstate);
				}
			}

			return(0);
		}
		break;

#ifdef _WIN32_WCE //maks
		case 0x0086/*WM_NCACTIVATE*/: //maks
		{
			SDL_VideoDevice *this = current_video;

			if(this) //maks: avoid crash in PocketPC when exit game
			{
				Uint8 appstate = SDL_APPACTIVE | SDL_APPINPUTFOCUS;
				
				if(wParam)
				{
					if ( this->input_grab != SDL_GRAB_OFF ) 
					{
						WIN_GrabInput(this, SDL_GRAB_ON);
					}
					
					if ( !(SDL_GetAppState()&SDL_APPINPUTFOCUS) ) 
					{
						if ( ! DDRAW_FULLSCREEN() ) {
#ifndef _WIN32_WCE //maks
							DIB_SwapGamma(this);
#endif
						}
						if ( WINDIB_FULLSCREEN() ) {
							SDL_RestoreGameMode();
						}
					}
					posted = SDL_PrivateAppActive(1, appstate);
					WIN_GetKeyboardState();
				}
			}

			return(0);
		}
		break;

		case WM_CANCELMODE: //maks
		{ 
			Uint8 appstate;
				
			/* Lose the following states */
			appstate = SDL_APPACTIVE | SDL_APPINPUTFOCUS;

			posted = SDL_PrivateAppActive(0, appstate);
				
			return(0);
		}
		break;
#else
		case WM_DROPFILES: //maks
		{
			if(DragQueryFile((HDROP)wParam, 0, fileName, 255 ))   
			{ 
				SDL_Event event;

				memset(&event, 0, sizeof(SDL_Event));
				
				event.type = ( SDL_USEREVENT + 7 ); //SDL_DROPFILES
				event.user.data1 = fileName;
				SDL_PushEvent(&event);				
			}      
			
			DragFinish((HDROP)wParam);
		}
		break;		

		case WM_COPYDATA: //maks
		{
			//Used by single instance to open a file in explorer			

			PCOPYDATASTRUCT pCds = (PCOPYDATASTRUCT)lParam;
			if(pCds->cbData && pCds->lpData)
			{
				SDL_Event event;

				memset(&event, 0, sizeof(SDL_Event));
				memset(fileName, 0, 255);

				strncpy(fileName, pCds->lpData, pCds->cbData);
				fileName[pCds->cbData] = 0;

				event.type = ( SDL_USEREVENT + 7 ); //SDL_DROPFILES
				event.user.data1 = fileName;
				SDL_PushEvent(&event);
			}
		}
		break;
#endif

		case WM_MOUSEMOVE: 
			if(this) //maks
			{
			
			/* Mouse is handled by DirectInput when fullscreen */
			if ( SDL_VideoSurface && ! DINPUT_FULLSCREEN() ) {
				Sint16 x, y;

				/* mouse has entered the window */
				if ( ! in_window ) {
#ifdef WM_MOUSELEAVE
					TRACKMOUSEEVENT tme;

					tme.cbSize = sizeof(tme);
					tme.dwFlags = TME_LEAVE;
					tme.hwndTrack = SDL_Window;
					_TrackMouseEvent(&tme);
#endif /* WM_MOUSELEAVE */
					in_window = TRUE;

					posted = SDL_PrivateAppActive(1, SDL_APPMOUSEFOCUS);
				}

				/* mouse has moved within the window */
				x = LOWORD(lParam);
				y = HIWORD(lParam);

#ifdef _USE_POCKET_HAL_
				AdjustMouseCoords(&x, &y);
#endif

				if ( mouse_relative ) {
					POINT center;
					center.x = (SDL_VideoSurface->w/2);
					center.y = (SDL_VideoSurface->h/2);
					x -= (Sint16)center.x;
					y -= (Sint16)center.y;
					if ( x || y ) {
						ClientToScreen(SDL_Window, &center);
						SetCursorPos(center.x, center.y);
						posted = SDL_PrivateMouseMotion(0, 1, x, y);
					}
				} else {
					posted = SDL_PrivateMouseMotion(0, 0, x, y);
				}
			}
		}
		return(0);

#ifdef WM_MOUSELEAVE
		case WM_MOUSELEAVE: {

			/* Mouse is handled by DirectInput when fullscreen */
			if ( this && SDL_VideoSurface && ! DINPUT_FULLSCREEN() ) { //maks
				/* mouse has left the window */
				/* or */
				/* Elvis has left the building! */
				posted = SDL_PrivateAppActive(0, SDL_APPMOUSEFOCUS);
			}
			in_window = FALSE;
		}
		return(0);
#endif /* WM_MOUSELEAVE */

		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP: 
			if(this) //maks
			{
			/* Mouse is handled by DirectInput when fullscreen */
			if ( SDL_VideoSurface && ! DINPUT_FULLSCREEN() ) {
				Sint16 x, y;
				Uint8 /*button,*/ state; //maks

				/* DJM:
				   We want the SDL window to take focus so that
				   it acts like a normal windows "component"
				   (e.g. gains keyboard focus on a mouse click).
				 */
				SetFocus(SDL_Window);

				/* Figure out which button to use */
				switch (msg) {
					case WM_LBUTTONDOWN:
						button = SDL_BUTTON_LEFT;
						state = SDL_PRESSED;
						break;
					case WM_LBUTTONUP:
						button = SDL_BUTTON_LEFT;
						state = SDL_RELEASED;
						break;
					case WM_MBUTTONDOWN:
						button = SDL_BUTTON_MIDDLE;
						state = SDL_PRESSED;
						break;
					case WM_MBUTTONUP:
						button = SDL_BUTTON_MIDDLE;
						state = SDL_RELEASED;
						break;
					case WM_RBUTTONDOWN:
						button = SDL_BUTTON_RIGHT;
						state = SDL_PRESSED;
						break;
					case WM_RBUTTONUP:
						button = SDL_BUTTON_RIGHT;
						state = SDL_RELEASED;
						break;
					default:
						/* Eh? Unknown button? */
						return(0);
				}
				if ( state == SDL_PRESSED ) {
					/* Grab mouse so we get up events */
					if ( ++mouse_pressed > 0 ) {
						SetCapture(hwnd);
					}
				} else {
					/* Release mouse after all up events */
					if ( --mouse_pressed <= 0 ) {
						ReleaseCapture();
						mouse_pressed = 0;
					}
				}
				if ( mouse_relative ) {
				/*	RJR: March 28, 2000
					report internal mouse position if in relative mode */
					x = 0; y = 0;
				} else {
					x = (Sint16)LOWORD(lParam);
					y = (Sint16)HIWORD(lParam);
				}

#ifdef _USE_POCKET_HAL_
				AdjustMouseCoords(&x, &y);
#endif
				posted = SDL_PrivateMouseButton(
							state, button, x, y);
			}
		}
		return(0);


#if (_WIN32_WINNT >= 0x0400) || (_WIN32_WINDOWS > 0x0400)
		case WM_MOUSEWHEEL: 
			if ( this && SDL_VideoSurface && ! DINPUT_FULLSCREEN() ) { //maks
				int move = (short)HIWORD(wParam);
				if ( move ) {
					Uint8 button;
					if ( move > 0 )
						button = SDL_BUTTON_WHEELUP;
					else
						button = SDL_BUTTON_WHEELDOWN;
					posted = SDL_PrivateMouseButton(
						SDL_PRESSED, button, 0, 0);
					posted |= SDL_PrivateMouseButton(
						SDL_RELEASED, button, 0, 0);
				}
			}
			return(0);
#endif

#ifdef WM_GETMINMAXINFO
		/* This message is sent as a way for us to "check" the values
		 * of a position change.  If we don't like it, we can adjust
		 * the values before they are changed.
		 */
		case WM_GETMINMAXINFO: 
			if(this) //maks
			{
			MINMAXINFO *info;
			RECT        size;
			int x, y;
			int style;
			int width;
			int height;

			/* We don't want to clobber an internal resize */
			if ( SDL_resizing )
				return(0);

			/* We allow resizing with the SDL_RESIZABLE flag */
			if ( SDL_PublicSurface &&
				(SDL_PublicSurface->flags & SDL_RESIZABLE) ) {
				return(0);
			}

			/* Get the current position of our window */
			if(SDL_Window) //maks
			{
				GetWindowRect(SDL_Window, &size);
				x = size.left;
				y = size.top;
			}
			else
			{
				x = y = 0;
			}

			/* Calculate current width and height of our window */
			size.top = 0;
			size.left = 0;
			if ( SDL_PublicSurface != NULL ) {
				size.bottom = SDL_PublicSurface->h;
				size.right = SDL_PublicSurface->w;
			} else {
				size.bottom = 0;
				size.right = 0;
			}

			/* DJM - according to the docs for GetMenu(), the
			   return value is undefined if hwnd is a child window.
			   Aparently it's too difficult for MS to check
			   inside their function, so I have to do it here.
          		 */
         		style = GetWindowLong(hwnd, GWL_STYLE);
         		AdjustWindowRect(
				&size,
				style,
            			style & WS_CHILDWINDOW ? FALSE
						       : GetMenu(hwnd) != NULL);

			width = size.right - size.left;
			height = size.bottom - size.top;

			/* Fix our size to the current size */
			info = (MINMAXINFO *)lParam;
			info->ptMaxSize.x = width;
			info->ptMaxSize.y = height;
			info->ptMaxPosition.x = x;
			info->ptMaxPosition.y = y;
			info->ptMinTrackSize.x = width;
			info->ptMinTrackSize.y = height;
			info->ptMaxTrackSize.x = width;
			info->ptMaxTrackSize.y = height;
		}
		return(0);
#endif /* WM_GETMINMAXINFO */

		case WM_WINDOWPOSCHANGED: {
			SDL_VideoDevice *this = current_video;

			if(this) //maks: avoid crash in PocketPC when exit game
			{
				int w, h;
				
				GetClientRect(SDL_Window, &SDL_bounds);
				ClientToScreen(SDL_Window, (LPPOINT)&SDL_bounds);
				ClientToScreen(SDL_Window, (LPPOINT)&SDL_bounds+1);
				if ( SDL_bounds.left || SDL_bounds.top ) 
				{
				/*SDL_windowX = SDL_bounds.left; //maks: maximized error
					SDL_windowY = SDL_bounds.top;*/
					
					SDL_windowX = SDL_windowY = 0;
				}
				
				w = SDL_bounds.right-SDL_bounds.left;
				h = SDL_bounds.bottom-SDL_bounds.top;
				
				if ( this->input_grab != SDL_GRAB_OFF ) 
				{
					ClipCursor(&SDL_bounds);
				}
				
				if ( SDL_PublicSurface && 
					(SDL_PublicSurface->flags & SDL_RESIZABLE) ) 
				{
					SDL_PrivateResize(w, h);
				}
			}
		}
		break;

		/* We need to set the cursor */
		case WM_SETCURSOR: {
			Uint16 hittest;

			hittest = LOWORD(lParam);
			if ( hittest == HTCLIENT ) {
				SetCursor(SDL_hcursor);
				return(TRUE);
			}
		}
		break;

		/* We are about to get palette focus! */
		case WM_QUERYNEWPALETTE: {
			WIN_RealizePalette(current_video);
			return(TRUE);
		}
		break;

		/* Another application changed the palette */
		case WM_PALETTECHANGED: {
			WIN_PaletteChanged(current_video, (HWND)wParam);
		}
		break;

		/* We were occluded, refresh our display */
		case WM_PAINT: 
			if(current_video) //maks
			{
				HDC hdc;
				PAINTSTRUCT ps;
				
				hdc = BeginPaint(SDL_Window, &ps);
				if ( current_video->screen &&
					!(current_video->screen->flags & SDL_OPENGL) ) {
					WIN_WinPAINT(current_video, hdc);
				}
				EndPaint(SDL_Window, &ps);
		}
		return(0);

		/* DJM: Send an expose event in this case */
		case WM_ERASEBKGND: {
			posted = SDL_PrivateExpose();
		}
		return(0);

#ifndef _WIN32_WCE //maks: handled in the gapievents

		case WM_CLOSE: {
			if ( (posted = SDL_PrivateQuit()) )
				PostQuitMessage(0);
		}
		return(0);


		case WM_DESTROY: {
			PostQuitMessage(0);
		}
		return(0);

#endif

		default: {
			/* Special handling by the video driver */
			if (HandleMessage) //maks
			{
				int res;
				bInWinMessage = 1; //to avoid recursive calling in DX5_HandleMessage and DIB_HandleMessage when exit Game Mode with wx
				res = HandleMessage(current_video,
			                     hwnd, msg, wParam, lParam);
				bInWinMessage = 0;

				return res;
			}
		}
		break;
	}
	return(DefWindowProc(hwnd, msg, wParam, lParam));
}

/* Allow the application handle to be stored and retrieved later */
static void *SDL_handle = NULL;

void SDL_SetModuleHandle(void *handle)
{
	SDL_handle = handle;
}
void *SDL_GetModuleHandle(void)
{
	void *handle;

	if ( SDL_handle ) {
		handle = SDL_handle;
	} else {
		/* Warning:
		   If SDL is built as a DLL, this will return a handle to
		   the DLL, not the application, and DirectInput may fail
		   to initialize.
		 */
		handle = GetModuleHandle(NULL);
	}
	return(handle);
}

/* This allows the SDL_WINDOWID hack */
const char *SDL_windowid = NULL;

void SetAppName(char *name) //maks
{
#ifdef _WIN32_WCE
	{
		/* WinCE uses the UNICODE version */
		int nLen = strlen(name)+1;
		SDL_Appname = malloc(nLen*2);
		MultiByteToWideChar(CP_ACP, 0, name, -1, SDL_Appname, nLen);
	}
#else
	{
		int nLen = strlen(name)+1;
		SDL_Appname = malloc(nLen);
		strcpy(SDL_Appname, name);
	}
#endif /* _WIN32_WCE */
}

TCHAR *RemovePath(TCHAR *file)
{
	TCHAR *name, *pEnd = NULL;
	int len = 0;

#ifdef _WIN32_WCE
	{
		pEnd = wcsrchr(file, L'\\');
		if(pEnd) 
		{
			pEnd++;
			len = wcslen(pEnd)*2;
		}
	}
#else
	{
		pEnd = strrchr(file, '\\');
		if(pEnd) 
		{
			pEnd++;
			len = strlen(pEnd);
		}
	}
#endif /* _WIN32_WCE */

	if(len)
	{
		name = malloc(len + 2);

#ifdef _WIN32_WCE
		wcscpy(name, pEnd);
#else
		strcpy(name, pEnd);
#endif /* _WIN32_WCE */
	}
	else
	{
		name = file;
	}

	return name;
}

/* Register the class for this application -- exported for winmain.c */
int SDL_RegisterApp(char *name, Uint32 style, void *hInst)
{
	static int initialized = 0;
	WNDCLASS class;
#ifdef WM_MOUSELEAVE
	HMODULE handle;
#endif

	/* Check for SDL_WINDOWID hack */
	SDL_windowid = getenv("SDL_WINDOWID"); //maks: put here before initialize variable test

	/* Only do this once... */
	if ( initialized ) {
		return(0);
	}

	/* This function needs to be passed the correct process handle
	   by the application.
	 */
	if ( ! hInst ) {
		hInst = SDL_GetModuleHandle();
	}

	/* Register the application class */
	class.hCursor		= NULL;
	
	if(name) //maks
	{		
		SetAppName(name);
	}
	else
	{
		//Get from file name
		int len = GetModuleFileName(hInst, lpFilename, 256);
		if(len)
		{
			if(len > 4 && lpFilename[len - 4] == '.') lpFilename[len - 4] = 0;
			SDL_Appname = RemovePath(lpFilename);
		}
		else
		{
			SetAppName("GameEditor");
		}
	}


	class.hIcon		= LoadImage(hInst, SDL_Appname, IMAGE_ICON,
	                                    0, 0, LR_DEFAULTCOLOR);
	class.lpszMenuName	= NULL;
	class.lpszClassName	= SDL_Appname;
	class.hbrBackground	= NULL;
	class.hInstance		= hInst;
	class.style		= style;
#if defined(HAVE_OPENGL) && !defined(_WIN32_WCE)
	class.style		|= CS_OWNDC;
#endif
	class.lpfnWndProc	= WinMessage;
	class.cbWndExtra	= 0;
	class.cbClsExtra	= 0;
	if ( ! RegisterClass(&class) ) {
		SDL_SetError("Couldn't register application class");
		return(-1);
	}
	SDL_Instance = hInst;

#ifdef WM_MOUSELEAVE
	/* Get the version of TrackMouseEvent() we use */
	_TrackMouseEvent = NULL;
	handle = GetModuleHandle("USER32.DLL");
	if ( handle ) {
		_TrackMouseEvent = (BOOL (WINAPI *)(TRACKMOUSEEVENT *))GetProcAddress(handle, "TrackMouseEvent");
	}
	if ( _TrackMouseEvent == NULL ) {
		_TrackMouseEvent = WIN_TrackMouseEvent;
	}
#endif /* WM_MOUSELEAVE */

	

	initialized = 1;
	return(0);
}


