// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

/*
This is the main Wii IPC file that handles all incoming IPC calls and directs them
to the right function.

IPC basics (IOS' usage):

Return values for file handles: All IPC calls will generate a return value to 0x04,
in case of success they are
	Open: DeviceID
	Close: 0
	Read: Bytes read
	Write: Bytes written
	Seek: Seek position
	Ioctl: 0 (in addition to that there may be messages to the out buffers)
	Ioctlv: 0 (in addition to that there may be messages to the out buffers)
They will also generate a true or false return for UpdateInterrupts() in WII_IPC.cpp.
*/

#include <map>
#include <string>
#include <list>

#include "Common.h"
#include "CommonPaths.h"
#include "Thread.h"
#include "WII_IPC_HLE.h"
#include "WII_IPC_HLE_Device.h"
#include "WII_IPC_HLE_Device_DI.h"
#include "WII_IPC_HLE_Device_FileIO.h"
#include "WII_IPC_HLE_Device_stm.h"
#include "WII_IPC_HLE_Device_fs.h"
#include "WII_IPC_HLE_Device_net.h"
#include "WII_IPC_HLE_Device_net_ssl.h"
#include "WII_IPC_HLE_Device_es.h"
#include "WII_IPC_HLE_Device_usb_hid.h"
#include "WII_IPC_HLE_Device_usb_kbd.h"
#include "WII_IPC_HLE_Device_usb_oh0.h"
#include "WII_IPC_HLE_Device_usb_oh1.h"
#include "WII_IPC_HLE_Device_sdio_slot0.h"

#include "FileUtil.h" // For Copy
#include "../ConfigManager.h"
#include "../HW/CPU.h"
#include "../HW/Memmap.h"
#include "../HW/WII_IPC.h"
#include "../Debugger/Debugger_SymbolMap.h"
#include "../PowerPC/PowerPC.h"
#include "../HW/SystemTimers.h"
#include "CoreTiming.h"


namespace WII_IPC_HLE_Interface
{

typedef IWII_IPC_HLE_Device* (*TCreateFunc)(const std::string& Name);
static const TCreateFunc g_DeviceCreateFuncs[] = {
	CWII_IPC_HLE_Device_FileIO::Create,
	CWII_IPC_HLE_Device_di::Create,
	CWII_IPC_HLE_Device_es::Create,
	CWII_IPC_HLE_Device_fs::Create,
	CWII_IPC_HLE_Device_net_ip_top::Create,
	CWII_IPC_HLE_Device_net_kd_request::Create,
	CWII_IPC_HLE_Device_net_kd_time::Create,
	CWII_IPC_HLE_Device_net_ncd_manage::Create,
	CWII_IPC_HLE_Device_net_ssl::Create,
	CWII_IPC_HLE_Device_net_wd_command::Create,
	CWII_IPC_HLE_Device_sdio_slot0::Create,
	CWII_IPC_HLE_Device_sdio_slot1::Create,
	CWII_IPC_HLE_Device_stm_eventhook::Create,
	CWII_IPC_HLE_Device_stm_immediate::Create,
	CWII_IPC_HLE_Device_usb_hid::Create,
	CWII_IPC_HLE_Device_usb_kbd::Create,
	CWII_IPC_HLE_Device_usb_oh0::Create,
	CWII_IPC_HLE_Device_usb_oh0_dev::Create,
	CWII_IPC_HLE_Device_usb_oh1::Create,
	CWII_IPC_HLE_Device_usb_oh1_57e_305::Create,
	0
};

#define IPC_MAX_FDS 0x18
IWII_IPC_HLE_Device* g_FdMap[IPC_MAX_FDS];
std::map<IWII_IPC_HLE_Device*, u32 /* instances */> g_Devices;

typedef std::deque<u32> ipc_msg_queue;
static ipc_msg_queue request_queue;	// ppc -> arm
static ipc_msg_queue reply_queue;	// arm -> ppc

static int enque_reply;

static u64 last_reply_time;

void EnqueReplyCallback(u64 userdata, int)
{
	reply_queue.push_back(userdata);
}

void Init()
{
	CWII_IPC_HLE_Device_es::m_ContentFile = "";
	u32 i;
	for (i=0; i<IPC_MAX_FDS; i++)
	{
		g_FdMap[i] = NULL;
	}

	enque_reply = CoreTiming::RegisterEvent("IPCReply", EnqueReplyCallback);
}

void Reset(bool _bHard)
{
	CoreTiming::RemoveAllEvents(enque_reply);

	u32 i;
	for (i=0; i<IPC_MAX_FDS; i++)
	{
		if (g_FdMap[i] != NULL)
		{
			// close all files and delete their resources
			g_FdMap[i]->Close(0, true);
			g_FdMap[i]->Unref();
		}
		g_FdMap[i] = NULL;
	}
	g_Devices.clear();

	request_queue.clear();
	reply_queue.clear();

	last_reply_time = 0;
}

void Shutdown()
{
	Reset(true);
	for (auto itr = g_SingletonDestructors.begin(); itr != g_SingletonDestructors.end(); ++itr)
	{
		itr->first(itr->second);
	}
	g_SingletonDestructors.clear();
}

void SetDefaultContentFile(const std::string& _rFilename)
{
	CWII_IPC_HLE_Device_es::MakeInstance()->LoadWAD(_rFilename);
}

void ES_DIVerify(u8 *_pTMD, u32 _sz)
{
	CWII_IPC_HLE_Device_es::ES_DIVerify(_pTMD, _sz);
}

void SDIO_EventNotify()
{
	CWII_IPC_HLE_Device_sdio_slot0 *pDevice =
		(CWII_IPC_HLE_Device_sdio_slot0*)GetDeviceByName(std::string("/dev/sdio/slot0"));
	if (pDevice)
		pDevice->EventNotify();
}
int getFreeDeviceId()
{
	u32 i;
	for (i=0; i<IPC_MAX_FDS; i++)
	{
		if (g_FdMap[i] == NULL)
		{
			return i;
		}
	}
	return -1;
}

IWII_IPC_HLE_Device* GetDeviceByName(const std::string& _rDeviceName)
{
	for (const TCreateFunc* func = g_DeviceCreateFuncs; *func; func++)
	{
		if (IWII_IPC_HLE_Device* Device = (*func)(_rDeviceName))
		{
			return Device;
		}
	}

	return NULL;
}

void DoState(PointerWrap &p)
{
	p.Do(request_queue);
	p.Do(reply_queue);
	p.Do(last_reply_time);

	if (p.GetMode() == PointerWrap::MODE_READ)
	{
		u32 Count;
		p.Do(Count);
		std::vector<IWII_IPC_HLE_Device*> DeviceList;
		for (u32 i = 0; i < Count; i++)
		{
			std::string Name;
			p.Do(Name);
			IWII_IPC_HLE_Device* Device = GetDeviceByName(Name);
			Device->DoState(p);
			p.DoMarker("device state", 0x84);
			DeviceList.push_back(Device);
		}

		for (u32 i = 0; i < IPC_MAX_FDS; i++)
		{
			IWII_IPC_HLE_Device* Device = NULL;
			s32 Idx;
			p.Do(Idx);
			if (Idx != -1)
			{
				Device = DeviceList[Idx];
				if (g_Devices[Device]++ != 0)
				{
					Device->Ref();
				}
			}
			g_FdMap[i] = Device;
		}
	}
	else
	{
		u32 Count = g_Devices.size();
		p.Do(Count);
		std::map<IWII_IPC_HLE_Device*, u32> Indices;
		u32 i = 0;
		for (auto itr = g_Devices.begin(); itr != g_Devices.end(); ++itr)
		{
			IWII_IPC_HLE_Device* Device = itr->first;
			std::string Name = Device->GetDeviceName();
			p.Do(Name);
			Device->DoState(p);
			p.DoMarker("device state", 0x84);
			Indices[Device] = i++;
		}
		for (u32 j = 0; j < IPC_MAX_FDS; j++)
		{
			IWII_IPC_HLE_Device* Device = g_FdMap[j];
			s32 Idx = Device ? Indices[Device] : -1;
			p.Do(Idx);
		}
	}
}

void ExecuteCommand(u32 _Address)
{
	bool CmdSuccess = false;

	ECommandType Command = static_cast<ECommandType>(Memory::Read_U32(_Address));
	s32 DeviceID = Memory::Read_U32(_Address + 8);

	IWII_IPC_HLE_Device* pDevice = (DeviceID >= 0 && DeviceID < IPC_MAX_FDS) ? g_FdMap[DeviceID] : NULL;

	INFO_LOG(WII_IPC_HLE, "-->> Execute Command Address: 0x%08x (code: %x, device: %x) %p", _Address, Command, DeviceID, pDevice);

	switch (Command)
	{
	case COMMAND_OPEN_DEVICE:
	{
		u32 Mode = Memory::Read_U32(_Address + 0x10);
		DeviceID = getFreeDeviceId();

		std::string DeviceName;
		Memory::GetString(DeviceName, Memory::Read_U32(_Address + 0xC));


		WARN_LOG(WII_IPC_HLE, "Trying to open %s as %d", DeviceName.c_str(), DeviceID);
		if (DeviceID >= 0)
		{
			pDevice = GetDeviceByName(DeviceName);
			if (pDevice)
			{
				u32 Err = pDevice->Open(_Address, Mode);
				INFO_LOG(WII_IPC_FILEIO, "IOP: ReOpen (Device=%s, DeviceID=%08x, Mode=%i) Err=%d",
					pDevice->GetDeviceName().c_str(), DeviceID, Mode, Err);
				Memory::Write_U32(Err == 0 ? DeviceID : Err, _Address+4);
				if (Err == 0)
				{
					g_FdMap[DeviceID] = pDevice;
					g_Devices[pDevice]++;
				}
			}
			else
			{
				WARN_LOG(WII_IPC_HLE, "Unimplemented device: %s", DeviceName.c_str());
				Memory::Write_U32(FS_ENOENT, _Address+4);
			}
		}
		else
		{
			Memory::Write_U32(FS_EFDEXHAUSTED, _Address + 4);
		}
		CmdSuccess = true;
		break;
	}
	case COMMAND_CLOSE_DEVICE:
	{
		if (pDevice)
		{
			g_FdMap[DeviceID] = NULL;
			if (--g_Devices[pDevice] == 0)
			{
				g_Devices.erase(pDevice);
			}
			CmdSuccess = pDevice->Close(_Address);
			pDevice->Unref();
		}
		else
		{
			Memory::Write_U32(FS_EINVAL, _Address + 4);
			CmdSuccess = true;
		}
		break;
	}
	case COMMAND_READ:
	{
		if (pDevice)
		{
			CmdSuccess = pDevice->Read(_Address);
		}
		else
		{
			Memory::Write_U32(FS_EINVAL, _Address + 4);
			CmdSuccess = true;
		}
		break;
	}
	case COMMAND_WRITE:
	{
		if (pDevice)
		{
			CmdSuccess = pDevice->Write(_Address);
		}
		else
		{
			Memory::Write_U32(FS_EINVAL, _Address + 4);
			CmdSuccess = true;
		}
		break;
	}
	case COMMAND_SEEK:
	{
		if (pDevice)
		{
			CmdSuccess = pDevice->Seek(_Address);
		}
		else
		{
			Memory::Write_U32(FS_EINVAL, _Address + 4);
			CmdSuccess = true;
		}
		break;
	}
	case COMMAND_IOCTL:
	{
		if (pDevice)
		{
			CmdSuccess = pDevice->IOCtl(_Address);
		}
		break;
	}
	case COMMAND_IOCTLV:
	{
		if (pDevice)
		{
			CmdSuccess = pDevice->IOCtlV(_Address);
		}
		break;
	}
	default:
	{
		_dbg_assert_msg_(WII_IPC_HLE, 0, "Unknown IPC Command %i (0x%08x)", Command, _Address);
		break;
	}
	}


	if (CmdSuccess)
	{
		// It seems that the original hardware overwrites the command after it has been
		// executed. We write 8 which is not any valid command, and what IOS does
		Memory::Write_U32(8, _Address);
		// IOS seems to write back the command that was responded to
		Memory::Write_U32(Command, _Address + 8);

		// Ensure replies happen in order, fairly ugly
		// Without this, tons of games fail now that DI commands have different reply delays
		int reply_delay = pDevice ? pDevice->GetCmdDelay(_Address) : 0;

		const s64 ticks_til_last_reply = last_reply_time - CoreTiming::GetTicks();

		if (ticks_til_last_reply > 0)
			reply_delay = ticks_til_last_reply;

		last_reply_time = CoreTiming::GetTicks() + reply_delay;

		// Generate a reply to the IPC command
		EnqReply(_Address, reply_delay);
	}
	else
	{
		if (pDevice)
		{
			INFO_LOG(WII_IPC_HLE, "<<-- Reply Failed to %s IPC Request %i @ 0x%08x ", pDevice->GetDeviceName().c_str(), Command, _Address);
		}
		else
		{
			INFO_LOG(WII_IPC_HLE, "<<-- Reply Failed to Unknown (%08x) IPC Request %i @ 0x%08x ", DeviceID, Command, _Address);
		}
	}
}

// Happens AS SOON AS IPC gets a new pointer!
void EnqRequest(u32 _Address)
{
	request_queue.push_back(_Address);
}

// Called when IOS module has some reply
void EnqReply(u32 _Address, int cycles_in_future)
{
	CoreTiming::ScheduleEvent(cycles_in_future, enque_reply, _Address);
}

// This is called every IPC_HLE_PERIOD from SystemTimers.cpp
// Takes care of routing ipc <-> ipc HLE
void Update()
{
	if (!WII_IPCInterface::IsReady())
		return;

	UpdateDevices();

	if (request_queue.size())
	{
		WII_IPCInterface::GenerateAck(request_queue.front());
		INFO_LOG(WII_IPC_HLE, "||-- Acknowledge IPC Request @ 0x%08x", request_queue.front());
		u32 command = request_queue.front();
		request_queue.pop_front();
		ExecuteCommand(command);

#if MAX_LOGLEVEL >= DEBUG_LEVEL
		Dolphin_Debugger::PrintCallstack(LogTypes::WII_IPC_HLE, LogTypes::LDEBUG);
#endif
	}

	if (reply_queue.size())
	{
		WII_IPCInterface::GenerateReply(reply_queue.front());
		INFO_LOG(WII_IPC_HLE, "<<-- Reply to IPC Request @ 0x%08x", reply_queue.front());
		reply_queue.pop_front();
	}
}

void UpdateDevices()
{
	// Check if a hardware device must be updated
	for (auto itr = g_Devices.begin(); itr != g_Devices.end(); ++itr)
	{
		if (itr->first->Update())
		{
			break;
		}
	}
}


} // end of namespace WII_IPC_HLE_Interface

// defined in Device.h
std::vector<std::pair<void (*)(void*), void*>> g_SingletonDestructors;

