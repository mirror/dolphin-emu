// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "Host.h"
#include "RenderBase.h"
#include "VideoConfig.h"
#include "../GLInterface.h"
#include "VertexShaderManager.h"

#if USE_EGL
bool cXInterface::ServerConnect(void)
{
	GLWin.dpy = XOpenDisplay(NULL);

	if (!GLWin.dpy)
		return false;

	return true;
}

bool cXInterface::Initialize(void *config)
{
	int _tx, _ty, _twidth, _theight;
	XVisualInfo  visTemplate;
	int num_visuals;
	EGLint vid;

	if (!GLWin.dpy) {
		printf("Error: couldn't open X display\n");
		return false;
	}

	if (!eglGetConfigAttrib(GLWin.egl_dpy, config, EGL_NATIVE_VISUAL_ID, &vid)) {
		printf("Error: eglGetConfigAttrib() failed\n");
		exit(1);
	}

	/* The X window visual must match the EGL config */
	visTemplate.visualid = vid;
	GLWin.vi = XGetVisualInfo(GLWin.dpy, VisualIDMask, &visTemplate, &num_visuals);
	if (!GLWin.vi) {
		printf("Error: couldn't get X visual\n");
		exit(1);
	}

	Host_GetRenderWindowSize(_tx, _ty, _twidth, _theight);

	GLWin.x = _tx;
	GLWin.y = _ty;
	GLWin.width = _twidth;
	GLWin.height = _theight;

	GLWin.evdpy = XOpenDisplay(NULL);
	GLWin.parent = GLWin.win;
	GLWin.screen = DefaultScreen(GLWin.dpy);

	if (GLWin.parent == 0)
		GLWin.parent = RootWindow(GLWin.dpy, GLWin.screen);

	/* Set initial projection/viewing transformation.
	 * We can't be sure we'll get a ConfigureNotify event when the window
	 * first appears.
	 */
	glViewport(0, 0, (GLint) GLWin.width, (GLint) GLWin.height);

	return true;
}

void *cXInterface::EGLGetDisplay(void)
{
	return eglGetDisplay(GLWin.dpy);
}

void *cXInterface::CreateWindow(void)
{
	Atom wmProtocols[1];

	// Setup window attributes
	GLWin.attr.colormap = XCreateColormap(GLWin.evdpy,
			GLWin.parent, GLWin.vi->visual, AllocNone);
	GLWin.attr.event_mask = KeyPressMask | StructureNotifyMask | FocusChangeMask;
	GLWin.attr.background_pixel = BlackPixel(GLWin.evdpy, GLWin.screen);
	GLWin.attr.border_pixel = 0;

	// Create the window
	GLWin.win = XCreateWindow(GLWin.evdpy, GLWin.parent,
			GLWin.x, GLWin.y, GLWin.width, GLWin.height, 0,
			GLWin.vi->depth, InputOutput, GLWin.vi->visual,
			CWBorderPixel | CWBackPixel | CWColormap | CWEventMask, &GLWin.attr);
	wmProtocols[0] = XInternAtom(GLWin.evdpy, "WM_DELETE_WINDOW", True);
	XSetWMProtocols(GLWin.evdpy, GLWin.win, wmProtocols, 1);
	XSetStandardProperties(GLWin.evdpy, GLWin.win, "GPU", "GPU", None, NULL, 0, NULL);
	XMapRaised(GLWin.evdpy, GLWin.win);
	XSync(GLWin.evdpy, True);

	GLWin.xEventThread.Run(&cXInterface::XEventThread, this);
	// Control window size and picture scaling
	GLInterface->SetBackBufferDimensions(GLWin.width, GLWin.height);

	return (void *) GLWin.win;
}

void cXInterface::DestroyWindow(void)
{
	XDestroyWindow(GLWin.evdpy, GLWin.win);
	GLWin.win = 0;
	if (GLWin.xEventThread.joinable())
		GLWin.xEventThread.join();
	XFreeColormap(GLWin.evdpy, GLWin.attr.colormap);
}

void cXInterface::UpdateFPSDisplay(const char *text)
{
	XStoreName(GLWin.evdpy, GLWin.win, text);
}

void cXInterface::XEventThread()
#else
void cX11Window::CreateXWindow(void)
{
	Atom wmProtocols[1];

	// Setup window attributes
	GLWin.attr.colormap = XCreateColormap(GLWin.evdpy,
			GLWin.parent, GLWin.vi->visual, AllocNone);
	GLWin.attr.event_mask = KeyPressMask | StructureNotifyMask | FocusChangeMask;
	GLWin.attr.background_pixel = BlackPixel(GLWin.evdpy, GLWin.screen);
	GLWin.attr.border_pixel = 0;

	// Create the window
	GLWin.win = XCreateWindow(GLWin.evdpy, GLWin.parent,
			GLWin.x, GLWin.y, GLWin.width, GLWin.height, 0,
			GLWin.vi->depth, InputOutput, GLWin.vi->visual,
			CWBorderPixel | CWBackPixel | CWColormap | CWEventMask, &GLWin.attr);
	wmProtocols[0] = XInternAtom(GLWin.evdpy, "WM_DELETE_WINDOW", True);
	XSetWMProtocols(GLWin.evdpy, GLWin.win, wmProtocols, 1);
	XSetStandardProperties(GLWin.evdpy, GLWin.win, "GPU", "GPU", None, NULL, 0, NULL);
	XMapRaised(GLWin.evdpy, GLWin.win);
	XSync(GLWin.evdpy, True);

	GLWin.xEventThread.Run(&cX11Window::XEventThread, this, "GUI");
}

void cX11Window::DestroyXWindow(void)
{
	XUnmapWindow(GLWin.evdpy, GLWin.win);
	GLWin.win = 0;
	if (GLWin.xEventThread.joinable())
		GLWin.xEventThread.join();
	XFreeColormap(GLWin.evdpy, GLWin.attr.colormap);
}

void cX11Window::XEventThread()
#endif
{
	// Free look variables
	static bool mouseLookEnabled = false;
	static bool mouseMoveEnabled = false;
	static float lastMouse[2];
	while (GLWin.win)
	{
		XEvent event;
		KeySym key;
		for (int num_events = XPending(GLWin.evdpy); num_events > 0; num_events--)
		{
			XNextEvent(GLWin.evdpy, &event);
			switch(event.type) {
				case KeyPress:
					key = XLookupKeysym((XKeyEvent*)&event, 0);
					if (g_Config.bOSDHotKey)
					{
						switch (key)
						{
							case XK_3:
								OSDChoice = 1;
								// Toggle native resolution
								g_Config.iEFBScale = g_Config.iEFBScale + 1;
								if (g_Config.iEFBScale > 7) g_Config.iEFBScale = 0;
								break;

							case XK_4:
								OSDChoice = 2;
								// Toggle aspect ratio
								g_Config.iAspectRatio = (g_Config.iAspectRatio + 1) & 3;
								break;

							case XK_5:
								OSDChoice = 3;
								// Toggle EFB copy
								if (!g_Config.bEFBCopyEnable || g_Config.bCopyEFBToTexture)
								{
									g_Config.bEFBCopyEnable ^= true;
									g_Config.bCopyEFBToTexture = false;
								}
								else
								{
									g_Config.bCopyEFBToTexture = !g_Config.bCopyEFBToTexture;
								}
								break;

							case XK_6:
								OSDChoice = 4;
								g_Config.bDisableFog = !g_Config.bDisableFog;
								break;

							default:
								break;
						}
					}
					if (g_Config.bFreeLook)
					{
						static float debugSpeed = 1.0f;
						switch (key)
						{
							case XK_parenleft:
								debugSpeed /= 2.0f;
								break;
							case XK_parenright:
								debugSpeed *= 2.0f;
								break;
							case XK_w:
								VertexShaderManager::TranslateView(0.0f, debugSpeed);
								break;
							case XK_s:
								VertexShaderManager::TranslateView(0.0f, -debugSpeed);
								break;
							case XK_a:
								VertexShaderManager::TranslateView(debugSpeed, 0.0f);
								break;
							case XK_d:
								VertexShaderManager::TranslateView(-debugSpeed, 0.0f);
								break;
							case XK_r:
								VertexShaderManager::ResetView();
								break;
						}
					}
					break;
				case ButtonPress:
					if (g_Config.bFreeLook)
					{
						switch (event.xbutton.button)
						{
							case 2: // Middle button
								lastMouse[0] = event.xbutton.x;
								lastMouse[1] = event.xbutton.y;
								mouseMoveEnabled = true;
								break;
							case 3: // Right button
								lastMouse[0] = event.xbutton.x;
								lastMouse[1] = event.xbutton.y;
								mouseLookEnabled = true;
								break;
						}
					}
					break;
				case ButtonRelease:
					if (g_Config.bFreeLook)
					{
						switch (event.xbutton.button)
						{
							case 2: // Middle button
								mouseMoveEnabled = false;
								break;
							case 3: // Right button
								mouseLookEnabled = false;
								break;
						}
					}
					break;
				case MotionNotify:
					if (g_Config.bFreeLook)
					{
						if (mouseLookEnabled)
						{
							VertexShaderManager::RotateView((event.xmotion.x - lastMouse[0]) / 200.0f,
									(event.xmotion.y - lastMouse[1]) / 200.0f);
							lastMouse[0] = event.xmotion.x;
							lastMouse[1] = event.xmotion.y;
						}

						if (mouseMoveEnabled)
						{
							VertexShaderManager::TranslateView((event.xmotion.x - lastMouse[0]) / 50.0f,
									(event.xmotion.y - lastMouse[1]) / 50.0f);
							lastMouse[0] = event.xmotion.x;
							lastMouse[1] = event.xmotion.y;
						}
					}
					break;
				case ConfigureNotify:
					Window winDummy;
					unsigned int borderDummy, depthDummy;
					XGetGeometry(GLWin.evdpy, GLWin.win, &winDummy, &GLWin.x, &GLWin.y,
							&GLWin.width, &GLWin.height, &borderDummy, &depthDummy);
					GLInterface->SetBackBufferDimensions(GLWin.width, GLWin.height);
					break;
				case ClientMessage:
					if ((unsigned long) event.xclient.data.l[0] ==
							XInternAtom(GLWin.evdpy, "WM_DELETE_WINDOW", False))
						Host_Message(WM_USER_STOP);
					if ((unsigned long) event.xclient.data.l[0] ==
							XInternAtom(GLWin.evdpy, "RESIZE", False))
						XMoveResizeWindow(GLWin.evdpy, GLWin.win,
								event.xclient.data.l[1], event.xclient.data.l[2],
								event.xclient.data.l[3], event.xclient.data.l[4]);
					break;
				default:
					break;
			}
		}
		Common::SleepCurrentThread(20);
	}
}
