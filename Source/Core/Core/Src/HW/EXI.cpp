// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"
#include "ChunkFile.h"
#include "../ConfigManager.h"
#include "../CoreTiming.h"

#include "ProcessorInterface.h"
#include "../PowerPC/PowerPC.h"

#include "EXI.h"
#include "Sram.h"
#include "Core.h"
#include "../Movie.h"
SRAM g_SRAM;

namespace ExpansionInterface
{
enum
{
	NUM_CHANNELS = 3
};

static int changeDevice[NUM_CHANNELS];
CEXIChannel *g_Channels[NUM_CHANNELS];
}

EXISyncClass::EXISyncClass()
: IOSync::Class(ClassEXI)
{
	m_AutoConnect = false;
	m_Synchronous = true;
}

// (This is almost identical to the SI code, but too short to bother
// templating.)
void EXISyncClass::PreInit()
{
	ExpansionInterface::PreInit();
}

void EXISyncClass::OnConnected(int channel, int localIndex, PWBuffer&& subtype)
{
	IOSync::Class::OnConnected(channel, localIndex, std::move(subtype));
	TEXIDevices type = GrabSubtype<TEXIDevices>(GetSubtype(channel));

	CoreTiming::RemoveAllEvents(ExpansionInterface::changeDevice[channel]);

	// card 0 -> channel 0 device 0; card 1 -> channel 1 device 0
	IEXIDevice* exidev = ExpansionInterface::g_Channels[channel]->GetDevice(1);
	if (!exidev || type != exidev->m_deviceType)
	{
		u64 info = (u64)channel << 32 | (u64)0 << 16;
		CoreTiming::ScheduleEvent(0, ExpansionInterface::changeDevice[channel], info | EXIDEVICE_NONE);
		CoreTiming::ScheduleEvent(50000000, ExpansionInterface::changeDevice[channel], info | type);
	}
}

void EXISyncClass::OnDisconnected(int channel)
{
	IOSync::Class::OnDisconnected(channel);
	CoreTiming::RemoveAllEvents(ExpansionInterface::changeDevice[channel]);
	CoreTiming::ScheduleEvent(0, ExpansionInterface::changeDevice[channel], ((u64)channel << 32) | ((u64)0 << 16) | EXIDEVICE_NONE);
}

EXISyncClass g_EXISyncClass;

namespace ExpansionInterface
{

void PreInit()
{
	for (int i = 0; i < NUM_CHANNELS; i++)
	{
		ChangeLocalDevice(i, SConfig::GetInstance().m_EXIDevice[i]);
	}
}

void Init()
{
	for (int i = 0; i < 4; i++)
	{
		char buf[64];
		sprintf(buf, "ChangeEXIDevice%d", i);
		changeDevice[i] = CoreTiming::RegisterEvent(strdup(buf), ChangeDeviceCallback);
	}

	PreInit();

	initSRAM();
	for (u32 i = 0; i < NUM_CHANNELS; i++)
		g_Channels[i] = new CEXIChannel(i);

	g_Channels[0]->AddDevice(EXIDEVICE_MASKROM,						1);
	g_Channels[0]->AddDevice(SConfig::GetInstance().m_EXIDevice[2],	2); // Serial Port 1
	g_Channels[2]->AddDevice(EXIDEVICE_AD16,						0);
}

void Shutdown()
{
	for (auto& channel : g_Channels)
	{
		delete channel;
		channel = NULL;
	}
}

void DoState(PointerWrap &p)
{
	for (auto& channel : g_Channels)
		channel->DoState(p);
}

void PauseAndLock(bool doLock, bool unpauseOnUnlock)
{
	for (auto& channel : g_Channels)
		channel->PauseAndLock(doLock, unpauseOnUnlock);
}


void ChangeDeviceCallback(u64 userdata, int cyclesLate)
{
	u8 channel = (u8)(userdata >> 32);
	u8 num = (u8)(userdata >> 16);
	u8 type = (u8)userdata;

	g_Channels[channel]->AddDevice((TEXIDevices)type, num);
}

void ChangeLocalDevice(int channel, TEXIDevices device_type)
{
	if (channel == 2)
	{
		if (Core::GetState() == Core::CORE_UNINITIALIZED)
			return;
		// not synced yet; card 2 -> channel 0 device 2
		u64 info = (u64)0 << 32 | (u64)2 << 16;
		CoreTiming::ScheduleEvent(0, ExpansionInterface::changeDevice[channel], info | EXIDEVICE_NONE);
		CoreTiming::ScheduleEvent(50000000, ExpansionInterface::changeDevice[channel], info | device_type);
	}
	else
	{
		TEXIDevices old;
		if (g_EXISyncClass.LocalIsConnected(channel))
			old = g_EXISyncClass.GrabSubtype<TEXIDevices>(g_EXISyncClass.GetLocalSubtype(channel));
		else
			old = EXIDEVICE_NONE;

		if (old == device_type)
			return;
		if (device_type != EXIDEVICE_NONE)
			g_EXISyncClass.ConnectLocalDevice(channel, g_EXISyncClass.PushSubtype(device_type));
	}
}

IEXIDevice* FindDevice(TEXIDevices device_type, int customIndex)
{
	for (auto& channel : g_Channels)
	{
		IEXIDevice* device = channel->FindDevice(device_type, customIndex);
		if (device)
			return device;
	}
	return NULL;
}

void Read32(u32& _uReturnValue, const u32 _iAddress)
{
	// TODO 0xfff00000 is mapped to EXI -> mapped to first MB of maskrom
	u32 iAddr = _iAddress & 0x3FF;
	u32 iRegister = (iAddr >> 2) % 5;
	u32 iChannel = (iAddr >> 2) / 5;

	_dbg_assert_(EXPANSIONINTERFACE, iChannel < NUM_CHANNELS);

	if (iChannel < NUM_CHANNELS)
	{
		g_Channels[iChannel]->Read32(_uReturnValue, iRegister);
	}
	else
	{
		_uReturnValue = 0;
	}
}

void Write32(const u32 _iValue, const u32 _iAddress)
{
	// TODO 0xfff00000 is mapped to EXI -> mapped to first MB of maskrom
	u32 iAddr = _iAddress & 0x3FF;
	u32 iRegister = (iAddr >> 2) % 5;
	u32 iChannel = (iAddr >> 2) / 5;

	_dbg_assert_(EXPANSIONINTERFACE, iChannel < NUM_CHANNELS);

	if (iChannel < NUM_CHANNELS)
		g_Channels[iChannel]->Write32(_iValue, iRegister);
}

void UpdateInterrupts()
{
	// Interrupts are mapped a bit strangely:
	// Channel 0 Device 0 generates interrupt on channel 0
	// Channel 0 Device 2 generates interrupt on channel 2
	// Channel 1 Device 0 generates interrupt on channel 1
	g_Channels[2]->SetEXIINT(g_Channels[0]->GetDevice(4)->IsInterruptSet());

	bool causeInt = false;
	for (auto& channel : g_Channels)
		causeInt |= channel->IsCausingInterrupt();

	ProcessorInterface::SetInterrupt(ProcessorInterface::INT_CAUSE_EXI, causeInt);
}

} // end of namespace ExpansionInterface
