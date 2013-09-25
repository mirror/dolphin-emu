// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"
#include "FileUtil.h"
#include "StringUtil.h"
#include "../Core.h"
#include "../CoreTiming.h"

#include "../ConfigManager.h"
#include "../Movie.h"
#include "EXI.h"
#include "EXI_Device.h"
#include "EXI_DeviceMemoryCardFolder.h"
#include "Sram.h"
#include "GCMemcard.h"
#include "Volume.h"
#include "VolumeCreator.h"
#include "Memmap.h"

#define MC_STATUS_BUSY					0x80   
#define MC_STATUS_UNLOCKED				0x40
#define MC_STATUS_SLEEP					0x20
#define MC_STATUS_ERASEERROR			0x10
#define MC_STATUS_PROGRAMEERROR			0x08
#define MC_STATUS_READY					0x01
#define SIZE_TO_Mb (1024 * 8 * 16)
#define MC_HDR_SIZE 0xA000

void CEXIMemoryCardFolder::FlushCallback(u64 userdata, int cyclesLate)
{
	// note that userdata is forbidden to be a pointer, due to the implementation of EventDoState
	int card_index = (int)userdata;
	CEXIMemoryCardFolder* pThis = (CEXIMemoryCardFolder*)ExpansionInterface::FindDevice(EXIDEVICE_MEMORYCARDFOLDER, card_index);
	if (pThis)
		pThis->Flush();
}

void CEXIMemoryCardFolder::CmdDoneCallback(u64 userdata, int cyclesLate)
{
	int card_index = (int)userdata;
	CEXIMemoryCardFolder* pThis = (CEXIMemoryCardFolder*)ExpansionInterface::FindDevice(EXIDEVICE_MEMORYCARDFOLDER, card_index);
	if (pThis)
		pThis->CmdDone();
}

CEXIMemoryCardFolder::CEXIMemoryCardFolder(const int index)
	: card_index(index)
	, m_bDirty(false)
{
	
	DiscIO::IVolume::ECountry CountryCode = DiscIO::IVolume::COUNTRY_UNKNOWN;
	DiscIO::IVolume * volume = DiscIO::CreateVolumeFromFilename(SConfig::GetInstance().m_LocalCoreStartupParameter.m_strFilename);
	if (volume)
		CountryCode = volume->GetCountry();
	bool ascii = true;
	m_strDirectoryName = File::GetUserPath(D_GCUSER_IDX);
	switch (CountryCode)
	{
	case  DiscIO::IVolume::COUNTRY_JAPAN:
		ascii = false;
		m_strDirectoryName += JAP_DIR DIR_SEP;
		break;
	case DiscIO::IVolume::COUNTRY_USA:
		m_strDirectoryName += USA_DIR DIR_SEP;
		break;
	default:
		CountryCode = DiscIO::IVolume::COUNTRY_EUROPE;
		m_strDirectoryName += EUR_DIR DIR_SEP;
	}
	m_strDirectoryName += StringFromFormat("Card %c", 'A'+card_index) + DIR_SEP;
	if (!File::Exists(m_strDirectoryName))
	{
		File::CreateFullPath(m_strDirectoryName);
		std::string ini_memcard = (card_index == 0) ? SConfig::GetInstance().m_strMemoryCardA : SConfig::GetInstance().m_strMemoryCardB;
		if (File::Exists(ini_memcard))
		{
			GCMemcard memcard(ini_memcard.c_str());
			if (memcard.IsValid())
			{
				for (u8 i = 0; i < DIRLEN; i++)
				{
					memcard.ExportGci(i, NULL, m_strDirectoryName);
				}
			}
		}
	}
	else if (!File::IsDirectory(m_strDirectoryName))
	{
		// TODO more user friendly abort
		PanicAlert("%s is not a directory", m_strDirectoryName.c_str());
		exit(0);
	}
	// we're potentially leaking events here, since there's no UnregisterEvent until emu shutdown, but I guess it's inconsequential
	et_this_card = CoreTiming::RegisterEvent((card_index == 0) ? "memcardFlushA" : "memcardFlushB", FlushCallback);
	et_cmd_done = CoreTiming::RegisterEvent((card_index == 0) ? "memcardDoneA" : "memcardDoneB", CmdDoneCallback);
 
	interruptSwitch = 0;
	m_bInterruptSet = 0;
	command = 0;
	status = MC_STATUS_BUSY | MC_STATUS_UNLOCKED | MC_STATUS_READY;
	m_uPosition = 0;
	memset(programming_buffer, 0, sizeof(programming_buffer));
	formatDelay = 0;
 
	//Nintendo Memory Card EXI IDs
	//0x00000004 Memory Card 59		4Mbit
	//0x00000008 Memory Card 123	8Mb
	//0x00000010 Memory Card 251	16Mb
	//0x00000020 Memory Card 507	32Mb
	//0x00000040 Memory Card 1019	64Mb
	//0x00000080 Memory Card 2043	128Mb
 
	//0x00000510 16Mb "bigben" card
	//card_id = 0xc243;
 
	card_id = 0xc221; // It's a Nintendo brand memcard
 
	memcarddir = new GCMemcardDirectory(m_strDirectoryName, card_index, MemCard2043Mb, ascii);
	nintendo_card_id = 0x00000080;
	memory_card_size = MemCard2043Mb * SIZE_TO_Mb;
	u8 header[20] = {0};
	memcarddir->Read(0, 20, header);
	memcarddir->Flush();
	SetCardFlashID(header, card_index);
}

// Flush memory card contents to disc
void CEXIMemoryCardFolder::Flush(bool exiting)
{
	if(!m_bDirty)
		return;
	if (!Core::g_CoreStartupParameter.bEnableMemcardSaving)
		return;
	
	memcarddir->Flush();

	if(!exiting)
		Core::DisplayMessage(StringFromFormat("Writing to memory card %c", card_index ? 'B' : 'A'), 1000);

	m_bDirty = false;
}

CEXIMemoryCardFolder::~CEXIMemoryCardFolder()
{
	delete memcarddir;
	CoreTiming::RemoveEvent(et_this_card);
	Flush(true);
}

bool CEXIMemoryCardFolder::IsPresent() 
{
	return true;
}

void CEXIMemoryCardFolder::CmdDone()
{
	status |= MC_STATUS_READY;
	status &= ~MC_STATUS_BUSY;

	m_bInterruptSet = 1;
	m_bDirty = true;
}

void CEXIMemoryCardFolder::CmdDoneLater(u64 cycles)
{
	CoreTiming::RemoveEvent(et_cmd_done);
	CoreTiming::ScheduleEvent(cycles, et_cmd_done, (u64)card_index);
}

void CEXIMemoryCardFolder::SetCS(int cs)
{


	if (cs)  // not-selected to selected
	{
		m_uPosition = 0;
	}
	else
	{	
		switch (command)
		{
		case cmdSectorErase:
			if (m_uPosition > 2)
			{

				memcarddir->clearBlock(address  & (memory_card_size-1));
				//memset(memory_card_content + (address & (memory_card_size-1)), 0xFF, 0x2000);
				status |= MC_STATUS_BUSY;
				status &= ~MC_STATUS_READY;

				//???

				CmdDoneLater(5000);
			}
			break;

		case cmdChipErase:
			if (m_uPosition > 2)
			{
				memcarddir->clearAll();
				//memset(memory_card_content, 0xFF, memory_card_size);
				status &= ~MC_STATUS_BUSY;
				m_bDirty = true;
			}
			break;

		case cmdPageProgram:
			if (m_uPosition >= 5)
			{
				int count = m_uPosition - 5;
				int i=0;
				status &= ~0x80;

				while (count--)
				{
					memcarddir->Write(address, 1, &(programming_buffer[i++]));
					//memory_card_content[address] = programming_buffer[i++];
					i &= 127;
					address = (address & ~0x1FF) | ((address+1) & 0x1FF);
				}

				CmdDoneLater(5000);
			}
			
			// Page written to memory card, not just to buffer - let's schedule a flush 0.5b cycles into the future (1 sec)
			// But first we unschedule already scheduled flushes - no point in flushing once per page for a large write.
			CoreTiming::RemoveEvent(et_this_card);
			CoreTiming::ScheduleEvent(500000000, et_this_card, (u64)card_index);
			break;
		}
	}
}

void CEXIMemoryCardFolder::Update()
{
	if (formatDelay)
	{
		formatDelay--;

		if (!formatDelay)
		{
			status |= MC_STATUS_READY;
			status &= ~MC_STATUS_BUSY;

			m_bInterruptSet = 1;
		}
	}
}

bool CEXIMemoryCardFolder::IsInterruptSet()
{
	if (interruptSwitch)
		return m_bInterruptSet;
	return false;
}

void CEXIMemoryCardFolder::TransferByte(u8 &byte)
{
	DEBUG_LOG(EXPANSIONINTERFACE, "EXI MEMCARD: > %02x", byte);
	if (m_uPosition == 0)
	{
		command = byte;  // first byte is command
		byte = 0xFF; // would be tristate, but we don't care.

		switch (command) // This seems silly, do we really need it?
		{
		case cmdNintendoID:
		case cmdReadArray:
		case cmdArrayToBuffer:
		case cmdSetInterrupt:
		case cmdWriteBuffer:
		case cmdReadStatus:
		case cmdReadID:
		case cmdReadErrorBuffer:
		case cmdWakeUp:
		case cmdSleep:
		case cmdClearStatus:
		case cmdSectorErase:
		case cmdPageProgram:
		case cmdExtraByteProgram:
		case cmdChipErase:
			INFO_LOG(EXPANSIONINTERFACE, "EXI MEMCARD: command %02x at position 0. seems normal.", command);
			break;
		default:
			WARN_LOG(EXPANSIONINTERFACE, "EXI MEMCARD: command %02x at position 0", command);
			break;
		}
		if (command == cmdClearStatus)
		{
			status &= ~MC_STATUS_PROGRAMEERROR;
			status &= ~MC_STATUS_ERASEERROR;

			status |= MC_STATUS_READY;

			m_bInterruptSet = 0;

			byte = 0xFF;
			m_uPosition = 0;
		}
	} 
	else
	{
		switch (command)
		{
		case cmdNintendoID:
			//
			// Nintendo card:
			// 00 | 80 00 00 00 10 00 00 00 
			// "bigben" card:
			// 00 | ff 00 00 05 10 00 00 00 00 00 00 00 00 00 00
			// we do it the Nintendo way.
			if (m_uPosition == 1)
				byte = 0x80; // dummy cycle
			else
				byte = (u8)(nintendo_card_id >> (24-(((m_uPosition-2) & 3) * 8)));
			break;

		case cmdReadArray:
			switch (m_uPosition)
			{
			case 1: // AD1
				address = byte << 17;
				byte = 0xFF;
				break;
			case 2: // AD2
				address |= byte << 9;
				break;
			case 3: // AD3
				address |= (byte & 3) << 7;
				break;
			case 4: // BA
				address |= (byte & 0x7F);
				break;
			}
			if (m_uPosition > 1) // not specified for 1..8, anyway
			{
				memcarddir->Read(address, 1, &byte);
				//byte = memory_card_content[address & (memory_card_size-1)];
				// after 9 bytes, we start incrementing the address,
				// but only the sector offset - the pointer wraps around
				if (m_uPosition >= 9)
					address = (address & ~0x1FF) | ((address+1) & 0x1FF);
			}
			break;

		case cmdReadStatus:
			// (unspecified for byte 1)
			byte = status;
			break;

		case cmdReadID:
			if (m_uPosition == 1) // (unspecified)
				byte = (u8)(card_id >> 8);
			else
				byte = (u8)((m_uPosition & 1) ? (card_id) : (card_id >> 8));
			break;

		case cmdSectorErase:
			switch (m_uPosition)
			{
			case 1: // AD1
				address = byte << 17;
				break;
			case 2: // AD2
				address |= byte << 9;
				break;
			}
			byte = 0xFF;
			break;

		case cmdSetInterrupt:
			if (m_uPosition == 1)
			{
				interruptSwitch = byte;
			}
			byte = 0xFF;
			break;

		case cmdChipErase:
			byte = 0xFF;
			break;

		case cmdPageProgram:
			switch (m_uPosition)
			{
			case 1: // AD1
				address = byte << 17;
				break;
			case 2: // AD2
				address |= byte << 9;
				break;
			case 3: // AD3
				address |= (byte & 3) << 7;
				break;
			case 4: // BA
				address |= (byte & 0x7F);
				break;
			}

			if(m_uPosition >= 5)
				programming_buffer[((m_uPosition - 5) & 0x7F)] = byte; // wrap around after 128 bytes

			byte = 0xFF;
			break;

		default:
			WARN_LOG(EXPANSIONINTERFACE, "EXI MEMCARD: unknown command byte %02x\n", byte);
			byte = 0xFF;	
		}
	}
	m_uPosition++;
	DEBUG_LOG(EXPANSIONINTERFACE, "EXI MEMCARD: < %02x", byte);
}

void CEXIMemoryCardFolder::PauseAndLock(bool doLock, bool unpauseOnUnlock)
{
	if (doLock)
	{
		// we don't exactly have anything to pause,
		// but let's make sure the flush thread isn't running.
	}
}

void CEXIMemoryCardFolder::DoState(PointerWrap &p)
{
	// for movie sync, we need to save/load memory card contents (and other data) in savestates.
	// otherwise, we'll assume the user wants to keep their memcards and saves separate,
	// unless we're loading (in which case we let the savestate contents decide, in order to stay aligned with them).
	bool storeContents = true;//(Movie::IsRecordingInput() || Movie::IsPlayingInput());
	p.Do(storeContents);

	if (storeContents)
	{
		p.Do(interruptSwitch);
		p.Do(m_bInterruptSet);
		p.Do(command);
		p.Do(status);
		p.Do(m_uPosition);
		p.Do(programming_buffer);
		p.Do(formatDelay);
		p.Do(m_bDirty);
		p.Do(address);
		p.Do(nintendo_card_id);
		p.Do(card_id);
		p.Do(card_index);
		memcarddir->DoState(p);
	}
}

IEXIDevice* CEXIMemoryCardFolder::FindDevice(TEXIDevices device_type, int customIndex)
{
	if (device_type != m_deviceType)
		return NULL;
	if (customIndex != card_index)
		return NULL;
	return this;
}

// DMA reads seem to be preceded by all of the necessary setup via
// IMMRead, do DMA all at once instead of single byte at a time as done by IEXIDevice::DMARead
void CEXIMemoryCardFolder::DMARead (u32 _uAddr, u32 _uSize)
{
	memcarddir->Read(address, _uSize, Memory::GetPointer(_uAddr));
}

// Unfortunately DMA wites are not as simple as reads
// TODO: investigate further to see if we can bypass the single
// byte transfer as done by IEXIDevice::DMAWrite
/*void CEXIMemoryCardFolder::DMAWrite(u32 _uAddr, u32 _uSize)
{
	//	memcarddir->Write(address, _uSize, Memory::GetPointer(_uAddr));
}*/
