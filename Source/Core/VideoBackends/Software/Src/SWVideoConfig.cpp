// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "FileUtil.h"
#include "IniFile.h"
#include "SWVideoConfig.h"

SWVideoConfig g_SWVideoConfig;

SWVideoConfig::SWVideoConfig()
{
	bFullscreen = false;
	bHideCursor = false;
	renderToMainframe = false;	

	bHwRasterizer = false;

	bShowStats = false;

	bDumpTextures = false;
	bDumpObjects = false;
	bDumpFrames = false;

	bZComploc = true;
	bZFreeze = true;

	bDumpTevStages = false;
	bDumpTevTextureFetches = false;

	drawStart = 0;
	drawEnd = 100000;
}

void SWVideoConfig::Load()
{
	IniFile iniFile;
	iniFile.Load(File::GetUserPath(F_DOLPHINCONFIG_IDX));

	iniFile.Get("Software_Renderer", "Fullscreen", &bFullscreen, 0); // Hardware
	iniFile.Get("Software_Renderer", "RenderToMainframe", &renderToMainframe, false);

	iniFile.Get("Software_Renderer", "HwRasterizer", &bHwRasterizer, false);
	iniFile.Get("Software_Renderer", "ZComploc", &bZComploc, true);
	iniFile.Get("Software_Renderer", "ZFreeze", &bZFreeze, true);

	iniFile.Get("Software_Renderer", "ShowStats", &bShowStats, false);

	iniFile.Get("Software_Renderer", "DumpTexture", &bDumpTextures, false);
	iniFile.Get("Software_Renderer", "DumpObjects", &bDumpObjects, false);
	iniFile.Get("Software_Renderer", "DumpFrames", &bDumpFrames, false);
	iniFile.Get("Software_Renderer", "DumpTevStages", &bDumpTevStages, false);
	iniFile.Get("Software_Renderer", "DumpTevTexFetches", &bDumpTevTextureFetches, false);

	iniFile.Get("Software_Renderer", "DrawStart", &drawStart, 0);
	iniFile.Get("Software_Renderer", "DrawEnd", &drawEnd, 100000);
}

void SWVideoConfig::Save()
{
	IniFile iniFile;
	iniFile.Load(File::GetUserPath(F_DOLPHINCONFIG_IDX));

	iniFile.Set("Software_Renderer", "Fullscreen", bFullscreen);
	iniFile.Set("Software_Renderer", "RenderToMainframe", renderToMainframe);

	iniFile.Set("Software_Renderer", "HwRasterizer", bHwRasterizer);
	iniFile.Set("Software_Renderer", "ZComploc", &bZComploc);
	iniFile.Set("Software_Renderer", "ZFreeze", &bZFreeze);

	iniFile.Set("Software_Renderer", "ShowStats", bShowStats);

	iniFile.Set("Software_Renderer", "DumpTexture", bDumpTextures);
	iniFile.Set("Software_Renderer", "DumpObjects", bDumpObjects);
	iniFile.Set("Software_Renderer", "DumpFrames", bDumpFrames);
	iniFile.Set("Software_Renderer", "DumpTevStages", bDumpTevStages);
	iniFile.Set("Software_Renderer", "DumpTevTexFetches", bDumpTevTextureFetches);

	iniFile.Set("Software_Renderer", "DrawStart", drawStart);
	iniFile.Set("Software_Renderer", "DrawEnd", drawEnd);

	iniFile.Save(File::GetUserPath(F_DOLPHINCONFIG_IDX));
}

