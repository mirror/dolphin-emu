// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "GLInterface.h"

GLWindow GLWin;
cInterfaceBase *GLInterface;

void InitInterface()
{
	#if defined(USE_EGL) && USE_EGL
		GLInterface = new cInterfaceEGL;
	#elif defined(__APPLE__)
		GLInterface = new cInterfaceAGL;
	#elif defined(_WIN32)
		GLInterface = new cInterfaceWGL;
	#elif defined(HAVE_X11) && HAVE_X11
		GLInterface = new cInterfaceGLX;
	#endif
}
