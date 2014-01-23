// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h" // Common
#include "ChunkFile.h"
#include "../ConfigManager.h"
#include "../CoreTiming.h"
#include "../HW/SystemTimers.h"

#include "StreamADPCM.h" // Core
#include "DVDInterface.h"
#include "../PowerPC/PowerPC.h"
#include "ProcessorInterface.h"
#include "Thread.h"
#include "Memmap.h"
#include "../VolumeHandler.h"
#include "AudioInterface.h"
#include "../Movie.h"

// Disc transfer rate measured in bytes per second
static const u32 DISC_TRANSFER_RATE_GC = 5 * 1024 * 1024;

// Disc access time measured in milliseconds
static const u32 DISC_ACCESS_TIME_MS = 1;

namespace DVDInterface
{

// internal hardware addresses
enum
{
	DI_STATUS_REGISTER			= 0x00,
	DI_COVER_REGISTER			= 0x04,
	DI_COMMAND_0				= 0x08,
	DI_COMMAND_1				= 0x0C,
	DI_COMMAND_2				= 0x10,
	DI_DMA_ADDRESS_REGISTER		= 0x14,
	DI_DMA_LENGTH_REGISTER		= 0x18, 
	DI_DMA_CONTROL_REGISTER		= 0x1C,
	DI_IMMEDIATE_DATA_BUFFER	= 0x20,
	DI_CONFIG_REGISTER			= 0x24
};


// DVD IntteruptTypes
enum DI_InterruptType
{
	INT_DEINT		= 0,
	INT_TCINT		= 1,
	INT_BRKINT		= 2,
	INT_CVRINT		= 3,
};

// debug commands which may be ORd
enum
{
	STOP_DRIVE	= 0,
	START_DRIVE	= 0x100,
	ACCEPT_COPY	= 0x4000,
	DISC_CHECK	= 0x8000,
};

// DI Status Register
union UDISR	
{
	u32 Hex;
	struct
	{
		u32 BREAK			:  1;	// Stop the Device + Interrupt
		u32 DEINITMASK		:  1;	// Access Device Error Int Mask
		u32 DEINT			:  1;	// Access Device Error Int
		u32 TCINTMASK		:  1;	// Transfer Complete Int Mask
		u32 TCINT			:  1;	// Transfer Complete Int
		u32 BRKINTMASK		:  1;
		u32 BRKINT			:  1;	// w 1: clear brkint
		u32					: 25;
	};
	UDISR() {Hex = 0;}
	UDISR(u32 _hex) {Hex = _hex;}
};

// DI Cover Register
union UDICVR
{
	u32 Hex;
	struct
	{
		u32 CVR				:  1;	// 0: Cover closed	1: Cover open
		u32 CVRINTMASK		:  1;	// 1: Interrupt enabled
		u32 CVRINT			:  1;	// r 1: Interrupt requested w 1: Interrupt clear
		u32					: 29;
	};
	UDICVR() {Hex = 0;}
	UDICVR(u32 _hex) {Hex = _hex;}
};

union UDICMDBUF
{
	u32 Hex;
	struct
	{
		u8 CMDBYTE3;
		u8 CMDBYTE2;
		u8 CMDBYTE1;
		u8 CMDBYTE0;
	};
};

// DI DMA Address Register
union UDIMAR
{
	u32 Hex;
	struct
	{
		u32 Zerobits	:	5; // Must be zero (32byte aligned)
		u32				:	27;
	};
	struct
	{
		u32 Address		:	26;
		u32				:	6;
	};
};

// DI DMA Address Length Register
union UDILENGTH
{
	u32 Hex;
	struct
	{
		u32 Zerobits	:	5; // Must be zero (32byte aligned)
		u32				:	27;
	};
	struct
	{
		u32 Length		:	26;
		u32				:	6;
	};
};

// DI DMA Control Register
union UDICR
{
	u32 Hex;
	struct
	{
		u32 TSTART		:	1;	// w:1 start   r:0 ready
		u32 DMA			:	1;	// 1: DMA Mode    0: Immediate Mode (can only do Access Register Command)
		u32 RW			:	1;	// 0: Read Command (DVD to Memory)  1: Write Command (Memory to DVD)
		u32				:  29;
	};
};

union UDIIMMBUF
{
	u32 Hex;
	struct
	{
		u8 REGVAL3;
		u8 REGVAL2;
		u8 REGVAL1;
		u8 REGVAL0;
	};
};

// DI Config Register
union UDICFG
{
	u32 Hex;
	struct
	{
		u32 CONFIG		:	8;
		u32				:  24;
	};
	UDICFG() {Hex = 0;}
	UDICFG(u32 _hex) {Hex = _hex;}
};


// STATE_TO_SAVE
// hardware registers
static UDISR		m_DISR;
static UDICVR		m_DICVR;
static UDICMDBUF	m_DICMDBUF[3];
static UDIMAR		m_DIMAR;
static UDILENGTH	m_DILENGTH;
static UDICR		m_DICR;
static UDIIMMBUF	m_DIIMMBUF;
static UDICFG		m_DICFG;

static u32			LoopStart;
static u32			AudioPos;
static u32			CurrentStart;
static u32			LoopLength;
static u32			CurrentLength;

static FILE			*m_netcfg;
static FILE			*m_netctrl;
static FILE			*m_dimm;
static u32			m_protocolversion;

u32	 g_ErrorCode = 0;
bool g_bDiscInside = false;
bool g_bStream = false;
int  tc = 0;

// GC-AM only
static unsigned char media_buffer[0x60];
static unsigned char media_reply_buffer[0x20];

// Needed because data and streaming audio access needs to be managed by the "drive"
// (both requests can happen at the same time, audio takes precedence)
static std::mutex dvdread_section;

static int ejectDisc;
static int insertDisc;

void EjectDiscCallback(u64 userdata, int cyclesLate);
void InsertDiscCallback(u64 userdata, int cyclesLate);

void UpdateInterrupts();
void GenerateDIInterrupt(DI_InterruptType _DVDInterrupt);
void ExecuteCommand(UDICR& _DICR);

void DoState(PointerWrap &p)
{
	p.DoPOD(m_DISR);
	p.DoPOD(m_DICVR);
	p.DoArray(m_DICMDBUF, 3);
	p.Do(m_DIMAR);
	p.Do(m_DILENGTH);
	p.Do(m_DICR);
	p.Do(m_DIIMMBUF);
	p.DoPOD(m_DICFG);

	p.Do(LoopStart);
	p.Do(AudioPos);
	p.Do(LoopLength);

	p.Do(g_ErrorCode);
	p.Do(g_bDiscInside);
	p.Do(g_bStream);

	p.Do(CurrentStart);
	p.Do(CurrentLength);
}

void TransferComplete(u64 userdata, int cyclesLate)
{
	if (m_DICR.TSTART)
		ExecuteCommand(m_DICR);
}

void Init()
{
	char FilePath[_MAX_PATH];

	m_DISR.Hex		= 0;
	m_DICVR.Hex		= 0;
	m_DICMDBUF[0].Hex= 0;
	m_DICMDBUF[1].Hex= 0;
	m_DICMDBUF[2].Hex= 0;
	m_DIMAR.Hex		= 0;
	m_DILENGTH.Hex	= 0;
	m_DICR.Hex		= 0;
	m_DIIMMBUF.Hex	= 0;
	m_DICFG.Hex		= 0;
	m_DICFG.CONFIG	= 1; // Disable bootrom descrambler

	AudioPos = 0;
	LoopStart = 0;
	LoopLength = 0;
	CurrentStart = 0;
	CurrentLength = 0;

	m_protocolversion = 2;

	memset( media_buffer, 0, sizeof(media_buffer) );
	
	m_netcfg = fopen("User/trinetcfg.bin","rb+");
	if( m_netcfg == (FILE*)NULL )
	{
		m_netcfg = fopen("User/trinetcfg.bin","wb+");
		if( m_netcfg == (FILE*)NULL )
		{
			PanicAlertT( "AM-BB Failed to open network config file" );
		}
	}

	m_netctrl = fopen("User/trinetctrl.bin","rb+");
	if( m_netctrl == (FILE*)NULL )
	{
		m_netctrl = fopen("User/trinetctrl.bin","wb+");
		if( m_netctrl == (FILE*)NULL )
		{
			PanicAlertT( "AM-BB Failed to open network control file" );
		}
	}

	sprintf_s( FilePath, _MAX_PATH, "User/tridimm_%s.bin", SConfig::GetInstance().m_LocalCoreStartupParameter.GetUniqueID().c_str() );

	m_dimm = fopen( FilePath, "rb+" );
	if( m_dimm == (FILE*)NULL )
	{
		m_dimm = fopen( FilePath, "wb+" );
		if( m_dimm == (FILE*)NULL )
		{
			PanicAlertT( "AM-BB Failed to open DIMM file" );
		}
	}	
	
	g_bStream = false;

	ejectDisc = CoreTiming::RegisterEvent("EjectDisc", EjectDiscCallback);
	insertDisc = CoreTiming::RegisterEvent("InsertDisc", InsertDiscCallback);

	tc = CoreTiming::RegisterEvent("TransferComplete", TransferComplete);
}

void Shutdown()
{
}

void SetDiscInside(bool _DiscInside)
{
	g_bDiscInside = _DiscInside;
}

bool IsDiscInside()
{	
	return g_bDiscInside;
}

// Take care of all logic of "swapping discs"
// We want this in the "backend", NOT the gui
// any !empty string will be deleted to ensure
// that the userdata string exists when called
void EjectDiscCallback(u64 userdata, int cyclesLate)
{
	// Empty the drive
	SetDiscInside(false);
	SetLidOpen();
	VolumeHandler::EjectVolume();
}

void InsertDiscCallback(u64 userdata, int cyclesLate)
{
	std::string& SavedFileName = SConfig::GetInstance().m_LocalCoreStartupParameter.m_strFilename;
	std::string *_FileName = (std::string *)userdata;

	if (!VolumeHandler::SetVolumeName(*_FileName))
	{
		// Put back the old one
		VolumeHandler::SetVolumeName(SavedFileName);
		PanicAlertT("Invalid file");
	}
	SetLidOpen(false);
	SetDiscInside(VolumeHandler::IsValid());
	delete _FileName;
}

void ChangeDisc(const char* _newFileName)
{
	std::string* _FileName = new std::string(_newFileName);
	CoreTiming::ScheduleEvent_Threadsafe(0, ejectDisc);
	CoreTiming::ScheduleEvent_Threadsafe(500000000, insertDisc, (u64)_FileName);
	if (Movie::IsRecordingInput())
	{
		Movie::g_bDiscChange = true;
		std::string fileName = _newFileName;
		int sizeofpath = fileName.find_last_of("/\\") + 1;
		if (fileName.substr(sizeofpath).length() > 40)
		{
			PanicAlert("Saving iso filename to .dtm failed; max file name length is 40 characters.");
		}
		Movie::g_discChange = fileName.substr(sizeofpath);
	}
}

void SetLidOpen(bool _bOpen)
{
	m_DICVR.CVR = _bOpen ? 1 : 0;

	GenerateDIInterrupt(INT_CVRINT);
}

bool IsLidOpen()
{	
	return (m_DICVR.CVR == 1);
}

void ClearCoverInterrupt()
{
	m_DICVR.CVRINT = 0;
}

bool DVDRead(u32 _iDVDOffset, u32 _iRamAddress, u32 _iLength)
{
	// We won't need the crit sec when DTK streaming has been rewritten correctly.
	std::lock_guard<std::mutex> lk(dvdread_section);
	return VolumeHandler::ReadToPtr(Memory::GetPointer(_iRamAddress), _iDVDOffset, _iLength);
}

bool DVDReadADPCM(u8* _pDestBuffer, u32 _iNumSamples)
{
	_iNumSamples &= ~31;

	if (AudioPos == 0)
	{
		memset(_pDestBuffer, 0, _iNumSamples); // probably __AI_SRC_INIT :P
	}
	else
	{
		std::lock_guard<std::mutex> lk(dvdread_section);
		VolumeHandler::ReadToPtr(_pDestBuffer, AudioPos, _iNumSamples);
	}

	// loop check
	if (g_bStream)
	{
		AudioPos += _iNumSamples;

		if (AudioPos >= CurrentStart + CurrentLength)
		{
			if (LoopStart == 0)
			{
				AudioPos = 0;
				CurrentStart = 0;
				CurrentLength = 0;
			}
			else
			{
				AudioPos = LoopStart;
				CurrentStart = LoopStart;
				CurrentLength = LoopLength;
			}
			NGCADPCM::InitFilter();
			AudioInterface::GenerateAISInterrupt();
		}

		//WARN_LOG(DVDINTERFACE,"ReadADPCM");
		return true;
	}
	else
	{
		return false;
	}
}

void Read32(u32& _uReturnValue, const u32 _iAddress)
{
	switch (_iAddress & 0xFF)
	{
	case DI_STATUS_REGISTER:		_uReturnValue = m_DISR.Hex; break;
	case DI_COVER_REGISTER:			_uReturnValue = m_DICVR.Hex; break;
	case DI_COMMAND_0:				_uReturnValue = m_DICMDBUF[0].Hex; break;
	case DI_COMMAND_1:				_uReturnValue = m_DICMDBUF[1].Hex; break;
	case DI_COMMAND_2:				_uReturnValue = m_DICMDBUF[2].Hex; break;
	case DI_DMA_ADDRESS_REGISTER:	_uReturnValue = m_DIMAR.Hex; break;
	case DI_DMA_LENGTH_REGISTER:	_uReturnValue = m_DILENGTH.Hex; break;
	case DI_DMA_CONTROL_REGISTER:	_uReturnValue = m_DICR.Hex; break;
	case DI_IMMEDIATE_DATA_BUFFER:	_uReturnValue = m_DIIMMBUF.Hex; break;
	case DI_CONFIG_REGISTER:		_uReturnValue = m_DICFG.Hex; break;

	default:
		_dbg_assert_(DVDINTERFACE, 0);
		_uReturnValue = 0;
		break;
	}
	DEBUG_LOG(DVDINTERFACE, "(r32): 0x%08x - 0x%08x", _iAddress, _uReturnValue);
}

void Write32(const u32 _iValue, const u32 _iAddress)
{
	DEBUG_LOG(DVDINTERFACE, "(w32): 0x%08x @ 0x%08x", _iValue, _iAddress);

	switch (_iAddress & 0xFF)
	{
	case DI_STATUS_REGISTER:
		{
			UDISR tmpStatusReg(_iValue);

			m_DISR.DEINITMASK	= tmpStatusReg.DEINITMASK;
			m_DISR.TCINTMASK	= tmpStatusReg.TCINTMASK;
			m_DISR.BRKINTMASK	= tmpStatusReg.BRKINTMASK;
			m_DISR.BREAK		= tmpStatusReg.BREAK;

			if (tmpStatusReg.DEINT)
				m_DISR.DEINT = 0;

			if (tmpStatusReg.TCINT)
				m_DISR.TCINT = 0;

			if (tmpStatusReg.BRKINT)
				m_DISR.BRKINT = 0;

			if (m_DISR.BREAK)
			{
				_dbg_assert_(DVDINTERFACE, 0);
			}
			
			UpdateInterrupts();
		}
		break;

	case DI_COVER_REGISTER:
		{
			UDICVR tmpCoverReg(_iValue);

			m_DICVR.CVRINTMASK = tmpCoverReg.CVRINTMASK;

			if (tmpCoverReg.CVRINT)
				m_DICVR.CVRINT = 0;

			UpdateInterrupts();
		}
		break;

	case DI_COMMAND_0:		m_DICMDBUF[0].Hex = _iValue; break;
	case DI_COMMAND_1:		m_DICMDBUF[1].Hex = _iValue; break;
	case DI_COMMAND_2:		m_DICMDBUF[2].Hex = _iValue; break;

	case DI_DMA_ADDRESS_REGISTER:
		{
			m_DIMAR.Hex = _iValue & ~0xfc00001f;
		}
		break;
	case DI_DMA_LENGTH_REGISTER:
		{
			m_DILENGTH.Hex = _iValue & ~0x1f;
		}
		break;
	case DI_DMA_CONTROL_REGISTER:	
		{
			m_DICR.Hex = _iValue & 7;
			if (m_DICR.TSTART)
			{
				if (!SConfig::GetInstance().m_LocalCoreStartupParameter.bFastDiscSpeed)
				{
					u64 ticksUntilTC = m_DILENGTH.Length * 
						(SystemTimers::GetTicksPerSecond() / (SConfig::GetInstance().m_LocalCoreStartupParameter.bWii ? 1 : DISC_TRANSFER_RATE_GC)) +
						(SystemTimers::GetTicksPerSecond() * DISC_ACCESS_TIME_MS / 1000);
					CoreTiming::ScheduleEvent((int)ticksUntilTC, tc);
				}
				else
				{
					ExecuteCommand(m_DICR);
				}
			}
		}
		break;

	case DI_IMMEDIATE_DATA_BUFFER:	m_DIIMMBUF.Hex = _iValue; break;

	case DI_CONFIG_REGISTER:
		{
			WARN_LOG(DVDINTERFACE, "Write to DICFG, ignored as it's read-only");
		}
		break;

	default:
		_dbg_assert_msg_(DVDINTERFACE, 0, "Write to unknown DI address 0x%08x", _iAddress);
		break;
	}
}

void UpdateInterrupts()
{
	if ((m_DISR.DEINT	& m_DISR.DEINITMASK) ||
		(m_DISR.TCINT	& m_DISR.TCINTMASK)  ||
		(m_DISR.BRKINT	& m_DISR.BRKINTMASK) ||
		(m_DICVR.CVRINT	& m_DICVR.CVRINTMASK))
	{
		ProcessorInterface::SetInterrupt(ProcessorInterface::INT_CAUSE_DI, true);
	}
	else
	{
		ProcessorInterface::SetInterrupt(ProcessorInterface::INT_CAUSE_DI, false);
	}

	// Required for Summoner: A Goddess Reborn
	CoreTiming::ForceExceptionCheck(50);
}

void GenerateDIInterrupt(DI_InterruptType _DVDInterrupt)
{
	switch(_DVDInterrupt) 
	{
	case INT_DEINT:		m_DISR.DEINT	= 1; break;
	case INT_TCINT:		m_DISR.TCINT	= 1; break;
	case INT_BRKINT:	m_DISR.BRKINT	= 1; break;
	case INT_CVRINT:	m_DICVR.CVRINT	= 1; break;
	}

	UpdateInterrupts();
}

void ExecuteCommand(UDICR& _DICR)
{
//	_dbg_assert_(DVDINTERFACE, _DICR.RW == 0); // only DVD to Memory
	int GCAM = ((SConfig::GetInstance().m_SIDevice[0] == SIDEVICE_AM_BASEBOARD)
		&& (SConfig::GetInstance().m_EXIDevice[2] == EXIDEVICE_AM_BASEBOARD))
		? 1 : 0;

	if (GCAM)
	{
		m_DICMDBUF[0].Hex <<= 24;

		NOTICE_LOG(DVDINTERFACE,
			"DVD: %08x, %08x, %08x, DMA=addr:%08x,len:%08x,ctrl:%08x",
			m_DICMDBUF[0].Hex, m_DICMDBUF[1].Hex<<2, m_DICMDBUF[2].Hex,
			m_DIMAR.Hex, m_DILENGTH.Hex, m_DICR.Hex);
	}


	switch (m_DICMDBUF[0].CMDBYTE0)
	{
	case DVDLowInquiry:
		if (GCAM)
		{
			if( m_protocolversion == 2 )
				m_DIIMMBUF.Hex = 0xA9484100;
			else
				m_DIIMMBUF.Hex = 0x21000000;
		}
		else
		{
			// small safety check, dunno if it's needed
			if ((m_DICMDBUF[1].Hex == 0) && (m_DILENGTH.Length == 0x20))
			{
				u8* driveInfo = Memory::GetPointer(m_DIMAR.Address);
				// gives the correct output in GCOS - 06 2001/08 (61)
				// there may be other stuff missing ?
				driveInfo[4] = 0x20;
				driveInfo[5] = 0x01;
				driveInfo[6] = 0x06;
				driveInfo[7] = 0x08;
				driveInfo[8] = 0x61;

				// Just for fun
				INFO_LOG(DVDINTERFACE, "Drive Info: %02x %02x%02x/%02x (%02x)",
					driveInfo[6], driveInfo[4], driveInfo[5], driveInfo[7], driveInfo[8]);
			}
		}
		break;

	// "Set Extension"...not sure what it does
	case 0x55:
		INFO_LOG(DVDINTERFACE, "SetExtension");
		break;

	// DMA Read from Disc
	case 0xA8:
		if (g_bDiscInside)
		{
			switch (m_DICMDBUF[0].CMDBYTE3)
			{
			case 0x00: // Read Sector
				{
					u32 iDVDOffset = m_DICMDBUF[1].Hex << 2;

					DEBUG_LOG(DVDINTERFACE, "Read: DVDOffset=%08x, DMABuffer=%08x, SrcLength=%08x, DMALength=%08x",
						iDVDOffset, m_DIMAR.Address, m_DICMDBUF[2].Hex, m_DILENGTH.Length);
					_dbg_assert_(DVDINTERFACE, m_DICMDBUF[2].Hex == m_DILENGTH.Length);

					if (GCAM)
					{
						u32 len = m_DILENGTH.Length / 4;
						if (iDVDOffset & 0x80000000) // read request to hardware buffer
						{
							switch (iDVDOffset)
							{
							case 0x80000000:
								NOTICE_LOG(DVDINTERFACE, "GC-AM: READ MEDIA BOARD STATUS (80000000)");
								for (u32 i = 0; i < len; i++)
									Memory::Write_U32(0, m_DIMAR.Address + i * 4);
								break;
							case 0x84000020:
								NOTICE_LOG(DVDINTERFACE, "GC-AM: READ MEDIA BOARD STATUS (1) (84000020)");
								if( m_protocolversion == 2 )
								{
									memcpy(Memory::GetPointer(m_DIMAR.Address), media_reply_buffer, m_DILENGTH.Length );
	
									NOTICE_LOG(DVDINTERFACE, "GC-AM: %08x %08x %08x %08x",	Memory::Read_U32(m_DIMAR.Address),
																							Memory::Read_U32(m_DIMAR.Address+4),
																							Memory::Read_U32(m_DIMAR.Address+8),
																							Memory::Read_U32(m_DIMAR.Address+12) );
									NOTICE_LOG(DVDINTERFACE, "GC-AM: %08x %08x %08x %08x",	Memory::Read_U32(m_DIMAR.Address+16),
																							Memory::Read_U32(m_DIMAR.Address+20),
																							Memory::Read_U32(m_DIMAR.Address+24),
																							Memory::Read_U32(m_DIMAR.Address+28) );
								}
								else
								{
									for (u32 i = 0; i < len; i++)
										Memory::Write_U32(0, m_DIMAR.Address + i * 4);
								}	
								break;
							case 0x80000040:
								NOTICE_LOG(DVDINTERFACE, "GC-AM: READ MEDIA BOARD STATUS (2) (80000040)");
								for (u32 i = 0; i < len; i++)
									Memory::Write_U32(~0, m_DIMAR.Address + i * 4);
								Memory::Write_U32(0x00000020, m_DIMAR.Address); // DIMM SIZE, LE
								Memory::Write_U32(0x4743414D, m_DIMAR.Address + 4); // GCAM signature
								break;
							case 0x80000120:
								NOTICE_LOG(DVDINTERFACE, "GC-AM: READ FIRMWARE STATUS (80000120)");
								for (u32 i = 0; i < len; i++)
									Memory::Write_U32(0x01010101, m_DIMAR.Address + i * 4);
								break;
							case 0x80000140:
								NOTICE_LOG(DVDINTERFACE, "GC-AM: READ FIRMWARE STATUS (80000140)");
								for (u32 i = 0; i < len; i++)
									Memory::Write_U32(0x01010101, m_DIMAR.Address + i * 4);
								break;
							default:
								NOTICE_LOG(DVDINTERFACE, "GC-AM: UNKNOWN MEDIA BOARD LOCATION %x", iDVDOffset);
								break;
							}
							break;
						}
						else if( (iDVDOffset >= 0x1F000000) && (iDVDOffset <= 0x1F300000) )
						{
							u32 Offset = iDVDOffset - 0x1F000000;

							NOTICE_LOG(DVDINTERFACE, "GC-AM: READ MEDIA BOARD DIMM MEMORY (%08X)", Offset );
											
							fseek( m_dimm, Offset, SEEK_SET );
							fread( Memory::GetPointer(m_DIMAR.Address), sizeof(char), len, m_dimm );				
						}
						else if ((iDVDOffset == 0x1f900000) || (iDVDOffset == 0x1f900020))
						{
							NOTICE_LOG(DVDINTERFACE, "GC-AM: READ MEDIA BOARD COMM AREA (1F900020)");
							memcpy(Memory::GetPointer(m_DIMAR.Address), media_buffer + iDVDOffset - 0x1f900000, m_DILENGTH.Length);
							NOTICE_LOG(DVDINTERFACE, "GC-AM: %08x %08x %08x %08x",	Memory::Read_U32(m_DIMAR.Address),
																					Memory::Read_U32(m_DIMAR.Address+4),
																					Memory::Read_U32(m_DIMAR.Address+8),
																					Memory::Read_U32(m_DIMAR.Address+12) );
							NOTICE_LOG(DVDINTERFACE, "GC-AM: %08x %08x %08x %08x",	Memory::Read_U32(m_DIMAR.Address+16),
																					Memory::Read_U32(m_DIMAR.Address+20),
																					Memory::Read_U32(m_DIMAR.Address+24),
																					Memory::Read_U32(m_DIMAR.Address+28) );
							break;
						} else if( iDVDOffset == 0x1FFEFFE0 )
						{
							NOTICE_LOG(DVDINTERFACE, "GC-AM: READ MEDIA BOARD NETWORK CONTROL (0x1FFEFFE0)");
								for (u32 i = 0; i < len; i++)
									Memory::Write_U32(0x00000000, m_DIMAR.Address + i * 4);

							fseek( m_netctrl, 0, SEEK_SET );
							fread( Memory::GetPointer(m_DIMAR.Address), sizeof(char), m_DILENGTH.Length, m_netctrl );

							break;
						} else if( (iDVDOffset == 0) && (m_DILENGTH.Length == 0x80) ) {
							
							NOTICE_LOG(DVDINTERFACE, "GC-AM: READ MEDIA BOARD NETWORK CONFIG");

							fseek( m_netcfg, 0, SEEK_SET );
							fread( Memory::GetPointer(m_DIMAR.Address), sizeof(char), m_DILENGTH.Length, m_netcfg );

							break;
						}
					}

					// Here is the actual Disk Reading
					if (!DVDRead(iDVDOffset, m_DIMAR.Address, m_DILENGTH.Length))
					{
						PanicAlertT("Can't read from DVD_Plugin - DVD-Interface: Fatal Error");
					}
				}
				break;

			case 0x40: // Read DiscID
			{
				_dbg_assert_(DVDINTERFACE, m_DICMDBUF[1].Hex == 0);
				_dbg_assert_(DVDINTERFACE, m_DICMDBUF[2].Hex == m_DILENGTH.Length);
				_dbg_assert_(DVDINTERFACE, m_DILENGTH.Length == 0x20);
				if (!DVDRead(m_DICMDBUF[1].Hex, m_DIMAR.Address, m_DILENGTH.Length))
					PanicAlertT("Can't read from DVD_Plugin - DVD-Interface: Fatal Error");
				WARN_LOG(DVDINTERFACE, "Read DiscID %08x", Memory::Read_U32(m_DIMAR.Address));
				} break;

			default:
				_dbg_assert_msg_(DVDINTERFACE, 0, "Unknown Read Subcommand");
				break;
			}
		}
		else
		{
			// there is no disc to read
			_DICR.TSTART = 0;
			m_DILENGTH.Length = 0;
			g_ErrorCode = ERROR_NO_DISK | ERROR_COVER_H;
			GenerateDIInterrupt(INT_DEINT);
			return;
		}
		break;

	// GC-AM
	case 0xAA:
		if (GCAM)
		{
			ERROR_LOG(DVDINTERFACE, "GC-AM: 0xAA, DMABuffer=%08x, DMALength=%08x", m_DIMAR.Address, m_DILENGTH.Length);
			u32 iDVDOffset = m_DICMDBUF[1].Hex << 2;
			unsigned int len = m_DILENGTH.Length; 
			u32 offset = iDVDOffset - 0x1F900000;
			/*
			if (iDVDOffset == 0x84800000)
			{
				ERROR_LOG(DVDINTERFACE, "firmware upload");
			}
			else*/
			
			// Protocol version 2.xx and higher
			if( (iDVDOffset >= 0x84000000) && (iDVDOffset < 0x84000060) )
			{
				offset = iDVDOffset - 0x84000000;

				u32 addr = m_DIMAR.Address;
				memcpy(media_buffer + offset, Memory::GetPointer(addr), len);

				if( (iDVDOffset == 0x84000040) && (media_buffer[0x40] == 1 ) )
				{
					GCAMExecuteCommand();
				}

				while (len >= 4)
				{
					ERROR_LOG(DVDINTERFACE, "GC-AM Media Board WRITE (0xAA): %08x: %08x", iDVDOffset, Memory::Read_U32(addr));
					addr += 4;
					len -= 4;
					iDVDOffset += 4;
				}

			}
			// Set IP address
			else if( iDVDOffset == 0x1F800200 )
			{
				NOTICE_LOG(DVDINTERFACE, "MEDIA BOARD SET IP:%.13s", Memory::GetPointer(m_DIMAR.Address) );					
			}
			// DIMM memory
			else if( (iDVDOffset >= 0x1F000000) && (iDVDOffset <= 0x1F300000) )
			{
				u32 Offset = iDVDOffset - 0x1F000000;
				NOTICE_LOG(DVDINTERFACE, "WRITE MEDIA BOARD DIMM MEMORY (%08X)", Offset );				
				fseek( m_dimm, Offset, SEEK_SET );
				fwrite( Memory::GetPointer(m_DIMAR.Address), sizeof(char), len, m_dimm );
				fflush( m_dimm );
			} 
			else if( (iDVDOffset >= 0) && (iDVDOffset <= 0x80) )
			{
				NOTICE_LOG(DVDINTERFACE, "GC-AM Media Board WRITE NETWORK CONFIG Type:%u Remote:%u", Memory::Read_U16(m_DIMAR.Address) , Memory::Read_U16(m_DIMAR.Address+2) );
				fseek( m_netcfg, iDVDOffset, SEEK_SET );
				fwrite( Memory::GetPointer(m_DIMAR.Address), sizeof(char), len, m_netcfg );
				fflush( m_netcfg );
			}
			else if ((offset < 0) || ((offset + len) > 0x40) || len > 0x40)
			{
				u32 addr = m_DIMAR.Address;
				if (iDVDOffset == 0x84800000) {
					ERROR_LOG(DVDINTERFACE, "FIRMWARE UPLOAD");
				} else {
					ERROR_LOG(DVDINTERFACE, "ILLEGAL MEDIA WRITE");
				}
				while (len >= 4)
				{
					ERROR_LOG(DVDINTERFACE, "GC-AM Media Board WRITE (0xAA): %08x: %08x (ignored)", iDVDOffset, Memory::Read_U32(addr));
					addr += 4;
					len -= 4;
					iDVDOffset += 4;
				}
			}
			else
			{
				u32 addr = m_DIMAR.Address;
				memcpy(media_buffer + offset, Memory::GetPointer(addr), len);
				//while (len >= 4)
				//{
				//	ERROR_LOG(DVDINTERFACE, "GC-AM Media Board WRITE (0xAA): %08x: %08x", iDVDOffset, Memory::Read_U32(addr));
				//	addr += 4;
				//	len -= 4;
				//	iDVDOffset += 4;
				//}
			}
		}
		break;

	// Seek (immediate)
	case DVDLowSeek:
		if (!GCAM)
		{
			// We don't care :)
			DEBUG_LOG(DVDINTERFACE, "Seek: offset=%08x (ignoring)", m_DICMDBUF[1].Hex << 2);
		}
		else
		{
			GCAMExecuteCommand();
			m_DIIMMBUF.Hex = 0x66556677; // just a random value that works.
		}
		break;

	case DVDLowOffset:
		DEBUG_LOG(DVDINTERFACE, "DVDLowOffset: ignoring...");
		break;

	// Request Error Code
	case DVDLowRequestError:
		NOTICE_LOG(DVDINTERFACE, "Requesting error... (0x%08x)", g_ErrorCode);
		m_DIIMMBUF.Hex = g_ErrorCode;
		break;

	// Audio Stream (Immediate)
	//	m_DICMDBUF[0].CMDBYTE1		= subcommand
	//	m_DICMDBUF[1].Hex << 2		= offset on disc 
	//	m_DICMDBUF[2].Hex			= Length of the stream
	case 0xE1:
		{
			u32 pos = m_DICMDBUF[1].Hex << 2;
			u32 length = m_DICMDBUF[2].Hex;

			// Start playing
			if (!g_bStream && m_DICMDBUF[0].CMDBYTE1 == 0 && pos != 0 && length != 0)
			{
				AudioPos = pos;
				CurrentStart = pos;
				CurrentLength = length;
				NGCADPCM::InitFilter();
				g_bStream = true;
			}

			LoopStart = pos;
			LoopLength = length;
			g_bStream = (m_DICMDBUF[0].CMDBYTE1 == 0); // This command can start/stop the stream

			// Stop stream
			if (m_DICMDBUF[0].CMDBYTE1 == 1)
			{
				AudioPos = 0;
				LoopStart = 0;
				LoopLength = 0;
				CurrentStart = 0;
				CurrentLength = 0;
			}

			WARN_LOG(DVDINTERFACE, "(Audio) Stream subcmd = %08x offset = %08x length=%08x",
				m_DICMDBUF[0].Hex, m_DICMDBUF[1].Hex << 2, m_DICMDBUF[2].Hex);
		}
		break;

	// Request Audio Status (Immediate)
	case 0xE2:
		{
			switch (m_DICMDBUF[0].CMDBYTE1)
			{
			case 0x00: // Returns streaming status
				m_DIIMMBUF.Hex = (AudioPos == 0) ? 0 : 1;
				break;
			case 0x01: // Returns the current offset
				if (g_bStream)
					m_DIIMMBUF.Hex = (AudioPos - CurrentStart) >> 2;
				else
					m_DIIMMBUF.Hex = 0;
				break;
			case 0x02: // Returns the start offset
				if (g_bStream)
					m_DIIMMBUF.Hex = CurrentStart >> 2;
				else
					m_DIIMMBUF.Hex = 0;
				break;
			case 0x03: // Returns the total length
				if (g_bStream)
					m_DIIMMBUF.Hex = CurrentLength;
				else
					m_DIIMMBUF.Hex = 0;
				break;
			default:
				WARN_LOG(DVDINTERFACE, "(Audio): Subcommand: %02x  Request Audio status %s", m_DICMDBUF[0].CMDBYTE1, g_bStream? "on":"off");
				break;
			}
		}
		break;

	case DVDLowStopMotor:
		DEBUG_LOG(DVDINTERFACE, "Stop motor");
		break;

	// DVD Audio Enable/Disable (Immediate)
	case DVDLowAudioBufferConfig:
		if (m_DICMDBUF[0].CMDBYTE1 == 1)
		{
			g_bStream = true;
			WARN_LOG(DVDINTERFACE, "(Audio): Audio enabled");
		}
		else
		{
			g_bStream = false;
			WARN_LOG(DVDINTERFACE, "(Audio): Audio disabled");
		}
		break;

	// yet another command we prolly don't care about
	case 0xEE:
		DEBUG_LOG(DVDINTERFACE, "SetStatus - Unimplemented");
		break;

	// Debug commands; see yagcd. We don't really care
	// NOTE: commands to stream data will send...a raw data stream
	// This will appear as unknown commands, unless the check is re-instated to catch such data.
	case 0xFE:
		INFO_LOG(DVDINTERFACE, "Unsupported DVD Drive debug command 0x%08x", m_DICMDBUF[0].Hex);
		break;

	// Unlock Commands. 1: "MATSHITA" 2: "DVD-GAME"
	// Just for fun
	case 0xFF:
		{
			if (m_DICMDBUF[0].Hex == 0xFF014D41
				&& m_DICMDBUF[1].Hex == 0x54534849
				&& m_DICMDBUF[2].Hex == 0x54410200)
			{
				INFO_LOG(DVDINTERFACE, "Unlock test 1 passed");
			}
			else if (m_DICMDBUF[0].Hex == 0xFF004456
				&& m_DICMDBUF[1].Hex == 0x442D4741
				&& m_DICMDBUF[2].Hex == 0x4D450300)
			{
				INFO_LOG(DVDINTERFACE, "Unlock test 2 passed");
			}
			else
			{
				INFO_LOG(DVDINTERFACE, "Unlock test failed");
			}
		}
		break;

	default:
	//	PanicAlertT("Unknown DVD command %08x - fatal error", m_DICMDBUF[0].Hex);
	//	_dbg_assert_(DVDINTERFACE, 0);
		break;
	}

	// transfer is done
	_DICR.TSTART = 0;
	m_DILENGTH.Length = 0;
	GenerateDIInterrupt(INT_TCINT);
	g_ErrorCode = 0;
}

static void GCAMExecuteCommand( void )
{
	memset(media_reply_buffer, 0, 0x20);

	media_reply_buffer[0] = media_buffer[0x20]; // ID
	media_reply_buffer[4] = media_buffer[0x22];
	media_reply_buffer[5] = media_buffer[0x23] | 0x80;
	int cmd = (media_buffer[0x23]<<8)|media_buffer[0x22];
	ERROR_LOG(DVDINTERFACE, "GC-AM: execute buffer, cmd=%04x", cmd);
	switch (cmd)
	{
	case 0x00:
		break;
	case 0x01:
		// DIMM size
		media_reply_buffer[7] = 0x20;
		break;
	case 0x100:		
		// Status
		media_reply_buffer[4] = 0x05;
		// Progress in %
		media_reply_buffer[8] = 0x64;
		break;		
	case 0x101:
		// Media board version: 3.03
		media_reply_buffer[4] = 3;
		media_reply_buffer[5] = 3; 
		media_reply_buffer[6] = 1; // xxx
		media_reply_buffer[8] = 1;
		media_reply_buffer[16] = 0xFF;
		media_reply_buffer[17] = 0xFF;
		media_reply_buffer[18] = 0xFF;
		media_reply_buffer[19] = 0xFF;
		break;
	// System flags (Region,DevFlag)
	case 0x102:
		// Error: 
		// 0 (E01) Media Board doesn't support game
		// 1 (E15) "
		// 2 OK
		media_reply_buffer[4] = 2;
		media_reply_buffer[5] = 0;
		// enable development mode (Sega Boot)
		media_reply_buffer[6] = 1;
		break;
	case 0x103:
		// Media board Serial
		memcpy(media_reply_buffer + 4, "A89E27A50364511", 15);
		break;
	case 0x104:
		media_reply_buffer[4] = 1;
		break;
	// Media Board Test
	case 0x301:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x301: (%08X)", *(u32*)(media_buffer+0x24) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x28) );

		Memory::Write_U32( 100, *(u32*)(media_buffer+0x28) );

		*(u32*)(media_buffer+0x04) = *(u32*)(media_buffer+0x24);
		break;
	case 0x401:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x401: (%08X)", *(u32*)(media_buffer+0x24) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x28) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x2C) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x30) );
		break;
	case 0x403:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x403: (%08X)", *(u32*)(media_buffer+0x24) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x28) );
		media_buffer[4] = 1;
		break;
	case 0x404:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x404: (%08X)", *(u32*)(media_buffer+0x2C) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x30) );
		break;
	case 0x408:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x408: (%08X)", *(u32*)(media_buffer+0x24) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x28) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x2C) );
		break;
	case 0x40B:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x40B: (%08X)", *(u32*)(media_buffer+0x24) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x28) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x2C) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x30) );
		break;
	case 0x40C:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x40C: (%08X)", *(u32*)(media_buffer+0x24) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x28) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x2C) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x30) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x34) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x38) );
		break;
	case 0x40E:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x40E: (%08X)", *(u32*)(media_buffer+0x28) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x2C) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x30) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x34) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x38) );
		break;
	case 0x410:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x410: (%08X)", *(u32*)(media_buffer+0x28) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x2C) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x30) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x34) );
		break;
	case 0x411:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x411: (%08X)", *(u32*)(media_buffer+0x24) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x28) );

		*(u32*)(media_reply_buffer+4) = 0x46;
		break;
	case 0x415:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x415: (%08X)", *(u32*)(media_buffer+0x24) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x28) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x2C) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x30) );
		break;
	case 0x601:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x601");
		break;
	case 0x606:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x606: (%04X)", *(u16*)(media_buffer+0x24) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%04X)", *(u16*)(media_buffer+0x26) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%02X)", *( u8*)(media_buffer+0x28) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%02X)", *( u8*)(media_buffer+0x29) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%04X)", *(u16*)(media_buffer+0x2A) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x2C) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x30) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x34) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x38) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x3C) );
		break;
	case 0x607:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x607: (%04X)", *(u16*)(media_buffer+0x24) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%04X)", *(u16*)(media_buffer+0x26) );
		ERROR_LOG(DVDINTERFACE, "GC-AM:        (%08X)", *(u32*)(media_buffer+0x28) );
		break;
	case 0x614:
		ERROR_LOG(DVDINTERFACE, "GC-AM: 0x601");
		break;
	default:
		ERROR_LOG(DVDINTERFACE, "GC-AM: execute buffer UNKNOWN:%03X", cmd );
		break;
	}
	memset( media_buffer + 0x20, 0, 0x20 );	
}

}  // namespace
