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

// ---------------------------------------------------------------------------------------------
// GC graphics pipeline
// ---------------------------------------------------------------------------------------------
// 3d commands are issued through the fifo. The gpu draws to the 2MB EFB.
// The efb can be copied back into ram in two forms: as textures or as XFB.
// The XFB is the region in RAM that the VI chip scans out to the television.
// So, after all rendering to EFB is done, the image is copied into one of two XFBs in RAM.
// Next frame, that one is scanned out and the other one gets the copy. = double buffering.
// ---------------------------------------------------------------------------------------------


#include "RenderBase.h"
#include "Atomic.h"
#include "BPMemory.h"
#include "CommandProcessor.h"
#include "CPMemory.h"
#include "MainBase.h"
#include "VideoConfig.h"
#include "FramebufferManagerBase.h"
#include "TextureCacheBase.h"
#include "Fifo.h"
#include "OnScreenDisplay.h"
#include "OpcodeDecoding.h"
#include "Timer.h"
#include "StringUtil.h"
#include "Host.h"
#include "XFMemory.h"
#include "FifoPlayer/FifoRecorder.h"
#include "AVIDump.h"

#include <cmath>
#include <string>

// TODO: Move these out of here.
int frameCount;
int HotkeyChoice, HotkeyTime;

Renderer *g_renderer = NULL;

std::mutex Renderer::s_criticalScreenshot;
std::string Renderer::s_sScreenshotName;

volatile bool Renderer::s_bScreenshot;

// The framebuffer size
int Renderer::s_target_width;
int Renderer::s_target_height;

// TODO: Add functionality to reinit all the render targets when the window is resized.
int Renderer::s_backbuffer_width;
int Renderer::s_backbuffer_height;

// ratio of backbuffer size and render area size
float Renderer::xScale;
float Renderer::yScale;

unsigned int Renderer::s_XFB_width;
unsigned int Renderer::s_XFB_height;

int Renderer::s_LastEFBScale;

bool Renderer::s_skipSwap;
bool Renderer::XFBWrited;
bool Renderer::s_EnableDLCachingAfterRecording;

unsigned int Renderer::prev_efb_format = (unsigned int)-1;

Renderer::Renderer() : frame_data(NULL), bLastFrameDumped(false)
{
	UpdateActiveConfig();

#if defined _WIN32 || defined HAVE_LIBAV
	bAVIDumping = false;
#endif
}

Renderer::~Renderer()
{
	// invalidate previous efb format
	prev_efb_format = (unsigned int)-1;

#if defined _WIN32 || defined HAVE_LIBAV
	if (g_ActiveConfig.bDumpFrames && bLastFrameDumped && bAVIDumping)
		AVIDump::Stop();
#else
	if (pFrameDump.IsOpen())
		pFrameDump.Close();
#endif
	delete[] frame_data;
}

void Renderer::RenderToXFB(u32 xfbAddr, u32 fbWidth, u32 fbHeight, const EFBRectangle& sourceRc, float Gamma)
{
	CheckFifoRecording();

	if (!fbWidth || !fbHeight)
		return;

	s_skipSwap = g_bSkipCurrentFrame;

	VideoFifo_CheckEFBAccess();
	VideoFifo_CheckSwapRequestAt(xfbAddr, fbWidth, fbHeight);
	XFBWrited = true;

	// XXX: Without the VI, how would we know what kind of field this is? So
	// just use progressive.
	if (g_ActiveConfig.bUseXFB)
	{
		FramebufferManagerBase::CopyToXFB(xfbAddr, fbWidth, fbHeight, sourceRc,Gamma);
	}
	else
	{
		g_renderer->Swap(xfbAddr, FIELD_PROGRESSIVE, fbWidth, fbHeight,sourceRc,Gamma);
		Common::AtomicStoreRelease(s_swapRequested, false);
	}
	
	if (TextureCache::DeferredInvalidate)
	{
		TextureCache::Invalidate(false);
	}
}

void Renderer::CalculateTargetScale(int x, int y, int &scaledX, int &scaledY)
{
	switch (g_ActiveConfig.iEFBScale)
	{
		case 3: // 1.5x
			scaledX = (x / 2) * 3;
			scaledY = (y / 2) * 3;
			break;
		case 4: // 2x
			scaledX = x * 2;
			scaledY = y * 2;
			break;
		case 5: // 2.5x
			scaledX = (x / 2) * 5;
			scaledY = (y / 2) * 5;
			break;
		case 6: // 3x
			scaledX = x * 3;
			scaledY = y * 3;
			break;
		case 7: // 4x
			scaledX = x * 4;
			scaledY = y * 4;
			break;
		default:
			scaledX = x;
			scaledY = y;
			break;
	};
}

// return true if target size changed
bool Renderer::CalculateTargetSize(int multiplier)
{
	int newEFBWidth, newEFBHeight;
	switch (s_LastEFBScale)
	{
		case 0: // fractional
			newEFBWidth = (int)(EFB_WIDTH * xScale);
			newEFBHeight = (int)(EFB_HEIGHT * yScale);
			break;
		case 1: // integral
			newEFBWidth = EFB_WIDTH * (int)ceilf(xScale);
			newEFBHeight = EFB_HEIGHT * (int)ceilf(yScale);
			break;
		default:
			CalculateTargetScale(EFB_WIDTH, EFB_HEIGHT, newEFBWidth, newEFBHeight);
			break;
	}

	newEFBWidth *= multiplier;
	newEFBHeight *= multiplier;

	if (newEFBWidth != s_target_width || newEFBHeight != s_target_height)
	{
		s_target_width  = newEFBWidth;
		s_target_height = newEFBHeight;
		return true;
	}
	return false;
}

void Renderer::SetScreenshot(const char *filename)
{
	std::lock_guard<std::mutex> lk(s_criticalScreenshot);
	s_sScreenshotName = filename;
	s_bScreenshot = true;
}

// Create On-Screen-Messages
void Renderer::DrawDebugText()
{
	if (!g_Config.bHotKey) return;

	static ControlState lastState[VideoConfig::INPUT_SIZE] = {0};

	for (int i = 0; i < VideoConfig::INPUT_SIZE; ++i)
	{
		ControlState state = g_Config.controls[i]->control_ref->State();

		if (state == lastState[i]) continue;
		lastState[i] = state;
		if (!state) continue;

		switch(i)
		{
		case VideoConfig::INPUT_AR: g_Config.iAspectRatio = (g_Config.iAspectRatio + 1) & 3; break;
		case VideoConfig::INPUT_FPS: g_Config.bShowFPS = !g_Config.bShowFPS; break;
		case VideoConfig::INPUT_EFB_ACCESS: g_Config.bEFBAccessEnable = !g_Config.bEFBAccessEnable; break;
		case VideoConfig::INPUT_EFB_COPY:
			!g_Config.bEFBCopyEnable ? (g_Config.bEFBCopyEnable = g_Config.bCopyEFBToTexture = true)
				: (g_Config.bCopyEFBToTexture ? g_Config.bCopyEFBToTexture = false : g_Config.bEFBCopyEnable = false);
			break;
		case VideoConfig::INPUT_EFB_SCALE:
			g_Config.iEFBScale++;
			if (g_Config.iEFBScale > 7) g_Config.iEFBScale = 0;
			break;
		case VideoConfig::INPUT_XFB:
			!g_Config.bUseXFB ? (g_Config.bUseXFB = true, g_Config.bUseRealXFB = false)
				: (!g_Config.bUseRealXFB ? g_Config.bUseRealXFB = true : g_Config.bUseXFB = false);
			break;
		case VideoConfig::INPUT_FOG: g_Config.bDisableFog = !g_Config.bDisableFog; break;
		// TODO: Not implemented in the D3D backends, yet
		case VideoConfig::INPUT_LIGHTING: g_Config.bDisableLighting = !g_Config.bDisableLighting; break;
		case VideoConfig::INPUT_WIREFRAME: g_Config.bWireFrame = !g_Config.bWireFrame; break;
		}

		const char* ar_text = "";
		switch(g_Config.iAspectRatio)
		{
		case ASPECT_AUTO: ar_text = "Auto"; break;
		case ASPECT_FORCE_16_9: ar_text = "16:9"; break;
		case ASPECT_FORCE_4_3: ar_text = "4:3"; break;
		case ASPECT_STRETCH: ar_text = "Stretch"; break;
		}

		const char* res_text = "";
		switch (g_Config.iEFBScale)
		{
		case 0: res_text = "Auto (fractional)"; break;
		case 1: res_text = "Auto (integral)"; break;
		case 2: res_text = "Native"; break;
		case 3: res_text = "1.5x"; break;
		case 4: res_text = "2x"; break;
		case 5: res_text = "2.5x"; break;
		case 6: res_text = "3x"; break;
		case 7: res_text = "4x"; break;
		}

		const char* const efbcopy_text = g_Config.bEFBCopyEnable ?
			(g_Config.bCopyEFBToTexture ? "Texture" : "RAM") : "Disabled";

		const char* const xfb_text = g_Config.bUseXFB ?
			(g_Config.bUseRealXFB ? "Real" : "Virtual") : "Disabled";

		std::string line;
		bool bEnabled;
		switch(i)
		{
		case VideoConfig::INPUT_AR: line = std::string("Aspect Ratio: ") + ar_text + (g_Config.bCrop ? " (crop)" : ""); bEnabled = true; break;
		case VideoConfig::INPUT_FPS: line = std::string("Show FPS: ") +  (g_Config.bShowFPS ? "Enabled" : "Disabled"); bEnabled = g_Config.bShowFPS; break;
		case VideoConfig::INPUT_EFB_ACCESS: line = std::string("EFB Access: ") + (g_Config.bEFBAccessEnable ? "Enabled" : "Disabled"); bEnabled = g_Config.bEFBAccessEnable; break;
		case VideoConfig::INPUT_EFB_COPY: line = std::string("Copy EFB: ") + efbcopy_text; bEnabled = g_Config.bEFBCopyEnable; break;		
		case VideoConfig::INPUT_EFB_SCALE: line = std::string("Internal Resolution: ") + res_text; bEnabled = true; break;
		case VideoConfig::INPUT_XFB: line = std::string("XFB: ") + xfb_text; bEnabled = g_Config.bUseXFB; break;
		case VideoConfig::INPUT_FOG: line = std::string("Fog: ") + (g_Config.bDisableFog ? "Disabled" : "Enabled"); bEnabled = g_Config.bDisableFog; break;
		case VideoConfig::INPUT_LIGHTING: line = std::string("Material Lighting: ") + (g_Config.bDisableLighting ? "Disabled" : "Enabled"); bEnabled = g_Config.bDisableLighting; break;
		case VideoConfig::INPUT_WIREFRAME: line = std::string("Wireframe: ") + (g_Config.bWireFrame ? "Enabled" : "Disabled"); bEnabled = g_Config.bWireFrame; break;
		}

		// Shadow and text
		OSD::AddMessage(line.c_str(), 3000, (bEnabled ? 0x00FFFF : 0xABABAB));
	}
}

void Renderer::CalculateXYScale(const TargetRectangle& dst_rect)
{
	if (g_ActiveConfig.bUseXFB && g_ActiveConfig.bUseRealXFB)
	{
		xScale = 1.0f;
		yScale = 1.0f;
	}
	else
	{
		if (g_ActiveConfig.b3DVision)
		{
			// This works, yet the version in the else doesn't. No idea why.
			xScale = (float)(s_backbuffer_width-1) / (float)(s_XFB_width-1);
			yScale = (float)(s_backbuffer_height-1) / (float)(s_XFB_height-1);
		}
		else
		{
			xScale = (float)(dst_rect.right - dst_rect.left - 1) / (float)(s_XFB_width-1);
			yScale = (float)(dst_rect.bottom - dst_rect.top - 1) / (float)(s_XFB_height-1);
		}
	}
}

void Renderer::SetWindowSize(int width, int height)
{
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;

	// Scale the window size by the EFB scale.
	CalculateTargetScale(width, height, width, height);

	Host_RequestRenderWindowSize(width, height);
}

void Renderer::CheckFifoRecording()
{
	bool wasRecording = g_bRecordFifoData;
	g_bRecordFifoData = FifoRecorder::GetInstance().IsRecording();

	if (g_bRecordFifoData)
	{
		if (!wasRecording)
		{
			// Disable display list caching because the recorder does not handle it
			s_EnableDLCachingAfterRecording = g_ActiveConfig.bDlistCachingEnable;
			g_ActiveConfig.bDlistCachingEnable = false;

			RecordVideoMemory();
		}

		FifoRecorder::GetInstance().EndFrame(CommandProcessor::fifo.CPBase, CommandProcessor::fifo.CPEnd);
	}
	else if (wasRecording)
	{
		g_ActiveConfig.bDlistCachingEnable = s_EnableDLCachingAfterRecording;
	}
}

void Renderer::RecordVideoMemory()
{
	u32 *bpMem = (u32*)&bpmem;
	u32 cpMem[256];
	u32 *xfMem = (u32*)xfmem;
	u32 *xfRegs = (u32*)&xfregs;

	memset(cpMem, 0, 256 * 4);
	FillCPMemoryArray(cpMem);

	FifoRecorder::GetInstance().SetVideoMemory(bpMem, cpMem, xfMem, xfRegs, sizeof(XFRegisters) / 4);
}

void UpdateViewport(Matrix44& vpCorrection)
{
	g_renderer->UpdateViewport(vpCorrection);
}
