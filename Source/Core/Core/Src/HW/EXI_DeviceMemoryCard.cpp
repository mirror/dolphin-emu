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
#include "EXI_DeviceMemoryCard.h"
#include "Sram.h"
#include "GCMemcard.h"

#define MC_STATUS_BUSY					0x80
#define MC_STATUS_UNLOCKED				0x40
#define MC_STATUS_SLEEP					0x20
#define MC_STATUS_ERASEERROR			0x10
#define MC_STATUS_PROGRAMEERROR			0x08
#define MC_STATUS_READY					0x01
#define SIZE_TO_Mb (1024 * 8 * 16)
#define MC_HDR_SIZE 0xA000

CLocalMemoryCard::CLocalMemoryCard(int card_index)
{
	std::string filename = (card_index == 0) ? SConfig::GetInstance().m_strMemoryCardA : SConfig::GetInstance().m_strMemoryCardB;
	m_Good = false;

	m_File.Open(filename, "r+b");
	if (m_File)
	{
		// Measure size of the memcard file.
		int size = (int)m_File.GetSize();
		if ((size & (size - 1)) || size > 128 * SIZE_TO_Mb)
		{
			PanicAlertT("Memory card file is corrupted (bad file size)");
			return;
		}
		u16 sizeMb;
		if (!Read(0x22, &sizeMb, sizeof(sizeMb)))
			return;

		if (Common::swap16(sizeMb) * SIZE_TO_Mb != size)
		{
			PanicAlertT("Memory card file is corrupted (bad size in header)");
			return;
		}
	}
	else
	{
		if (File::Exists(filename))
			goto badOpen;
		WARN_LOG(EXPANSIONINTERFACE, "No memory card found. Will create a new one.");
		std::string dir;
		SplitPath(filename, &dir, 0, 0);
		if (!File::IsDirectory(dir))
			File::CreateFullPath(dir);
		m_File.Open(filename, "w+b");
		if (m_File)
		{
			PWBuffer buf;
			buf.resize(128 * SIZE_TO_Mb);
			GCMemcard::Format(buf.data(), filename.find(".JAP.raw") != std::string::npos, buf.size() / SIZE_TO_Mb);
			memset(buf.data() + MC_HDR_SIZE, 0xff, buf.size() - MC_HDR_SIZE);
			if (!m_File.WriteBytes(buf.data(), buf.size()))
			{
				PanicAlertT("Write to memory card failed");
				return;
			}
		}
		else
		{
			goto badOpen;
		}
	}
	m_Good = true;
	return;

badOpen:
	PanicAlertT("Could not open memory card file %s r/w.\n\n"
		"Are you running Dolphin from a CD/DVD, or is the save file write protected?\n\n"
		"Are you receiving this after moving the emulator directory?\nIf so, then you may "
		"need to re-specify your memory card location in the options.", filename.c_str());
}

bool CLocalMemoryCard::Read(int offset, void* buffer, int size)
{
	m_File.Seek(offset & (m_File.GetSize() - 1), SEEK_SET);
	bool result = m_File.ReadBytes(buffer, size);
	if (!result)
	{
		PanicAlertT("Read from memory card failed");
		memset(buffer, 0xff, size);
	}
	return result;
}

bool CLocalMemoryCard::Write(int offset, const void* buffer, int size)
{
	m_File.Seek(offset & (m_File.GetSize() - 1), SEEK_SET);
	bool result = m_File.WriteBytes(buffer, size);
	if (!result)
	{
		PanicAlertT("Write to memory card (offset=%d, size=%d) failed", offset, size);
	}
	return result;
}

#if 0
void CLocalMemoryCard::DoState(PointerWrap &p)
{
	// for movie sync, we need to save/load memory card contents (and other data) in savestates.
	// otherwise, we'll assume the user wants to keep their memcards and saves separate,
	// unless we're loading (in which case we let the savestate contents decide, in order to stay aligned with them).
	// ^ does that actually make sense? for now, ignore movies

	p.Do(nintendo_card_id);
	p.Do(card_id);
	p.Do(memory_card_size);
	p.DoArray(memory_card_content, memory_card_size);
	p.Do(card_index);
}
#endif

CEXIMemoryCard::CEXIMemoryCard(const int index)
	: card_index(index)
{
	// we're potentially leaking events here, since there's no UnregisterEvent until emu shutdown, but I guess it's inconsequential
	et_cmd_done = CoreTiming::RegisterEvent((card_index == 0) ? "memcardDoneA" : "memcardDoneB", CmdDoneCallback);

	interruptSwitch = 0;
	m_bInterruptSet = 0;
	command = 0;
	status = MC_STATUS_BUSY | MC_STATUS_UNLOCKED | MC_STATUS_READY;
	m_uPosition = 0;
	memset(programming_buffer, 0, sizeof(programming_buffer));

	int local = g_EXISyncClass.GetLocalIndex(index);
	if (local != -1)
	{
		local_card.reset(new CLocalMemoryCard(local));
	}

	SetCardFlashID(GetSector(0), card_index);
}

CEXIMemoryCard::~CEXIMemoryCard()
{
	CoreTiming::RemoveEvent(et_cmd_done);
}

bool CEXIMemoryCard::IsPresent()
{
	return true;
}

void CEXIMemoryCard::CmdDoneCallback(u64 userdata, int cyclesLate)
{
	int card_index = (int)userdata;
	CEXIMemoryCard* pThis = (CEXIMemoryCard*)ExpansionInterface::FindDevice(EXIDEVICE_MEMORYCARD, card_index);
	if (pThis)
		pThis->CmdDone();
}


void CEXIMemoryCard::CmdDone()
{
	status |= MC_STATUS_READY;
	status &= ~MC_STATUS_BUSY;

	m_bInterruptSet = 1;
}

void CEXIMemoryCard::CmdDoneLater(u64 cycles)
{
	CoreTiming::RemoveEvent(et_cmd_done);
	CoreTiming::ScheduleEvent((int)cycles, et_cmd_done, (u64)card_index);
}

void CEXIMemoryCard::SetCS(int cs)
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
				// This was originally 0x2000, but I'm going to make a leap and
				// say it should be 0x200?
				u8* buf = GetSector(address);
				memset(buf, 0xff, 0x200);
				WriteSector(address);

				status |= MC_STATUS_BUSY;
				status &= ~MC_STATUS_READY;

				//???

				CmdDoneLater(5000);
			}
			break;

		case cmdChipErase:
			if (m_uPosition > 2)
			{
				// I hope you really wanted to erase the whole thing...
				// XX delete card

				status &= ~MC_STATUS_BUSY;
				// The command is never done...? XXX
			}
			break;

		case cmdPageProgram:
			if (m_uPosition >= 5)
			{
				int count = m_uPosition - 5;
				int i = 0;
				status &= ~MC_STATUS_BUSY;

				u8* buf = GetSector(address);
				while (count--)
				{
					buf[address & 0x1ff] = programming_buffer[i++];
					i &= 127;
					address = (address & ~0x1ff) | ((address+1) & 0x1ff);
				}
				WriteSector(address);

				CmdDoneLater(5000);
			}

			break;
		}
	}
}

bool CEXIMemoryCard::IsInterruptSet()
{
	if (interruptSwitch)
		return m_bInterruptSet;
	return false;
}

void CEXIMemoryCard::TransferByte(u8 &byte)
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
			{
			int nintendo_card_id = Common::swap16(GetSector(0) + 0x22);

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
			}

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
				byte = GetSector(address)[address & 0x1ff];
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
			{
			//Nintendo Memory Card EXI IDs
			//0x00000004 Memory Card 59		4Mbit
			//0x00000008 Memory Card 123	8Mb
			//0x00000010 Memory Card 251	16Mb
			//0x00000020 Memory Card 507	32Mb
			//0x00000040 Memory Card 1019	64Mb
			//0x00000080 Memory Card 2043	128Mb

			//0x00000510 16Mb "bigben" card
			//card_id = 0xc243;

			int card_id = 0xc221; // It's a Nintendo brand memcard

			if (m_uPosition == 1) // (unspecified)
				byte = (u8)(card_id >> 8);
			else
				byte = (u8)((m_uPosition & 1) ? (card_id) : (card_id >> 8));
			break;
			}

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

void CEXIMemoryCard::RequestChunk(unsigned int chunk)
{
	if (local_card && !current_chunks[chunk].m_Requested)
	{
		ChunkReport cr;
		cr.m_Chunk = chunk;
		cr.m_Data.resize(ChunkSize);
		local_card->Read(chunk * ChunkSize, cr.m_Data.data(), ChunkSize);
		int idx = g_EXISyncClass.GetLocalIndex(card_index);
		if (idx != -1)
			g_EXISyncClass.EnqueueLocalReport<ChunkReport>(idx, cr);
		current_chunks[chunk].m_Requested = true;
	}
}

u8* CEXIMemoryCard::GetSector(unsigned int addr)
{
	unsigned int chunk = addr / ChunkSize;
	if (current_chunks.size() <= chunk + 1)
		current_chunks.resize(chunk + 2);
	RequestChunk(chunk);
	RequestChunk(chunk + 1);
	while (!current_chunks[chunk].m_Present)
	{
		// Of course, this ought to be fixed to not be synchronous.
		g_EXISyncClass.DequeueReport<ChunkReport>(card_index, [&](ChunkReport&& incoming) {
			auto& chunkInfo = current_chunks.at(incoming.m_Chunk);
			chunkInfo.m_Present = true;
			chunkInfo.m_Data = std::move(incoming.m_Data);
		});
	}
	// Beginning of the sector
	return current_chunks[chunk].m_Data.data() + ((addr % ChunkSize) & ~0x1ff);
}

void CEXIMemoryCard::WriteSector(unsigned int addr)
{
	if (local_card)
		local_card->Write(addr & ~0x1ff, GetSector(addr), 0x200);
}

void CEXIMemoryCard::DoState(PointerWrap &p)
{
	p.Do(interruptSwitch);
	p.Do(m_bInterruptSet);
	p.Do(command);
	p.Do(status);
	p.Do(m_uPosition);
	p.Do(programming_buffer);
	p.Do(address);
	// XXX sync status
}

IEXIDevice* CEXIMemoryCard::FindDevice(TEXIDevices device_type, int customIndex)
{
	if (device_type != m_deviceType)
		return NULL;
	if (customIndex != card_index)
		return NULL;
	return this;
}
