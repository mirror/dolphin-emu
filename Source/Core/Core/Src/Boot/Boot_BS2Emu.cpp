// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"
#include "CommonPaths.h"
#include "FileUtil.h"
#include "NandPaths.h"

#include "../PowerPC/PowerPC.h"
#include "../Core.h"
#include "../HW/EXI_DeviceIPL.h"
#include "../HW/Memmap.h"
#include "../HW/DVDInterface.h"
#include "../HW/CPU.h"

#include "../Host.h"
#include "../VolumeHandler.h"
#include "../PatchEngine.h"
#include "../MemTools.h"

#include "../ConfigManager.h"
#include "VolumeCreator.h"
#include "Boot.h"
#include "HLE/HLE.h"

void CBoot::RunFunction(u32 _iAddr)
{
	PC = _iAddr;
	LR = 0x00;

	while (PC != 0x00)
		PowerPC::SingleStep();
}

// __________________________________________________________________________________________________
// GameCube Bootstrap 2 HLE: 
// copy the apploader to 0x81200000
// execute the apploader, function by function, using the above utility.
bool CBoot::EmulatedBS2_GC()
{
	INFO_LOG(BOOT, "Faking GC BS2...");

	UReg_MSR& m_MSR = ((UReg_MSR&)PowerPC::ppcState.msr);
	m_MSR.FP = 1;

	// Clear ALL memory
	Memory::Clear();

	// Write necessary values
	// Here we write values to memory that the apploader does not take care of. Game info goes
	// to 0x80000000 according to yagcd 4.2.
	DVDInterface::DVDRead(0x00000000, 0x80000000, 0x20); // write disc info

	Memory::Write_U32(0x0D15EA5E, 0x80000020);	// booted from bootrom. 0xE5207C22 = booted from jtag
	Memory::Write_U32(Memory::REALRAM_SIZE, 0x80000028);	// Physical Memory Size (24MB on retail)
	// TODO determine why some games fail when using a retail id. (Seem to take different EXI paths, see ikaruga for example)
	Memory::Write_U32(0x10000006, 0x8000002C);	// Console type - DevKit  (retail ID == 0x00000003) see yagcd 4.2.1.1.2

	Memory::Write_U32(SConfig::GetInstance().m_LocalCoreStartupParameter.bNTSC
						 ? 0 : 1, 0x800000CC);	// fake the VI Init of the IPL (yagcd 4.2.1.4)

	Memory::Write_U32(0x01000000, 0x800000d0);	// ARAM Size. 16MB main + 4/16/32MB external (retail consoles have no external ARAM)

	Memory::Write_U32(0x09a7ec80, 0x800000F8);	// Bus Clock Speed
	Memory::Write_U32(0x1cf7c580, 0x800000FC);	// CPU Clock Speed

	Memory::Write_U32(0x4c000064, 0x80000300);	// write default DFI Handler:		rfi
	Memory::Write_U32(0x4c000064, 0x80000800);	// write default FPU Handler:		rfi
	Memory::Write_U32(0x4c000064, 0x80000C00);	// write default Syscall Handler:   rfi

	Memory::Write_U64((u64)CEXIIPL::GetGCTime() * (u64)40500000, 0x800030D8);	// preset time base ticks
	// HIO checks this
	//Memory::Write_U16(0x8200,     0x000030e6);	// Console type

	HLE::Patch(0x81300000, "OSReport"); // HLE OSReport for Apploader

	// Load Apploader to Memory - The apploader is hardcoded to begin at 0x2440 on the disc,
	// but the size can differ between discs. Compare with yagcd chap 13.
	u32 iAppLoaderOffset = 0x2440;
	u32 iAppLoaderEntry	= VolumeHandler::Read32(iAppLoaderOffset + 0x10);
	u32 iAppLoaderSize	= VolumeHandler::Read32(iAppLoaderOffset + 0x14) + VolumeHandler::Read32(iAppLoaderOffset + 0x18);
	if ((iAppLoaderEntry == (u32)-1) || (iAppLoaderSize == (u32)-1))
	{
		INFO_LOG(BOOT, "GC BS2: Not running apploader!");
		return false;
	}
	VolumeHandler::ReadToPtr(Memory::GetPointer(0x81200000), iAppLoaderOffset + 0x20, iAppLoaderSize);

	// Setup pointers like real BS2 does
	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bNTSC)
	{
		PowerPC::ppcState.gpr[1] = 0x81566550;		// StackPointer, used to be set to 0x816ffff0
		PowerPC::ppcState.gpr[2] = 0x81465cc0;		// global pointer to Small Data Area 2 Base (haven't seen anything use it...meh)
		PowerPC::ppcState.gpr[13] = 0x81465320;		// global pointer to Small Data Area Base (Luigi's Mansion's apploader uses it)
	}
	else
	{
		PowerPC::ppcState.gpr[1] = 0x815edca8;
		PowerPC::ppcState.gpr[2] = 0x814b5b20;
		PowerPC::ppcState.gpr[13] = 0x814b4fc0;
	}

	// TODO - Make Apploader(or just RunFunction()) debuggable!!!

	// Call iAppLoaderEntry.
	DEBUG_LOG(MASTER_LOG, "Call iAppLoaderEntry");
	u32 iAppLoaderFuncAddr = 0x80003100;
	PowerPC::ppcState.gpr[3] = iAppLoaderFuncAddr + 0;
	PowerPC::ppcState.gpr[4] = iAppLoaderFuncAddr + 4;
	PowerPC::ppcState.gpr[5] = iAppLoaderFuncAddr + 8;
	RunFunction(iAppLoaderEntry);
	u32 iAppLoaderInit = Memory::ReadUnchecked_U32(iAppLoaderFuncAddr + 0);
	u32 iAppLoaderMain = Memory::ReadUnchecked_U32(iAppLoaderFuncAddr + 4);
	u32 iAppLoaderClose = Memory::ReadUnchecked_U32(iAppLoaderFuncAddr + 8);

	// iAppLoaderInit
	DEBUG_LOG(MASTER_LOG, "Call iAppLoaderInit");
	PowerPC::ppcState.gpr[3] = 0x81300000;
	RunFunction(iAppLoaderInit);
	
	// iAppLoaderMain - Here we load the apploader, the DOL (the exe) and the FST (filesystem).
	// To give you an idea about where the stuff is located on the disc take a look at yagcd
	// ch 13.
	DEBUG_LOG(MASTER_LOG, "Call iAppLoaderMain");
	do
	{
		PowerPC::ppcState.gpr[3] = 0x81300004;
		PowerPC::ppcState.gpr[4] = 0x81300008;
		PowerPC::ppcState.gpr[5] = 0x8130000c;

		RunFunction(iAppLoaderMain);

		u32 iRamAddress	= Memory::ReadUnchecked_U32(0x81300004);
		u32 iLength		= Memory::ReadUnchecked_U32(0x81300008);
		u32 iDVDOffset	= Memory::ReadUnchecked_U32(0x8130000c);
		
		INFO_LOG(MASTER_LOG, "DVDRead: offset: %08x   memOffset: %08x   length: %i", iDVDOffset, iRamAddress, iLength);
		DVDInterface::DVDRead(iDVDOffset, iRamAddress, iLength);

	} while(PowerPC::ppcState.gpr[3] != 0x00);

	// iAppLoaderClose
	DEBUG_LOG(MASTER_LOG, "call iAppLoaderClose");
	RunFunction(iAppLoaderClose);

	// return
	PC = PowerPC::ppcState.gpr[3];

	// Load patches
	std::string gameID = VolumeHandler::GetVolume()->GetUniqueID();
	u8 version = VolumeHandler::GetVolume()->GetRevision();
	PatchEngine::LoadPatches(gameID.c_str(), version);
	
	PowerPC::ppcState.DebugCount = 0;

	// If we have any patches that need to be applied very early, here's a good place
	PatchEngine::ApplyFramePatches();

	return true;
}

bool CBoot::SetupWiiMemory(unsigned int _CountryCode)
{
	INFO_LOG(BOOT, "Setup Wii Memory...");

	// Write the 256 byte setting.txt to memory. This may not be needed as
	// most or all games read the setting.txt file from
	// \title\00000001\00000002\data\setting.txt directly after the read the
	// SYSCONF file. The games also read it to 0x3800, what is a little strange
	// however is that it only reads the first 100 bytes of it.
	std::string region_filename,
				settings_Filename(Common::GetTitleDataPath(TITLEID_SYSMENU) + WII_SETTING);

	switch((DiscIO::IVolume::ECountry)_CountryCode)
	{
	case DiscIO::IVolume::COUNTRY_KOREA:
		region_filename = File::GetSysDirectory() + WII_SYS_DIR + DIR_SEP + WII_KOR_SETTING;
		break;
	case DiscIO::IVolume::COUNTRY_TAIWAN: 
		// TODO: Determine if Taiwan has their own specific settings.
	case DiscIO::IVolume::COUNTRY_JAPAN:
		region_filename = File::GetSysDirectory() + WII_SYS_DIR + DIR_SEP + WII_JAP_SETTING;
		break;

	case DiscIO::IVolume::COUNTRY_USA:
		region_filename = File::GetSysDirectory() + WII_SYS_DIR + DIR_SEP + WII_USA_SETTING;
		break;

	case DiscIO::IVolume::COUNTRY_EUROPE:
		region_filename = File::GetSysDirectory() + WII_SYS_DIR + DIR_SEP + WII_EUR_SETTING;
		break;

	default:
		// PanicAlertT("SetupWiiMem: Unknown country. Wii boot process will be switched to European settings.");
		region_filename = File::GetSysDirectory() + WII_SYS_DIR + DIR_SEP + WII_EUR_SETTING;
		break;
	}

	{
	if (File::Exists(settings_Filename))
	{
		File::Delete(settings_Filename);
	}
	File::CreateFullPath(settings_Filename);
	File::Copy(region_filename, settings_Filename);
	File::IOFile settingsFile(settings_Filename, "rb");
	if (!settingsFile)
	{
		PanicAlertT("SetupWiiMem: Cant find setting file");	
		return false;
	}

	settingsFile.ReadBytes(Memory::GetPointer(0x3800), 256);
	}

	/*
	Set hardcoded global variables to Wii memory. These are partly collected from
	Wiibrew. These values are needed for the games to function correctly. A few
	values in this region will also be placed here by the game as it boots.
	They are:
	0x80000038	Start of FST
	0x8000003c	Size of FST Size
	0x80000060	Copyright code
	*/

	DVDInterface::DVDRead(0x00000000, 0x00000000, 0x20); // Game Code
	Memory::Write_U32(0x0D15EA5E, 0x00000020);		// Another magic word
	Memory::Write_U32(0x00000001, 0x00000024);		// Unknown
	Memory::Write_U32(Memory::REALRAM_SIZE, 0x00000028);	// MEM1 size 24MB
	Memory::Write_U32(0x00000023, 0x0000002c);		// Production Board Model
	Memory::Write_U32(0x00000000, 0x00000030);		// Init
	Memory::Write_U32(0x817FEC60, 0x00000034);		// Init
	// 38, 3C should get start, size of FST through apploader
	Memory::Write_U32(0x38a00040, 0x00000060);		// Exception init
	Memory::Write_U32(0x8008f7b8, 0x000000e4);		// Thread Init
	Memory::Write_U32(Memory::REALRAM_SIZE, 0x000000f0);		// "Simulated memory size" (debug mode?)
	Memory::Write_U32(0x8179b500, 0x000000f4);		// __start
	Memory::Write_U32(0x0e7be2c0, 0x000000f8);		// Bus speed
	Memory::Write_U32(0x2B73A840, 0x000000fc);		// CPU speed
	Memory::Write_U16(0x0000,     0x000030e6);		// Console type
	Memory::Write_U32(0x00000000, 0x000030c0);		// EXI
	Memory::Write_U32(0x00000000, 0x000030c4);		// EXI
	Memory::Write_U32(0x00000000, 0x000030dc);		// Time
	Memory::Write_U32(0x00000000, 0x000030d8);		// Time
	Memory::Write_U16(0x8201,     0x000030e6);		// Dev console / debug capable
	Memory::Write_U32(0x00000000, 0x000030f0);		// Apploader
	Memory::Write_U32(0x01800000, 0x00003100);		// BAT
	Memory::Write_U32(0x01800000, 0x00003104);		// BAT
	Memory::Write_U32(0x00000000, 0x0000310c);		// Init
	Memory::Write_U32(0x8179d500, 0x00003110);		// Init
	Memory::Write_U32(0x04000000, 0x00003118);		// Unknown
	Memory::Write_U32(0x04000000, 0x0000311c);		// BAT
	Memory::Write_U32(0x93400000, 0x00003120);		// BAT
	Memory::Write_U32(0x90000800, 0x00003124);		// Init - MEM2 low
	Memory::Write_U32(0x93ae0000, 0x00003128);		// Init - MEM2 high
	Memory::Write_U32(0x93ae0000, 0x00003130);		// IOS MEM2 low
	Memory::Write_U32(0x93b00000, 0x00003134);		// IOS MEM2 high
	Memory::Write_U32(0x00000012, 0x00003138);		// Console type
	// 40 is copied from 88 after running apploader
	Memory::Write_U32(0x00090204, 0x00003140);		// IOS revision (IOS9, v2.4)
	Memory::Write_U32(0x00062507, 0x00003144);		// IOS date in USA format (June 25, 2007)
	Memory::Write_U16(0x0113,     0x0000315e);		// Apploader
	Memory::Write_U32(0x0000FF16, 0x00003158);		// DDR ram vendor code
	Memory::Write_U32(0x00000000, 0x00003160);		// Init semaphore (sysmenu waits for this to clear)
	Memory::Write_U32(0x00090204, 0x00003188);		// Expected IOS revision

	Memory::Write_U8(0x80, 0x0000315c);				// OSInit
	Memory::Write_U16(0x0000, 0x000030e0);			// PADInit
	Memory::Write_U32(0x80000000, 0x00003184);		// GameID Address

	// Fake the VI Init of the IPL 
	Memory::Write_U32(SConfig::GetInstance().m_LocalCoreStartupParameter.bNTSC ? 0 : 1, 0x000000CC);

	// Clear exception handler. Why? Don't we begin with only zeros?
	for (int i = 0x3000; i <= 0x3038; i += 4)
	{
		Memory::Write_U32(0x00000000, 0x80000000 + i);
	}
	return true;
}

// __________________________________________________________________________________________________
// Wii Bootstrap 2 HLE: 
// copy the apploader to 0x81200000
// execute the apploader
bool CBoot::EmulatedBS2_Wii()
{
	INFO_LOG(BOOT, "Faking Wii BS2...");

	// setup wii memory
	DiscIO::IVolume::ECountry CountryCode = DiscIO::IVolume::COUNTRY_UNKNOWN;
	if (VolumeHandler::IsValid())
		CountryCode = VolumeHandler::GetVolume()->GetCountry();
	if (SetupWiiMemory(CountryCode) == false)
		return false;

	// This is some kind of consistency check that is compared to the 0x00
	// values as the game boots. This location keep the 4 byte ID for as long
	// as the game is running. The 6 byte ID at 0x00 is overwritten sometime
	// after this check during booting. 
	VolumeHandler::ReadToPtr(Memory::GetPointer(0x3180), 0, 4);

	// Execute the apploader
	bool apploaderRan = false;
	if (VolumeHandler::IsValid() && VolumeHandler::IsWii())	
	{
		UReg_MSR& m_MSR = ((UReg_MSR&)PowerPC::ppcState.msr);
		m_MSR.FP = 1;

		Memory::Write_U32(0x4c000064,	0x80000300);	// write default DFI Handler:		rfi
		Memory::Write_U32(0x4c000064,	0x80000800);	// write default FPU Handler:		rfi
		Memory::Write_U32(0x4c000064,	0x80000C00);	// write default Syscall Handler:	rfi

		HLE::Patch(0x81300000, "OSReport");				// HLE OSReport for Apploader

		PowerPC::ppcState.gpr[1] = 0x816ffff0;			// StackPointer

		u32 iAppLoaderOffset = 0x2440; // 0x1c40;

		// Load Apploader to Memory
		u32 iAppLoaderEntry = VolumeHandler::Read32(iAppLoaderOffset + 0x10);
		u32 iAppLoaderSize = VolumeHandler::Read32(iAppLoaderOffset + 0x14);
		if ((iAppLoaderEntry == (u32)-1) || (iAppLoaderSize == (u32)-1)) 
		{
			ERROR_LOG(BOOT, "Invalid apploader. Probably your image is corrupted.");
			return false;
		}
		VolumeHandler::ReadToPtr(Memory::GetPointer(0x81200000), iAppLoaderOffset + 0x20, iAppLoaderSize);

		//call iAppLoaderEntry
		DEBUG_LOG(BOOT, "Call iAppLoaderEntry");

		u32 iAppLoaderFuncAddr = 0x80004000;
		PowerPC::ppcState.gpr[3] = iAppLoaderFuncAddr + 0;
		PowerPC::ppcState.gpr[4] = iAppLoaderFuncAddr + 4;
		PowerPC::ppcState.gpr[5] = iAppLoaderFuncAddr + 8;	
		RunFunction(iAppLoaderEntry);
		u32 iAppLoaderInit = Memory::ReadUnchecked_U32(iAppLoaderFuncAddr+0);
		u32 iAppLoaderMain = Memory::ReadUnchecked_U32(iAppLoaderFuncAddr+4);
		u32 iAppLoaderClose = Memory::ReadUnchecked_U32(iAppLoaderFuncAddr+8);

		// iAppLoaderInit
		DEBUG_LOG(BOOT, "Run iAppLoaderInit");
		PowerPC::ppcState.gpr[3] = 0x81300000; 
		RunFunction(iAppLoaderInit);

		// Let the apploader load the exe to memory. At this point I get an unknown IPC command
		// (command zero) when I load Wii Sports or other games a second time. I don't notice
		// any side effects however. It's a little disconcerting however that Start after Stop
		// behaves differently than the first Start after starting Dolphin. It means something
		// was not reset correctly. 
		DEBUG_LOG(BOOT, "Run iAppLoaderMain");
		do
		{
			PowerPC::ppcState.gpr[3] = 0x81300004;
			PowerPC::ppcState.gpr[4] = 0x81300008;
			PowerPC::ppcState.gpr[5] = 0x8130000c;

			RunFunction(iAppLoaderMain);

			u32 iRamAddress	= Memory::ReadUnchecked_U32(0x81300004);
			u32 iLength		= Memory::ReadUnchecked_U32(0x81300008);
			u32 iDVDOffset	= Memory::ReadUnchecked_U32(0x8130000c) << 2;

			INFO_LOG(BOOT, "DVDRead: offset: %08x   memOffset: %08x   length: %i", iDVDOffset, iRamAddress, iLength);
			DVDInterface::DVDRead(iDVDOffset, iRamAddress, iLength);
		} while(PowerPC::ppcState.gpr[3] != 0x00);

		// iAppLoaderClose
		DEBUG_LOG(BOOT, "Run iAppLoaderClose");
		RunFunction(iAppLoaderClose);

		apploaderRan = true;

		// Pass the "#002 check"
		// Apploader writes the IOS version and revision here, we copy it
		// Fake IOSv9 r2.4 if no version is found (elf loading)
		u32 firmwareVer = Memory::Read_U32(0x80003188);
		Memory::Write_U32(firmwareVer ? firmwareVer : 0x00090204, 0x00003140);

		// Load patches and run startup patches
		std::string gameID = VolumeHandler::GetVolume()->GetUniqueID();
		u8 version = VolumeHandler::GetVolume()->GetRevision();
		PatchEngine::LoadPatches(gameID.c_str(), version);

		// return
		PC = PowerPC::ppcState.gpr[3];
	}

	PowerPC::ppcState.DebugCount = 0;

	return apploaderRan;
}

// Returns true if apploader has run successfully
bool CBoot::EmulatedBS2(bool _bIsWii)
{
	return _bIsWii ? EmulatedBS2_Wii() : EmulatedBS2_GC();
}
