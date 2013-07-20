// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _WII_IPC_HLE_DEVICE_H_
#define _WII_IPC_HLE_DEVICE_H_

#include <string>
#include <queue>
#include "../HW/Memmap.h"

#include "ChunkFile.h"

#define	FS_SUCCESS		(u32)0		// Success
#define	FS_EACCES		(u32)-1		// Permission denied 
#define	FS_EEXIST		(u32)-2		// File exists 
#define	FS_EINVAL		(u32)-4		// Invalid argument Invalid FD
#define	FS_ENOENT		(u32)-6		// File not found 
#define	FS_EBUSY		(u32)-8		// Resource busy 
#define	FS_EIO			(u32)-12		// Returned on ECC error 
#define	FS_ENOMEM		(u32)-22		// Alloc failed during request 
#define	FS_EFATAL		(u32)-101	// Fatal error 
#define	FS_EACCESS		(u32)-102	// Permission denied 
#define	FS_ECORRUPT		(u32)-103	// returned for "corrupted" NAND 
#define	FS_EEXIST2		(u32)-105	// File exists 
#define	FS_ENOENT2		(u32)-106	// File not found 
#define	FS_ENFILE		(u32)-107	// Too many fds open 
#define	FS_EFBIG		(u32)-108	// Max block count reached? 
#define	FS_EFDEXHAUSTED	(u32)-109	// Too many fds open 
#define	FS_ENAMELEN		(u32)-110	// Pathname is too long 
#define	FS_EFDOPEN		(u32)-111	// FD is already open 
#define	FS_EIO2			(u32)-114	// Returned on ECC error 
#define	FS_ENOTEMPTY 	(u32)-115	// Directory not empty 
#define	FS_EDIRDEPTH	(u32)-116	// Max directory depth exceeded 
#define	FS_EBUSY2		(u32)-118	// Resource busy 
//#define	FS_EFATAL		(u32)-119		// Fatal error not used by IOS as fatal ERROR
#define	FS_EESEXHAUSTED	(u32)-1016	// Max of 2 ES handles at a time

class IWII_IPC_HLE_Device
{
public:

	IWII_IPC_HLE_Device(u32 _DeviceID, const std::string& _rName, bool _Hardware = true) :
		m_Name(_rName),
		m_DeviceID(_DeviceID),
		m_Hardware(_Hardware),
		m_Active(false)
	{
	}

	virtual ~IWII_IPC_HLE_Device()
	{
	}

	virtual void DoState(PointerWrap& p) 
	{
		DoStateShared(p);
		p.Do(m_Active);
	}
	
	void DoStateShared(PointerWrap& p);

	const std::string& GetDeviceName() const { return m_Name; }
	u32 GetDeviceID() const { return m_DeviceID; }

	virtual bool Open(u32 _CommandAddress, u32 _Mode)
	{
		(void)_Mode;
		WARN_LOG(WII_IPC_HLE, "%s does not support Open()", m_Name.c_str());
		Memory::Write_U32(FS_ENOENT, _CommandAddress + 4);
		m_Active = true;
		return true;
	}

	virtual bool Close(u32 _CommandAddress, bool _bForce = false)
	{
		WARN_LOG(WII_IPC_HLE, "%s does not support Close()", m_Name.c_str());
		if (!_bForce)
			Memory::Write_U32(FS_EINVAL, _CommandAddress + 4);
		m_Active = false;
		return true;
	}

#define UNIMPLEMENTED_CMD(cmd) WARN_LOG(WII_IPC_HLE, "%s does not support "#cmd"()", m_Name.c_str()); return true;
	virtual bool Seek	(u32) { UNIMPLEMENTED_CMD(Seek) }
	virtual bool Read	(u32) { UNIMPLEMENTED_CMD(Read) }
	virtual bool Write	(u32) { UNIMPLEMENTED_CMD(Write) }
	virtual bool IOCtl	(u32) { UNIMPLEMENTED_CMD(IOCtl) }
	virtual bool IOCtlV	(u32) { UNIMPLEMENTED_CMD(IOCtlV) }
#undef UNIMPLEMENTED_CMD

	virtual int GetCmdDelay(u32) { return 0; }

	virtual u32 Update() { return 0; }

	virtual bool IsHardware() { return m_Hardware; }
	virtual bool IsOpened() { return m_Active; }

protected:

	// STATE_TO_SAVE
	std::string m_Name;
	u32 m_DeviceID;
	bool m_Hardware;
	bool m_Active;

	// A struct for IOS ioctlv calls
	struct SIOCtlVBuffer
	{
		SIOCtlVBuffer(u32 _Address) : m_Address(_Address)
		{
			// These are the Ioctlv parameters in the IOS communication. The BufferVector
			// is a memory address offset at where the in and out buffer addresses are
			// stored.
			Parameter			= Memory::Read_U32(m_Address + 0x0C); // command 3, arg0
			NumberInBuffer		= Memory::Read_U32(m_Address + 0x10); // 4, arg1
			NumberPayloadBuffer	= Memory::Read_U32(m_Address + 0x14); // 5, arg2
			BufferVector		= Memory::Read_U32(m_Address + 0x18); // 6, arg3

			// The start of the out buffer
			u32 BufferVectorOffset = BufferVector;

			// Write the address and size for all in messages
			for (u32 i = 0; i < NumberInBuffer; i++)
			{
				SBuffer Buffer;
				Buffer.m_Address	= Memory::Read_U32(BufferVectorOffset);
				BufferVectorOffset += 4;
				Buffer.m_Size		= Memory::Read_U32(BufferVectorOffset);
				BufferVectorOffset += 4;
				InBuffer.push_back(Buffer);
				DEBUG_LOG(WII_IPC_HLE, "SIOCtlVBuffer in%i: 0x%08x, 0x%x",
							i, Buffer.m_Address, Buffer.m_Size);
			}

			// Write the address and size for all out or in-out messages
			for (u32 i = 0; i < NumberPayloadBuffer; i++)
			{
				SBuffer Buffer;
				Buffer.m_Address	= Memory::Read_U32(BufferVectorOffset);
				BufferVectorOffset += 4;
				Buffer.m_Size		= Memory::Read_U32(BufferVectorOffset);
				BufferVectorOffset += 4;
				PayloadBuffer.push_back(Buffer);
				DEBUG_LOG(WII_IPC_HLE, "SIOCtlVBuffer io%i: 0x%08x, 0x%x",
							i, Buffer.m_Address, Buffer.m_Size);
			}
		}

		const u32 m_Address;

		u32 Parameter;
		u32 NumberInBuffer;
		u32 NumberPayloadBuffer;
		u32 BufferVector;

		struct SBuffer { u32 m_Address, m_Size; };
		std::vector<SBuffer> InBuffer;
		std::vector<SBuffer> PayloadBuffer;
	};

	// Write out the IPC struct from _CommandAddress to _NumberOfCommands numbers
	// of 4 byte commands.
	void DumpCommands(u32 _CommandAddress, size_t _NumberOfCommands = 8,
		LogTypes::LOG_TYPE LogType = LogTypes::WII_IPC_HLE,
		LogTypes::LOG_LEVELS Verbosity = LogTypes::DEBUG)
	{
		GENERIC_LOG(LogType, Verbosity, "CommandDump of %s", 
					GetDeviceName().c_str());
		for (u32 i = 0; i < _NumberOfCommands; i++)
		{
			GENERIC_LOG(LogType, Verbosity, "    Command%02i: 0x%08x", i,
						Memory::Read_U32(_CommandAddress + i*4));	
		}
	}
	
	void DumpAsync(u32 BufferVector, u32 NumberInBuffer, u32 NumberOutBuffer,
		LogTypes::LOG_TYPE LogType = LogTypes::WII_IPC_HLE,
		LogTypes::LOG_LEVELS Verbosity = LogTypes::DEBUG)
	{
		GENERIC_LOG(LogType, Verbosity, "======= DumpAsync ======");

		u32 BufferOffset = BufferVector;
		for (u32 i = 0; i < NumberInBuffer; i++)
		{
			u32 InBuffer        = Memory::Read_U32(BufferOffset); BufferOffset += 4;
			u32 InBufferSize    = Memory::Read_U32(BufferOffset); BufferOffset += 4;

			GENERIC_LOG(LogType, LogTypes::INFO, "%s - IOCtlV InBuffer[%i]:",
				GetDeviceName().c_str(), i);

			std::string Temp;
			for (u32 j = 0; j < InBufferSize; j++)
			{
				char Buffer[128];
				sprintf(Buffer, "%02x ", Memory::Read_U8(InBuffer+j));
				Temp.append(Buffer);
			}

			GENERIC_LOG(LogType, LogTypes::DEBUG, "    Buffer: %s", Temp.c_str());
		}

		for (u32 i = 0; i < NumberOutBuffer; i++)
		{
			u32 OutBuffer        = Memory::Read_U32(BufferOffset); BufferOffset += 4;
			u32 OutBufferSize    = Memory::Read_U32(BufferOffset); BufferOffset += 4;

			GENERIC_LOG(LogType, LogTypes::INFO, "%s - IOCtlV OutBuffer[%i]:",
				GetDeviceName().c_str(), i);
			GENERIC_LOG(LogType, LogTypes::INFO, "    OutBuffer: 0x%08x (0x%x):",
				OutBuffer, OutBufferSize);

			#if defined(MAX_LOGLEVEL) && MAX_LOGLEVEL >= LOGTYPES_INFO
			DumpCommands(OutBuffer, OutBufferSize, LogType, Verbosity);
			#endif
		}
	}
};

class CWII_IPC_HLE_Device_stub : public IWII_IPC_HLE_Device
{
public:
	CWII_IPC_HLE_Device_stub(u32 DeviceID, const std::string& Name)
		: IWII_IPC_HLE_Device(DeviceID, Name)
	{
	}

	bool Open(u32 CommandAddress, u32 Mode)
	{
		(void)Mode;
		WARN_LOG(WII_IPC_HLE, "%s faking Open()", m_Name.c_str());
		Memory::Write_U32(GetDeviceID(), CommandAddress + 4);
		m_Active = true;
		return true;
	}
	bool Close(u32 CommandAddress, bool bForce = false)
	{
		WARN_LOG(WII_IPC_HLE, "%s faking Close()", m_Name.c_str());
		if (!bForce)
			Memory::Write_U32(FS_SUCCESS, CommandAddress + 4);
		m_Active = false;
		return true;
	}

	bool IOCtl(u32 CommandAddress)
	{
		WARN_LOG(WII_IPC_HLE, "%s faking IOCtl()", m_Name.c_str());
		Memory::Write_U32(FS_SUCCESS, CommandAddress + 4);
		return true;
	}
	bool IOCtlV(u32 CommandAddress)
	{
		WARN_LOG(WII_IPC_HLE, "%s faking IOCtlV()", m_Name.c_str());
		Memory::Write_U32(FS_SUCCESS, CommandAddress + 4);
		return true;
	}
};

#endif
