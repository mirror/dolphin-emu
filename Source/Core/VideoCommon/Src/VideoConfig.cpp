// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <cmath>

#include "Common.h"
#include "IniFile.h"
#include "VideoConfig.h"
#include "VideoCommon.h"
#include "FileUtil.h"
#include "Core.h"
#include "Movie.h"
#include "OnScreenDisplay.h"
#include "ConfigManager.h"

VideoConfig g_Config;
VideoConfig g_ActiveConfig;

void UpdateActiveConfig()
{
	if (Movie::IsPlayingInput() && Movie::IsConfigSaved())
		Movie::SetGraphicsConfig();
	g_ActiveConfig = g_Config;
}

VideoConfig::VideoConfig()
{
	bRunning = false;

	// Needed for the first frame, I think
	fAspectRatioHackW = 1;
	fAspectRatioHackH = 1;

	// disable all features by default
	backend_info.APIType = API_NONE;
	backend_info.bUseRGBATextures = false;
	backend_info.bUseMinimalMipCount = false;
	backend_info.bSupports3DVision = false;
}

void VideoConfig::Load()
{
	IniFile iniFile;
	iniFile.Load(File::GetUserPath(F_DOLPHINCONFIG_IDX));

	// Hardware
	iniFile.Get("Video_Settings", "Adapter", &iAdapter, 0);
	iniFile.Get("Video_Settings", "VSync", &bVSync, 0); 

	iniFile.Get("Video_Settings", "wideScreenHack", &bWidescreenHack, false);
	iniFile.Get("Video_Settings", "AspectRatio", &iAspectRatio, (int)ASPECT_AUTO);
	iniFile.Get("Video_Settings", "Crop", &bCrop, false);
	iniFile.Get("Video_Settings", "UseXFB", &bUseXFB, 0);
	iniFile.Get("Video_Settings", "UseRealXFB", &bUseRealXFB, 0);
	iniFile.Get("Video_Settings", "SafeTextureCacheColorSamples", &iSafeTextureCache_ColorSamples,128);
	iniFile.Get("Video_Settings", "ShowFPS", &bShowFPS, false); // Settings
	iniFile.Get("Video_Settings", "LogFPSToFile", &bLogFPSToFile, false);
	iniFile.Get("Video_Settings", "ShowInputDisplay", &bShowInputDisplay, false);
	iniFile.Get("Video_Settings", "OverlayStats", &bOverlayStats, false);
	iniFile.Get("Video_Settings", "OverlayProjStats", &bOverlayProjStats, false);
	iniFile.Get("Video_Settings", "ShowEFBCopyRegions", &bShowEFBCopyRegions, false);
	iniFile.Get("Video_Settings", "DLOptimize", &iCompileDLsLevel, 0);
	iniFile.Get("Video_Settings", "DumpTextures", &bDumpTextures, 0);
	iniFile.Get("Video_Settings", "HiresTextures", &bHiresTextures, 0);
	iniFile.Get("Video_Settings", "DumpEFBTarget", &bDumpEFBTarget, 0);
	iniFile.Get("Video_Settings", "DumpFrames", &bDumpFrames, 0);
	iniFile.Get("Video_Settings", "FreeLook", &bFreeLook, 0);
	iniFile.Get("Video_Settings", "UseFFV1", &bUseFFV1, 0);
	iniFile.Get("Video_Settings", "AnaglyphStereo", &bAnaglyphStereo, false);
	iniFile.Get("Video_Settings", "AnaglyphStereoSeparation", &iAnaglyphStereoSeparation, 200);
	iniFile.Get("Video_Settings", "AnaglyphFocalAngle", &iAnaglyphFocalAngle, 0);
	iniFile.Get("Video_Settings", "EnablePixelLighting", &bEnablePixelLighting, 0);
	iniFile.Get("Video_Settings", "HackedBufferUpload", &bHackedBufferUpload, 0);
	iniFile.Get("Video_Settings", "FastDepthCalc", &bFastDepthCalc, true);

	iniFile.Get("Video_Settings", "AAMode", &iMultisampleMode, 0);
	iniFile.Get("Video_Settings", "AASamples", &iMultisampleSamples, 1);
	iniFile.Get("Video_Settings", "AAQualityLevel", &iMultisampleQualityLevel, 1);

	iniFile.Get("Video_Settings", "EFBScale", &iEFBScale, (int) SCALE_1X); // native

	iniFile.Get("Video_Settings", "DstAlphaPass", &bDstAlphaPass, false);

	iniFile.Get("Video_Settings", "TexFmtOverlayEnable", &bTexFmtOverlayEnable, 0);
	iniFile.Get("Video_Settings", "TexFmtOverlayCenter", &bTexFmtOverlayCenter, 0);
	iniFile.Get("Video_Settings", "WireFrame", &bWireFrame, 0);
	iniFile.Get("Video_Settings", "DisableFog", &bDisableFog, 0);

	iniFile.Get("Video_Settings", "EnableOpenCL", &bEnableOpenCL, false);
	iniFile.Get("Video_Settings", "OMPDecoder", &bOMPDecoder, false);

	iniFile.Get("Video_Settings", "EnableShaderDebugging", &bEnableShaderDebugging, false);

	// Enhancements
	iniFile.Get("Video_Enhancements", "ForceFiltering", &bForceFiltering, 0);
	iniFile.Get("Video_Enhancements", "MaxAnisotropy", &iMaxAnisotropy, 0);  // NOTE - this is x in (1 << x)
	iniFile.Get("Video_Enhancements", "PostProcessingShader", &sPostProcessingShader, "");
	iniFile.Get("Video_Enhancements", "Enable3dVision", &b3DVision, false);
	
	// Hacks
	iniFile.Get("Video_Hacks", "EFBAccessEnable", &bEFBAccessEnable, true);
	iniFile.Get("Video_Hacks", "DlistCachingEnable", &bDlistCachingEnable,false);
	iniFile.Get("Video_Hacks", "EFBCopyEnable", &bEFBCopyEnable, true);
	iniFile.Get("Video_Hacks", "EFBToTextureEnable", &bCopyEFBToTexture, true);
	iniFile.Get("Video_Hacks", "EFBScaledCopy", &bCopyEFBScaled, true);
	iniFile.Get("Video_Hacks", "EFBCopyCacheEnable", &bEFBCopyCacheEnable, false);
	iniFile.Get("Video_Hacks", "EFBEmulateFormatChanges", &bEFBEmulateFormatChanges, false);

	// Load common settings
	bool bUsePanicHandlers;
	iniFile.Get("Interface", "UsePanicHandlers", &bUsePanicHandlers, true);
	SetEnableAlert(bUsePanicHandlers);

	// Shader Debugging causes a huge slowdown and it's easy to forget about it
	// since it's not exposed in the settings dialog. It's only used by
	// developers, so displaying an obnoxious message avoids some confusion and
	// is not too annoying/confusing for users.
	//
	// XXX(delroth): This is kind of a bad place to put this, but the current
	// VideoCommon is a mess and we don't have a central initialization
	// function to do these kind of checks. Instead, the init code is
	// triplicated for each video backend.
	if (bEnableShaderDebugging)
		OSD::AddMessage("Warning: Shader Debugging is enabled, performance will suffer heavily", 15000);
}

void VideoConfig::GameIniLoad()
{
	bool gfx_override_exists = false;

	// XXX: Again, bad place to put OSD messages at (see delroth's comment above)
	// XXX: This will add an OSD message for each projection hack value... meh
#define CHECK_SETTING(section, key, var) do { \
		decltype(var) temp = var; \
		if (iniFile.GetIfExists(section, key, &var) && var != temp) { \
			char buf[256]; \
			snprintf(buf, sizeof(buf), "Note: Option \"%s\" is overridden by game ini.", key); \
			OSD::AddMessage(buf, 7500); \
			gfx_override_exists = true; \
		} \
	} while (0)

	IniFile iniFile = SConfig::GetInstance().m_LocalCoreStartupParameter.LoadGameIni();

	CHECK_SETTING("Video_Hardware", "VSync", bVSync);

	CHECK_SETTING("Video_Settings", "wideScreenHack", bWidescreenHack);
	CHECK_SETTING("Video_Settings", "AspectRatio", iAspectRatio);
	CHECK_SETTING("Video_Settings", "Crop", bCrop);
	CHECK_SETTING("Video_Settings", "UseXFB", bUseXFB);
	CHECK_SETTING("Video_Settings", "UseRealXFB", bUseRealXFB);
	CHECK_SETTING("Video_Settings", "SafeTextureCacheColorSamples", iSafeTextureCache_ColorSamples);
	CHECK_SETTING("Video_Settings", "DLOptimize", iCompileDLsLevel);
	CHECK_SETTING("Video_Settings", "HiresTextures", bHiresTextures);
	CHECK_SETTING("Video_Settings", "AnaglyphStereo", bAnaglyphStereo);
	CHECK_SETTING("Video_Settings", "AnaglyphStereoSeparation", iAnaglyphStereoSeparation);
	CHECK_SETTING("Video_Settings", "AnaglyphFocalAngle", iAnaglyphFocalAngle);
	CHECK_SETTING("Video_Settings", "EnablePixelLighting", bEnablePixelLighting);
	CHECK_SETTING("Video_Settings", "HackedBufferUpload", bHackedBufferUpload);
	CHECK_SETTING("Video_Settings", "FastDepthCalc", bFastDepthCalc);
	CHECK_SETTING("Video_Settings", "AAMode", iMultisampleMode);
	CHECK_SETTING("Video_Settings", "AASamples", iMultisampleSamples);
	CHECK_SETTING("Video_Settings", "AAQualityLevel", iMultisampleQualityLevel);

	int tmp = -9000;
	CHECK_SETTING("Video_Settings", "EFBScale", tmp); // integral
	if (tmp != -9000)
	{
		if (tmp != SCALE_FORCE_INTEGRAL)
		{
			iEFBScale = tmp;
		}
		else // Round down to multiple of native IR
		{
			switch (iEFBScale)
			{
			case SCALE_AUTO:
				iEFBScale = SCALE_AUTO_INTEGRAL;
				break;
			case SCALE_1_5X:
				iEFBScale = SCALE_1X;
				break;
			case SCALE_2_5X:
				iEFBScale = SCALE_2X;
				break;
			default:
				break;
			}
		}
	}

	CHECK_SETTING("Video_Settings", "DstAlphaPass", bDstAlphaPass);
	CHECK_SETTING("Video_Settings", "DisableFog", bDisableFog);
	CHECK_SETTING("Video_Settings", "EnableOpenCL", bEnableOpenCL);
	CHECK_SETTING("Video_Settings", "OMPDecoder", bOMPDecoder);

	CHECK_SETTING("Video_Enhancements", "ForceFiltering", bForceFiltering);
	CHECK_SETTING("Video_Enhancements", "MaxAnisotropy", iMaxAnisotropy);  // NOTE - this is x in (1 << x)
	CHECK_SETTING("Video_Enhancements", "PostProcessingShader", sPostProcessingShader);
	CHECK_SETTING("Video_Enhancements", "Enable3dVision", b3DVision);

	CHECK_SETTING("Video_Hacks", "EFBAccessEnable", bEFBAccessEnable);
	CHECK_SETTING("Video_Hacks", "DlistCachingEnable", bDlistCachingEnable);
	CHECK_SETTING("Video_Hacks", "EFBCopyEnable", bEFBCopyEnable);
	CHECK_SETTING("Video_Hacks", "EFBToTextureEnable", bCopyEFBToTexture);
	CHECK_SETTING("Video_Hacks", "EFBScaledCopy", bCopyEFBScaled);
	CHECK_SETTING("Video_Hacks", "EFBCopyCacheEnable", bEFBCopyCacheEnable);
	CHECK_SETTING("Video_Hacks", "EFBEmulateFormatChanges", bEFBEmulateFormatChanges);

	CHECK_SETTING("Video", "ProjectionHack", iPhackvalue[0]);
	CHECK_SETTING("Video", "PH_SZNear", iPhackvalue[1]);
	CHECK_SETTING("Video", "PH_SZFar", iPhackvalue[2]);
	CHECK_SETTING("Video", "PH_ExtraParam", iPhackvalue[3]);
	CHECK_SETTING("Video", "PH_ZNear", sPhackvalue[0]);
	CHECK_SETTING("Video", "PH_ZFar", sPhackvalue[1]);
	CHECK_SETTING("Video", "ZTPSpeedupHack", bZTPSpeedHack);
	CHECK_SETTING("Video", "UseBBox", bUseBBox);
	CHECK_SETTING("Video", "PerfQueriesEnable", bPerfQueriesEnable);

	if (gfx_override_exists)
		OSD::AddMessage("Warning: Opening the graphics configuration will reset settings and might cause issues!", 10000);
}

void VideoConfig::VerifyValidity()
{
	// TODO: Check iMaxAnisotropy value
	if (iAdapter < 0 || iAdapter > ((int)backend_info.Adapters.size() - 1)) iAdapter = 0;
	if (iMultisampleMode != AA_NONE) // Check to make sure it is valid for the backend selected
	{
		bool Found = false;
		for (unsigned int a = 0; a < backend_info.AAModes.size() && !Found; ++a)
			if (iMultisampleMode == backend_info.AAModes[a].first
				|| iMultisampleSamples == backend_info.AAModes[a].second)
				Found = true;
		if (!Found)
			iMultisampleMode = AA_NONE; // Not in available list, set to none
	}
	if (!backend_info.bSupports3DVision) b3DVision = false;
	if (!backend_info.bSupportsFormatReinterpretation) bEFBEmulateFormatChanges = false;
	if (!backend_info.bSupportsPixelLighting) bEnablePixelLighting = false;
	if (backend_info.APIType != API_OPENGL) backend_info.bSupportsGLSLUBO = false;
}

void VideoConfig::Save()
{
	IniFile iniFile;
	iniFile.Load(File::GetUserPath(F_DOLPHINCONFIG_IDX));
	// Hardware
	iniFile.Set("Video_Settings", "Adapter", iAdapter);
	iniFile.Set("Video_Settings", "VSync", bVSync);

	iniFile.Set("Video_Settings", "AspectRatio", iAspectRatio);
	iniFile.Set("Video_Settings", "Crop", bCrop);
	iniFile.Set("Video_Settings", "wideScreenHack", bWidescreenHack);
	iniFile.Set("Video_Settings", "UseXFB", bUseXFB);
	iniFile.Set("Video_Settings", "UseRealXFB", bUseRealXFB);
	iniFile.Set("Video_Settings", "SafeTextureCacheColorSamples", iSafeTextureCache_ColorSamples);
	iniFile.Set("Video_Settings", "ShowFPS", bShowFPS);
	iniFile.Set("Video_Settings", "LogFPSToFile", bLogFPSToFile);
	iniFile.Set("Video_Settings", "ShowInputDisplay", bShowInputDisplay);
	iniFile.Set("Video_Settings", "OverlayStats", bOverlayStats);
	iniFile.Set("Video_Settings", "OverlayProjStats", bOverlayProjStats);
	iniFile.Set("Video_Settings", "DLOptimize", iCompileDLsLevel);
	iniFile.Set("Video_Settings", "Show", iCompileDLsLevel);
	iniFile.Set("Video_Settings", "DumpTextures", bDumpTextures);
	iniFile.Set("Video_Settings", "HiresTextures", bHiresTextures);
	iniFile.Set("Video_Settings", "DumpEFBTarget", bDumpEFBTarget);
	iniFile.Set("Video_Settings", "DumpFrames", bDumpFrames);
	iniFile.Set("Video_Settings", "FreeLook", bFreeLook);
	iniFile.Set("Video_Settings", "UseFFV1", bUseFFV1);
	iniFile.Set("Video_Settings", "AnaglyphStereo", bAnaglyphStereo);
	iniFile.Set("Video_Settings", "AnaglyphStereoSeparation", iAnaglyphStereoSeparation);
	iniFile.Set("Video_Settings", "AnaglyphFocalAngle", iAnaglyphFocalAngle);
	iniFile.Set("Video_Settings", "EnablePixelLighting", bEnablePixelLighting);
	iniFile.Set("Video_Settings", "HackedBufferUpload", bHackedBufferUpload);
	iniFile.Set("Video_Settings", "FastDepthCalc", bFastDepthCalc);

	iniFile.Set("Video_Settings", "ShowEFBCopyRegions", bShowEFBCopyRegions);
	iniFile.Set("Video_Settings", "AAMode", iMultisampleMode);
	iniFile.Set("Video_Settings", "AASamples", iMultisampleSamples);
	iniFile.Set("Video_Settings", "AAQualityLevel", iMultisampleQualityLevel);

	iniFile.Set("Video_Settings", "EFBScale", iEFBScale);
	iniFile.Set("Video_Settings", "TexFmtOverlayEnable", bTexFmtOverlayEnable);
	iniFile.Set("Video_Settings", "TexFmtOverlayCenter", bTexFmtOverlayCenter);
	iniFile.Set("Video_Settings", "Wireframe", bWireFrame);
	iniFile.Set("Video_Settings", "DstAlphaPass", bDstAlphaPass);
	iniFile.Set("Video_Settings", "DisableFog", bDisableFog);

	iniFile.Set("Video_Settings", "EnableOpenCL", bEnableOpenCL);
	iniFile.Set("Video_Settings", "OMPDecoder", bOMPDecoder);

	iniFile.Set("Video_Settings", "EnableShaderDebugging", bEnableShaderDebugging);

	iniFile.Set("Video_Enhancements", "ForceFiltering", bForceFiltering);
	iniFile.Set("Video_Enhancements", "MaxAnisotropy", iMaxAnisotropy);
	iniFile.Set("Video_Enhancements", "PostProcessingShader", sPostProcessingShader);
	iniFile.Set("Video_Enhancements", "Enable3dVision", b3DVision);

	iniFile.Set("Video_Hacks", "EFBAccessEnable", bEFBAccessEnable);
	iniFile.Set("Video_Hacks", "DlistCachingEnable", bDlistCachingEnable);
	iniFile.Set("Video_Hacks", "EFBCopyEnable", bEFBCopyEnable);
	iniFile.Set("Video_Hacks", "EFBToTextureEnable", bCopyEFBToTexture);
	iniFile.Set("Video_Hacks", "EFBScaledCopy", bCopyEFBScaled);
	iniFile.Set("Video_Hacks", "EFBCopyCacheEnable", bEFBCopyCacheEnable);
	iniFile.Set("Video_Hacks", "EFBEmulateFormatChanges", bEFBEmulateFormatChanges);

	iniFile.Save(File::GetUserPath(F_DOLPHINCONFIG_IDX));
}

bool VideoConfig::IsVSync()
{
	return Core::isTabPressed ? false : bVSync;
}
