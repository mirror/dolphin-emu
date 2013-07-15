// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"

namespace CLI
{

// CLI methods

extern Common::Event updateMainFrameEvent;
extern bool isCLI;
extern bool rendererHasFocus;

void Entry(std::string exec, bool init = false);
#if defined(HAVE_X11) && HAVE_X11
void X11_MainLoop();
#endif

void* GetRenderHandle();
void GetRenderWindowSize(int& x, int& y, int& width, int& height);
void Message(int Id);
void SetStartupDebuggingParameters();
void UpdateMainFrame();

}