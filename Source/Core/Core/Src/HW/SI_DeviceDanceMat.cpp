// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "SI.h"
#include "SI_Device.h"
#include "SI_DeviceDanceMat.h"

#include "EXI_Device.h"
#include "EXI_DeviceMic.h"

#include "GCPad.h"

#include "../Movie.h"

#include "../CoreTiming.h"
#include "SystemTimers.h"
#include "ProcessorInterface.h"
#include "../Core.h"

// --- Dance mat gamecube controller ---
CSIDevice_DanceMat::CSIDevice_DanceMat(SIDevices device, int _iDeviceNumber)
	: CSIDevice_GCController(device, _iDeviceNumber) {}


u32 CSIDevice_DanceMat::MapPadStatus(const SPADStatus& padStatus)
{
	// Map the dpad to the blue arrows, the buttons to the orange arrows
	// Z = + button, Start = - button
	u16 map = 0;
	if (padStatus.button & PAD_BUTTON_UP)
		map |= 0x1000;
	if (padStatus.button & PAD_BUTTON_DOWN)
		map |= 0x2;
	if (padStatus.button & PAD_BUTTON_LEFT)
		map |= 0x8;
	if (padStatus.button & PAD_BUTTON_RIGHT)
		map |= 0x4;
	if (padStatus.button & PAD_BUTTON_Y)
		map |= 0x200;
	if (padStatus.button & PAD_BUTTON_A)
		map |= 0x10;
	if (padStatus.button & PAD_BUTTON_B)
		map |= 0x100;
	if (padStatus.button & PAD_BUTTON_X)
		map |= 0x800;
	if (padStatus.button & PAD_TRIGGER_Z)
		map |= 0x400;
	if (padStatus.button & PAD_BUTTON_START)
		map |= 0x1;

	return (u32)(map << 16) | 0x8080;
}

void CSIDevice_DanceMat::HandleButtonCombos(const SPADStatus& padStatus)
{}
