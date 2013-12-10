// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _GLINTERFACE_H_
#define _GLINTERFACE_H_

#include "GLInterface/InterfaceBase.h"

#if USE_EGL
#include "GLInterface/Platform.h"
#else

#include "Thread.h"
#ifdef ANDROID
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include "GLInterface/EGL.h"
#elif defined(USE_EGL) && USE_EGL
#include "GLInterface/EGL.h"
#elif defined(__APPLE__)
#include "GLInterface/AGL.h"
#elif defined(_WIN32)
#include "GLInterface/WGL.h"
#elif defined(HAVE_X11) && HAVE_X11
#include "GLInterface/GLX.h"
#else
#error Platform doesnt have a GLInterface
#endif

typedef struct {
#if defined(HAVE_X11) && HAVE_X11
	int screen;
	Window win;
	Window parent;
	// dpy used for glx stuff, evdpy for window events etc.
	// evdpy is to be used by XEventThread only
	Display *evdpy;
	XVisualInfo *vi;
	int x, y;
	unsigned int width, height;
#endif
} GLWindow;

#endif

extern cInterfaceBase *GLInterface;
extern GLWindow GLWin;

void InitInterface();

#endif
