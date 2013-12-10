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

#include "VideoConfig.h"
#include "Host.h"
#include "RenderBase.h"
#include "ConfigManager.h"

#include <wx/panel.h>

#include "VertexShaderManager.h"
#include "../GLInterface.h"
#include "AGL.h"

void cInterfaceAGL::Swap()
{
	[cocoaCtx flushBuffer];
}

// Create rendering window.
//		Call browser: Core.cpp:EmuThread() > main.cpp:Video_Initialize()
bool cInterfaceAGL::Create(void *&window_handle)
{
	int _tx, _ty, _twidth, _theight;
	Host_GetRenderWindowSize(_tx, _ty, _twidth, _theight);

	cocoaWin = (NSView*)(((wxPanel*)window_handle)->GetHandle());

	// Enable high-resolution display support.
	[cocoaWin setWantsBestResolutionOpenGLSurface:YES];

	NSWindow *window = [cocoaWin window];

	float scale = [window backingScaleFactor];
	_twidth *= scale;
	_theight *= scale;

	// Control window size and picture scaling
	s_backbuffer_width = _twidth;
	s_backbuffer_height = _theight;

	NSOpenGLPixelFormatAttribute attr[] = { NSOpenGLPFADoubleBuffer, NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core, NSOpenGLPFAAccelerated, 0 };
	NSOpenGLPixelFormat *fmt = [[NSOpenGLPixelFormat alloc]
		initWithAttributes: attr];
	if (fmt == nil) {
		ERROR_LOG(VIDEO, "failed to create pixel format");
		return false;
	}

	cocoaCtx = [[NSOpenGLContext alloc]
		initWithFormat: fmt shareContext: nil];
	[fmt release];
	if (cocoaCtx == nil) {
		ERROR_LOG(VIDEO, "failed to create context");
		return false;
	}

	if (cocoaWin == nil) {
		ERROR_LOG(VIDEO, "failed to create window");
		return false;
	}

	[window makeFirstResponder:cocoaWin];
	[cocoaCtx setView: cocoaWin];
	[window makeKeyAndOrderFront: nil];

	return true;
}

bool cInterfaceAGL::MakeCurrent()
{
	[cocoaCtx makeCurrentContext];
	return true;
}

bool cInterfaceAGL::ClearCurrent()
{
	// not tested at all
	//clearCurrentContext();
	return true;
}

// Close backend
void cInterfaceAGL::Shutdown()
{
	[cocoaCtx clearDrawable];
	[cocoaCtx release];
	cocoaCtx = nil;
}

void cInterfaceAGL::Update()
{
	NSWindow *window = [cocoaWin window];
	NSSize size = [cocoaWin frame].size;

	float scale = [window backingScaleFactor];
	size.width *= scale;
	size.height *= scale;

	if( s_backbuffer_width == size.width
	   && s_backbuffer_height == size.height)
		return;

	s_backbuffer_width = size.width;
	s_backbuffer_height = size.height;

	[cocoaCtx update];
}


