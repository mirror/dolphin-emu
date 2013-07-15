// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#if defined HAVE_X11 && HAVE_X11
#include <X11/keysym.h>
#include "X11Utils.h"
#endif

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#endif

#include "Common.h"
#include "BootManager.h"
#include "ConfigManager.h"
#include "LogManager.h"
#if defined HAVE_X11 && HAVE_X11
#include "State.h"
#endif
#include "Thread.h"
#include "VideoBackendBase.h"

#include "CPUDetect.h"
#include "Core.h"
#include "CoreParameter.h"
#include "HW/CPU.h"
#include "HW/Wiimote.h"
#include "Host.h"
#include "PowerPC/PowerPC.h"

#include "CLI.h"

namespace CLI
{

Common::Event updateMainFrameEvent;

bool isCLI = false;

bool rendererHasFocus = true;
bool running = true;

// ------------
// CLI methods

void Entry(std::string exec, bool init)
{
	if (init)
	{
		LogManager::Init();
		VideoBackend::PopulateList();
		VideoBackend::ActivateBackend(SConfig::GetInstance().m_LocalCoreStartupParameter.m_strVideoBackend);
		WiimoteReal::LoadSettings();
	}

	// No use running the loop when booting fails
	if (BootManager::BootCore(exec))
	{
#ifdef __APPLE__
		NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
		NSEvent *event = [[NSEvent alloc] init];

		while (running)
		{
			event = [NSApp nextEventMatchingMask: NSAnyEventMask
				untilDate: [NSDate distantFuture]
				inMode: NSDefaultRunLoopMode dequeue: YES];

			if ([event type] == NSKeyDown &&
				[event modifierFlags] & NSCommandKeyMask &&
				[[event characters] UTF8String][0] == 'q')
			{
				Core::Stop();
				break;
			}

			if ([event type] != NSKeyDown)
				[NSApp sendEvent: event];
		}

		[event release];
		[pool release];
#elif defined HAVE_X11 && HAVE_X11
		XInitThreads();
		X11_MainLoop();
#else
		while (running && PowerPC::GetState() != PowerPC::CPU_POWERDOWN)
			updateMainFrameEvent.Wait();
		Core::Stop();
#endif
	}

	WiimoteReal::Shutdown();
	VideoBackend::ClearList();
	SConfig::Shutdown();
	LogManager::Shutdown();
}

#if defined(HAVE_X11) && HAVE_X11
void X11_MainLoop()
{
	bool fullscreen = SConfig::GetInstance().m_LocalCoreStartupParameter.bFullscreen;
	while (Core::GetState() == Core::CORE_UNINITIALIZED)
		updateMainFrameEvent.Wait();

	Display *dpy = XOpenDisplay(0);
	Window win = (Window)Core::GetWindowHandle();
	XSelectInput(dpy, win, KeyPressMask | FocusChangeMask);

	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bDisableScreenSaver)
		X11Utils::InhibitScreensaver(dpy, win, true);

#if defined(HAVE_XRANDR) && HAVE_XRANDR
	X11Utils::XRRConfiguration *XRRConfig = new X11Utils::XRRConfiguration(dpy, win);
#endif

	Cursor blankCursor = None;
	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bHideCursor)
	{
		// make a blank cursor
		Pixmap Blank;
		XColor DummyColor;
		char ZeroData[1] = {0};
		Blank = XCreateBitmapFromData (dpy, win, ZeroData, 1, 1);
		blankCursor = XCreatePixmapCursor(dpy, Blank, Blank, &DummyColor, &DummyColor, 0, 0);
		XFreePixmap (dpy, Blank);
		XDefineCursor(dpy, win, blankCursor);
	}

	if (fullscreen)
	{
		X11Utils::EWMH_Fullscreen(dpy, _NET_WM_STATE_TOGGLE);
#if defined(HAVE_XRANDR) && HAVE_XRANDR
		XRRConfig->ToggleDisplayMode(True);
#endif
	}

	// The actual loop
	while (running)
	{
		XEvent event;
		KeySym key;
		for (int num_events = XPending(dpy); num_events > 0; num_events--)
		{
			XNextEvent(dpy, &event);
			switch(event.type)
			{
				case KeyPress:
					key = XLookupKeysym((XKeyEvent*)&event, 0);
					if (key == XK_Escape)
					{
						if (Core::GetState() == Core::CORE_RUN)
						{
							if (SConfig::GetInstance().m_LocalCoreStartupParameter.bHideCursor)
								XUndefineCursor(dpy, win);
							Core::SetState(Core::CORE_PAUSE);
						}
						else
						{
							if (SConfig::GetInstance().m_LocalCoreStartupParameter.bHideCursor)
								XDefineCursor(dpy, win, blankCursor);
							Core::SetState(Core::CORE_RUN);
						}
					}
					else if ((key == XK_Return) && (event.xkey.state & Mod1Mask))
					{
						fullscreen = !fullscreen;
						X11Utils::EWMH_Fullscreen(dpy, _NET_WM_STATE_TOGGLE);
#if defined(HAVE_XRANDR) && HAVE_XRANDR
						XRRConfig->ToggleDisplayMode(fullscreen);
#endif
					}
					else if (key >= XK_F1 && key <= XK_F8)
					{
						int slot_number = key - XK_F1 + 1;
						if (event.xkey.state & ShiftMask)
							State::Save(slot_number);
						else
							State::Load(slot_number);
					}
					else if (key == XK_F9)
						Core::SaveScreenShot();
					else if (key == XK_F11)
						State::LoadLastSaved();
					else if (key == XK_F12)
					{
						if (event.xkey.state & ShiftMask)
							State::UndoLoadState();
						else
							State::UndoSaveState();
					}
					break;
				case FocusIn:
					rendererHasFocus = true;
					if (SConfig::GetInstance().m_LocalCoreStartupParameter.bHideCursor &&
							Core::GetState() != Core::CORE_PAUSE)
						XDefineCursor(dpy, win, blankCursor);
					break;
				case FocusOut:
					rendererHasFocus = false;
					if (SConfig::GetInstance().m_LocalCoreStartupParameter.bHideCursor)
						XUndefineCursor(dpy, win);
					break;
			}
		}
		if (!fullscreen)
		{
			Window winDummy;
			unsigned int borderDummy, depthDummy;
			XGetGeometry(dpy, win, &winDummy,
					&SConfig::GetInstance().m_LocalCoreStartupParameter.iRenderWindowXPos,
					&SConfig::GetInstance().m_LocalCoreStartupParameter.iRenderWindowYPos,
					(unsigned int *)&SConfig::GetInstance().m_LocalCoreStartupParameter.iRenderWindowWidth,
					(unsigned int *)&SConfig::GetInstance().m_LocalCoreStartupParameter.iRenderWindowHeight,
					&borderDummy, &depthDummy);
		}
		usleep(100000);
	}

#if defined(HAVE_XRANDR) && HAVE_XRANDR
	delete XRRConfig;
#endif
	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bDisableScreenSaver)
		X11Utils::InhibitScreensaver(dpy, win, false);

	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bHideCursor)
		XFreeCursor(dpy, blankCursor);
	XCloseDisplay(dpy);
	Core::Stop();
}
#endif


// ------------
// Talk to interface

void* GetRenderHandle()
{
	return NULL;
}

void GetRenderWindowSize(int& x, int& y, int& width, int& height)
{
	x = SConfig::GetInstance().m_LocalCoreStartupParameter.iCLIRenderWindowXPos;
	y = SConfig::GetInstance().m_LocalCoreStartupParameter.iCLIRenderWindowYPos;
	width = SConfig::GetInstance().m_LocalCoreStartupParameter.iCLIRenderWindowWidth;
	height = SConfig::GetInstance().m_LocalCoreStartupParameter.iCLIRenderWindowHeight;
}

void Message(int Id)
{
	switch (Id)
	{
		case WM_USER_STOP:
			running = false;
			break;
	}
	updateMainFrameEvent.Set();
}

void SetStartupDebuggingParameters()
{
	SCoreStartupParameter& StartUp = SConfig::GetInstance().m_LocalCoreStartupParameter;
	StartUp.bEnableDebugging = false;
	StartUp.bBootToPause = false;
}

void UpdateMainFrame()
{
	updateMainFrameEvent.Set();
}

}