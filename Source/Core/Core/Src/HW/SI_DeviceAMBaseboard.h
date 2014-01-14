// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _SIDEVICE_AMBASEBOARD_H
#define _SIDEVICE_AMBASEBOARD_H

// triforce (GC-AM) baseboard
class CSIDevice_AMBaseboard : public ISIDevice
{
private:
	enum EBufferCommands
	{
		CMD_RESET		= 0x00,
		CMD_GCAM		= 0x70,
	};
	enum CARDCommands
	{
		CARD_INIT			= 0x10,
		CARD_GET_CARD_STATE	= 0x20,
		CARD_IS_PRESENT		= 0x40,
		CARD_LOAD_CARD		= 0xB0,
		CARD_CLEAN_CARD		= 0xA0,
		CARD_READ			= 0x33,
		CARD_WRITE			= 0x53,
		CARD_WRITE_INFO		= 0x7C,
		CARD_78				= 0x78,
		CARD_7A				= 0x7A,
		CARD_7D				= 0x7D,
		CARD_D0				= 0xD0,
		CARD_80				= 0x80,
	};

	unsigned short coin[2];
	int coin_pressed[2];
	
	u32 CARDMemSize;
	u32 CARDInserted;

	u8 CARDRBuf[0x100];
	u8 CARDROff=0;
	u32 CARDCommand;
	u32 CARDClean;

	u8	CARDMem[0xD0];
	u8	CARDReadPacket[0xDB];


	u32 CARDWriteLength;
	u32 CARDWriteWrote;

	u32 CARDReadLength;
	u32 CARDReadRead;

	u32 CARDBit;
	u32 CardStateCallCount;

	u32 STRInit;

public:
	// constructor
	CSIDevice_AMBaseboard(SIDevices device, int _iDeviceNumber);

	// run the SI Buffer
	virtual int RunBuffer(u8* _pBuffer, int _iLength);

	// return true on new data
	virtual bool GetData(u32& _Hi, u32& _Low);

	// send a command directly
	virtual void SendCommand(u32 _Cmd, u8 _Poll);
};

#endif // _SIDEVICE_AMBASEBOARD_H
