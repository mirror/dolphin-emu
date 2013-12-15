// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _EXIINTERFACE_H
#define _EXIINTERFACE_H

#include "CommonTypes.h"
#include "EXI_Channel.h"
#include "Thread.h"
#include "IOSync.h"
class PointerWrap;

namespace ExpansionInterface
{

void PreInit();
void Init();
void Shutdown();
void DoState(PointerWrap &p);
void PauseAndLock(bool doLock, bool unpauseOnUnlock);

void Update();
void UpdateInterrupts();

void ChangeDeviceCallback(u64 userdata, int cyclesLate);
void ChangeLocalDevice(int channel, const TEXIDevices device_type);
IEXIDevice* FindDevice(TEXIDevices device_type, int customIndex=-1);

void Read32(u32& _uReturnValue, const u32 _iAddress);
void Write32(const u32 _iValue, const u32 _iAddress);

} // end of namespace ExpansionInterface

// Note: This does not currently support the slot 3 devices.  I suppose they
// should be considered a separate class...
class EXISyncClass : public IOSync::Class
{
public:
	EXISyncClass();
    virtual void OnConnected(int index, int localIndex, PWBuffer&& subtype) override;
    virtual void OnDisconnected(int index) override;
	virtual int GetMaxDeviceIndex() override { return 2; }
	virtual void PreInit() override;
};
extern EXISyncClass g_EXISyncClass;

#endif
