// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _SI_DEVICEDANCEMAT_H
#define _SI_DEVICEDANCEMAT_H

#include "SI_Device.h"
#include "GCPadStatus.h"
#include "SI_DeviceGCController.h"

// standard gamecube controller
class CSIDevice_DanceMat : public CSIDevice_GCController
{
public:
	CSIDevice_DanceMat(SIDevices device, int _iDeviceNumber);
	virtual u32 MapPadStatus(const SPADStatus& padStatus) override;
	virtual void HandleButtonCombos(const SPADStatus& padStatus) override;
};

#endif
