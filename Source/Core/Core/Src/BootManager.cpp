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
#include "CommonPaths.h"
#include "FileSearch.h"
#include "FileUtil.h"
#include "IniFile.h"
#include "BootManager.h"
#include "Volume.h"
#include "VolumeCreator.h"
#include "Movie.h"
#include "ConfigManager.h"
#include "SysConf.h"
#include "Core.h"
#include "Host.h"
#include "VideoBackendBase.h"
#include "Movie.h"
#include "State.h"

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
static ConfigCache config_cache;

// find file in library
bool SearchLibrary(std::string &filename, std::string isoID = "")
{
	bool isoFound = false;

	// Library dirs
	CFileSearch::XStringVector Directories(SConfig::GetInstance().m_ISOFolder);
	if (SConfig::GetInstance().m_RecursiveISOFolder)
	{
		File::FSTEntry FST_Temp;
		File::ScanDirectoryTreeRecursive(Directories, FST_Temp);
	}

	// Search library paths
	for (size_t i = 0; i < Directories.size(); i++)
	{
		std::string path = Directories[i] + DIR_SEP + filename;
		if (File::Exists(path))
		{
			filename = path;
			isoFound = true;
			break;
		}
	}

	// Find library file by ISO ID
	if (!isoID.empty())
	{
		CFileSearch::XStringVector Extensions;
		Extensions.push_back("*.ciso");
		Extensions.push_back("*.gcm");
		Extensions.push_back("*.gcz");
		Extensions.push_back("*.iso");
		Extensions.push_back("*.wad");
		Extensions.push_back("*.wbfs");
		CFileSearch FileSearch(Extensions, Directories);

		const CFileSearch::XStringVector& libraryFiles = FileSearch.GetFileNames();

		for (u32 i = 0; i < libraryFiles.size(); i++)
		{
			DiscIO::IVolume* pVolume = DiscIO::CreateVolumeFromFilename(libraryFiles[i]);
			if (pVolume->GetUniqueID().find(isoID) != std::string::npos)
			{
				filename = libraryFiles[i];
				isoFound = true;
				break;
			}
		}
	}
	return isoFound;
}

// find ISO file for state
bool GetSaveStateISO(std::string &filename)
{
	std::string isoID;
	if (!State::GetISOID(isoID, filename)) return false;

	if (!SearchLibrary(filename, isoID))
	{
		PanicAlertT("The ISO file %s for %s is not in the library.", isoID.c_str(), filename.c_str());
		return false;
	}

	return true;
}

// find ISO file for movie
bool GetRecordingISO(std::string &filename)
{
	std::string isoID;
	if (!Movie::GetISOID(isoID)) return false;

	if (!SearchLibrary(filename, isoID))
	{
		PanicAlertT("The ISO file %s for %s is not in the library.", isoID.c_str(), filename.c_str());
		return false;
	}

	return true;
}

// compare recording and savestate ISO ID
bool GetISOIDMatch(std::string filenameRec, std::string filenameState)
{
	std::string isoIDRec;
	if (!Movie::GetISOID(isoIDRec)) return false;
	std::string isoIDState;
	if (!State::GetISOID(isoIDState, filenameState)) return false;

	if (isoIDRec.compare(isoIDState))
	{
		PanicAlertT("The %s ISO ID %s doesn't match the %s ISO ID %s.", filenameRec.c_str(),  isoIDRec.c_str(), filenameState.c_str(), isoIDState.c_str());
		return false;
	}

	return true;
}

// Boot the ISO or file
bool BootCore(const std::string& _rFilename)
{
	SCoreStartupParameter& StartUp = SConfig::GetInstance().m_LocalCoreStartupParameter;

	// Use custom settings for debugging mode
	Host_SetStartupDebuggingParameters();

	StartUp.m_BootType = SCoreStartupParameter::BOOT_ISO;

	std::string rFilename = _rFilename;
	std::string Extension;
	SplitPath(_rFilename, NULL, NULL, &Extension);

	// State
	if (!strncasecmp(Extension.c_str(), ".s", 2))
	{
		// find the file
		if (!File::SearchStateDir(rFilename)) return false;
		std::string stateFilename = rFilename;
		if (!State::IsCorrectVersion(stateFilename)) return false;
		if (!GetSaveStateISO(rFilename)) return false;
		// schedule savestate
		Core::SetStateFileName(stateFilename);
	}

	// Movie
	if (!strcasecmp(Extension.c_str(), ".dtm"))
	{
		// find the file
		if (!File::SearchStateDir(rFilename)) return false;

		if (!Movie::PlayInput(rFilename.c_str()))
		{
			PanicAlertT("Can't play %s.", rFilename.c_str());
			return false;
		}

		// the recording require a savestate
		if (Movie::IsRecordingInputFromSaveState())
		{
			std::string recordingFilename = rFilename;
			if (!State::IsCorrectVersion(recordingFilename + ".sav")) return false;
			if (!GetRecordingISO(rFilename)) return false;
			if (!GetISOIDMatch(recordingFilename, recordingFilename + ".sav")) return false;
		}
		else
		{
			if (!GetRecordingISO(rFilename)) return false;
		}
	}

	// FIFO movie
	if (!strcasecmp(Extension.c_str(), ".dff"))
	{
		// find the file
		if (!File::SearchStateDir(rFilename)) return false;
	}

	// Determine action for empty filename
	if (_rFilename.empty())
	{
		if (File::Exists(StartUp.m_strDefaultGCM))
			rFilename = StartUp.m_strDefaultGCM;
		else if (File::Exists(SConfig::GetInstance().m_LastFilename))
			rFilename = SConfig::GetInstance().m_LastFilename;
	}
	else
		SConfig::GetInstance().m_LastFilename = _rFilename;

	if (rFilename.empty())
	{
			PanicAlertT("No ISO file provided and no default or previous ISO file is set.");
			return false;
	}

	// Search library if the file doesn't exist
	if (!File::Exists(rFilename)) SearchLibrary(rFilename);

	if (!File::Exists(rFilename))
	{
		PanicAlertT("Can't find %s.", rFilename.c_str());
		return false;
	}

	SConfig::GetInstance().SaveSettings();
	StartUp.m_strFilename = rFilename;
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
		config_cache.valid = true;
		config_cache.bCPUThread = StartUp.bCPUThread;
		config_cache.bSkipIdle = StartUp.bSkipIdle;
		config_cache.iCPUCore = StartUp.iCPUCore;
		config_cache.bEnableFPRF = StartUp.bEnableFPRF;
		config_cache.bMMU = StartUp.bMMU;
		config_cache.bDCBZOFF = StartUp.bDCBZOFF;
		config_cache.iTLBHack = StartUp.iTLBHack;
		config_cache.bVBeamSpeedHack = StartUp.bVBeamSpeedHack;
		config_cache.bSyncGPU = StartUp.bSyncGPU;
		config_cache.bFastDiscSpeed = StartUp.bFastDiscSpeed;
		config_cache.bMergeBlocks = StartUp.bMergeBlocks;
		config_cache.bDSPHLE = StartUp.bDSPHLE;
		config_cache.strBackend = StartUp.m_strVideoBackend;
		config_cache.bHLE_BS2 = StartUp.bHLE_BS2;

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

		// Wii settings
		if (StartUp.bWii)
		{
			// Flush possible changes to SYSCONF to file
			SConfig::GetInstance().m_SYSCONF->Save();
		}
	} 

	// movie settings
	if (Movie::IsPlayingInput() && Movie::IsConfigSaved())
	{
		StartUp.bCPUThread = Movie::IsDualCore();
		StartUp.bSkipIdle = Movie::IsSkipIdle();
		StartUp.bDSPHLE = Movie::IsDSPHLE();
		StartUp.bProgressive = Movie::IsProgressive();
		StartUp.bFastDiscSpeed = Movie::IsFastDiscSpeed();
		StartUp.iCPUCore = Movie::GetCPUMode();
		StartUp.bSyncGPU = Movie::IsSyncGPU();
		if (Movie::IsUsingMemcard() && Movie::IsStartingFromClearSave() && !StartUp.bWii)
		{
			if (File::Exists(File::GetUserPath(D_GCUSER_IDX) + "Movie.raw"))
				File::Delete(File::GetUserPath(D_GCUSER_IDX) + "Movie.raw");
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

	SCoreStartupParameter& StartUp = SConfig::GetInstance().m_LocalCoreStartupParameter;

	StartUp.m_strUniqueID = "00000000";
	if (config_cache.valid)
	{
		config_cache.valid = false;
		StartUp.bCPUThread = config_cache.bCPUThread;
		StartUp.bSkipIdle = config_cache.bSkipIdle;
		StartUp.iCPUCore = config_cache.iCPUCore;
		StartUp.bEnableFPRF = config_cache.bEnableFPRF;
		StartUp.bMMU = config_cache.bMMU;
		StartUp.bDCBZOFF = config_cache.bDCBZOFF;
		StartUp.iTLBHack = config_cache.iTLBHack;
		StartUp.bVBeamSpeedHack = config_cache.bVBeamSpeedHack;
		StartUp.bSyncGPU = config_cache.bSyncGPU;
		StartUp.bFastDiscSpeed = config_cache.bFastDiscSpeed;
		StartUp.bMergeBlocks = config_cache.bMergeBlocks;
		StartUp.bDSPHLE = config_cache.bDSPHLE;
		StartUp.m_strVideoBackend = config_cache.strBackend;
		VideoBackend::ActivateBackend(StartUp.m_strVideoBackend);
		StartUp.bHLE_BS2 = config_cache.bHLE_BS2;
	}
}

} // namespace
