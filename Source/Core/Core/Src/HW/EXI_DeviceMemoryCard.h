// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _EXI_DEVICEMEMORYCARD_H
#define _EXI_DEVICEMEMORYCARD_H

#include "Thread.h"
#include <memory>
#include <vector>

class CLocalMemoryCard
{
public:
	CLocalMemoryCard(int index);
	bool Read(int offset, void* buffer, int size);
	bool Write(int offset, const void* buffer, int size);

	File::IOFile m_File;
	bool m_Good;
};

class CEXIMemoryCard : public IEXIDevice
{
public:
	CEXIMemoryCard(const int index);
	virtual ~CEXIMemoryCard();
	void SetCS(int cs) override;
	bool IsInterruptSet() override;
	bool IsPresent() override;
	void DoState(PointerWrap &p) override;
	IEXIDevice* FindDevice(TEXIDevices device_type, int customIndex=-1) override;

private:
	enum
	{
		ChunkSize = 64*1024
	};

	// Scheduled when a command that required delayed end signaling is done.
	static void CmdDoneCallback(u64 userdata, int cyclesLate);

	// Signals that the command that was previously executed is now done.
	void CmdDone();

	// Variant of CmdDone which schedules an event later in the future to complete the command.
	void CmdDoneLater(u64 cycles);

	void RequestChunk(unsigned int chunk);
	u8* GetSector(unsigned int addr);
	void WriteSector(unsigned int addr);

	enum
	{
		cmdNintendoID			= 0x00,
		cmdReadArray			= 0x52,
		cmdArrayToBuffer		= 0x53,
		cmdSetInterrupt			= 0x81,
		cmdWriteBuffer			= 0x82,
		cmdReadStatus			= 0x83,
		cmdReadID				= 0x85,
		cmdReadErrorBuffer		= 0x86,
		cmdWakeUp				= 0x87,
		cmdSleep				= 0x88,
		cmdClearStatus			= 0x89,
		cmdSectorErase			= 0xF1,
		cmdPageProgram			= 0xF2,
		cmdExtraByteProgram		= 0xF3,
		cmdChipErase			= 0xF4,
	};

	//! memory card state

	// STATE_TO_SAVE
	int interruptSwitch;
	bool m_bInterruptSet;
	int command;
	int status;
	u32 m_uPosition;
	u8 programming_buffer[128];
	int card_index;
	int et_cmd_done;
	//! memory card parameters
	unsigned int address;

	struct ChunkReport
	{
	public:
		void DoReport(Packet& p)
		{
			p.Do(m_Chunk);
			p.Do(m_Data);
		}
		unsigned int m_Chunk;
		PWBuffer m_Data;
	};
	struct ChunkInfo
	{
		ChunkInfo() { m_Requested = m_Present = false; }
		PWBuffer m_Data;
		bool m_Requested;
		bool m_Present;
	};
	std::vector<ChunkInfo> current_chunks;
	std::unique_ptr<CLocalMemoryCard> local_card;

protected:
	virtual void TransferByte(u8 &byte) override;
};

#endif

