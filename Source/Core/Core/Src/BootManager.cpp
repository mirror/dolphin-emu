// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


// File description
// -------------
// Purpose of this file: Collect boot settings for Core::Init()

// Call sequence: This file has one of the first function called when a game is booted,
// the boot sequence in the code is:
  
// DolphinWX:    FrameTools.cpp         StartGame
// Core          BootManager.cpp        BootCore
//               Core.cpp               Init                     Thread creation
//                                      EmuThread                Calls CBoot::BootUp
//               Boot.cpp               CBoot::BootUp()
//                                      CBoot::EmulatedBS2_Wii() / GC() or Load_BS2()


// Includes
// ----------------
#include <string>
#include <vector>

#include "Common.h"
#include "IniFile.h"
#include "BootManager.h"
#include "Volume.h"
#include "VolumeCreator.h"
#include "ConfigManager.h"
#include "SysConf.h"
#include "Core.h"
#include "Host.h"
#include "VideoBackendBase.h"
#include "Movie.h"

namespace BootManager
{

// TODO this is an ugly hack which allows us to restore values trampled by per-game settings
// Apply fire liberally
struct ConfigCache
{
	bool valid, bCPUThread, bSkipIdle, bEnableFPRF, bMMU, bDCBZOFF,
		bVBeamSpeedHack, bSyncGPU, bFastDiscSpeed, bMergeBlocks, bDSPHLE, bHLE_BS2;
	int iTLBHack, iCPUCore;
	std::string strBackend;
};

// Boot the ISO or file
bool BootCore(const std::string& _rFilename)
{
	SCoreStartupParameter& StartUp = SConfig::GetInstance().m_LocalCoreStartupParameter;

	// Use custom settings for debugging mode
	Host_SetStartupDebuggingParameters();

	StartUp.m_BootType = SCoreStartupParameter::BOOT_ISO;
	StartUp.m_strFilename = _rFilename;
	SConfig::GetInstance().m_LastFilename = _rFilename;
	StartUp.bRunCompareClient = false;
	StartUp.bRunCompareServer = false;

	StartUp.hInstance = Host_GetInstance();

	// If for example the ISO file is bad we return here
	if (!StartUp.AutoSetup(SCoreStartupParameter::BOOT_DEFAULT))
		return false;

	// Load game specific settings
	IniFile game_ini;
	std::string unique_id = StartUp.GetUniqueID();
	StartUp.m_strGameIni = File::GetUserPath(D_GAMECONFIG_IDX) + unique_id + ".ini";
	if (unique_id.size() == 6 && game_ini.Load(StartUp.m_strGameIni.c_str()))
	{
		// General settings
		game_ini.Get("Core", "CPUThread",			&StartUp.bCPUThread, StartUp.bCPUThread);
		game_ini.Get("Core", "SkipIdle",			&StartUp.bSkipIdle, StartUp.bSkipIdle);
		game_ini.Get("Core", "EnableFPRF",			&StartUp.bEnableFPRF, StartUp.bEnableFPRF);
		game_ini.Get("Core", "MMU",					&StartUp.bMMU, StartUp.bMMU);
		game_ini.Get("Core", "TLBHack",				&StartUp.iTLBHack, StartUp.iTLBHack);
		game_ini.Get("Core", "DCBZ",				&StartUp.bDCBZOFF, StartUp.bDCBZOFF);
		game_ini.Get("Core", "VBeam",				&StartUp.bVBeamSpeedHack, StartUp.bVBeamSpeedHack);
		game_ini.Get("Core", "SyncGPU",				&StartUp.bSyncGPU, StartUp.bSyncGPU);
		game_ini.Get("Core", "FastDiscSpeed",		&StartUp.bFastDiscSpeed, StartUp.bFastDiscSpeed);
		game_ini.Get("Core", "BlockMerging",		&StartUp.bMergeBlocks, StartUp.bMergeBlocks);
		game_ini.Get("Core", "DSPHLE",				&StartUp.bDSPHLE, StartUp.bDSPHLE);
		game_ini.Get("Core", "GFXBackend", &StartUp.m_strVideoBackend, StartUp.m_strVideoBackend.c_str());
		game_ini.Get("Core", "CPUCore",				&StartUp.iCPUCore, StartUp.iCPUCore);
		game_ini.Get("Core", "HLE_BS2",				&StartUp.bHLE_BS2, StartUp.bHLE_BS2);
		VideoBackend::ActivateBackend(StartUp.m_strVideoBackend);

		if (Movie::IsPlayingInput() && Movie::IsConfigSaved())
		{
			StartUp.bCPUThread = Movie::IsDualCore();
			StartUp.bSkipIdle = Movie::IsSkipIdle();
			StartUp.bDSPHLE = Movie::IsDSPHLE();
			StartUp.bProgressive = Movie::IsProgressive();
			StartUp.bFastDiscSpeed = Movie::IsFastDiscSpeed();
			StartUp.iCPUCore = Movie::GetCPUMode();
			if (Movie::IsUsingMemcard() && Movie::IsStartingFromClearSave() && !StartUp.bWii)
			{
				if (File::Exists("Movie.raw"))
					File::Delete("Movie.raw");
			}
		}
		// Wii settings
		if (StartUp.bWii)
		{
			// Flush possible changes to SYSCONF to file
			SConfig::GetInstance().m_SYSCONF->Save();
		}
	} 

	// Run the game
	// Init the core
	if (!Core::Init())
	{
		PanicAlertT("Couldn't init the core.\nCheck your configuration.");
		return false;
	}

	return true;
}

void Stop()
{
	Core::Stop();
	SConfig::Shutdown();
	SConfig::Init();
}

} // namespace
