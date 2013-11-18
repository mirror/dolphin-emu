// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "SI.h"
#include "SI_Device.h"
#include "SI_DeviceGCSteeringWheel.h"

#include "EXI_Device.h"
#include "EXI_DeviceMic.h"

#include "GCPad.h"

#include "../Movie.h"

#include "../CoreTiming.h"
#include "SystemTimers.h"
#include "ProcessorInterface.h"
#include "../Core.h"

// --- standard gamecube controller ---
CSIDevice_GCSteeringWheel::CSIDevice_GCSteeringWheel(SIDevices device, int _iDeviceNumber)
	: CSIDevice_GCController(device, _iDeviceNumber)
{}

// SendCommand
void CSIDevice_GCSteeringWheel::SendCommand(u32 _Cmd, u8 _Poll)
{
	UCommand command(_Cmd);

	if (command.Command == CMD_FORCE)
	{
		unsigned int uStrength = command.Parameter1; // 0 = left strong, 127 = left weak, 128 = right weak, 255 = right strong
		unsigned int uType = command.Parameter2;  // 06 = motor on, 04 = motor off

		// get the correct pad number that should rumble locally when using netplay
		int li = GetLocalIndex();
		if (li != -1)
			Pad::Motor(li, uType, uStrength);

		if (!_Poll)
		{
			m_Mode = command.Parameter2;
			INFO_LOG(SERIALINTERFACE, "PAD %i set to mode %i", ISIDevice::m_iDeviceNumber, m_Mode);
		}
	}
	else
	{
		return CSIDevice_GCController::SendCommand(_Cmd, _Poll);
	}
}
