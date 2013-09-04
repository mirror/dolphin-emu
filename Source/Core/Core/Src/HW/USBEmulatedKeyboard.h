// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "USBInterface.h"
#include "USBEmulated.h"
#include "KeyboardClient.h"

namespace USBInterface
{

class CUSBDeviceEmulatedKeyboard final : public IUSBDeviceEmulated, public CKeyboardClient
{
public:
	CUSBDeviceEmulatedKeyboard(TUSBDeviceOpenInfo OpenInfo, IUSBDeviceClient* Client, IUSBControllerEmulatedBase* Controller);
    ~CUSBDeviceEmulatedKeyboard();

	virtual bool ControlRequest(const USBSetup* Request, void* Payload, void* UserData) override;
	virtual void InterruptRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData) override;

    virtual void UpdateReport(u8* Data);
private:
    bool m_PendingStateChange;
    CUSBRequest* m_PendingInterruptRequest;
    void* m_PendingPayload;
};

class CUSBControllerEmulatedKeyboard : public IUSBControllerEmulated<CUSBDeviceEmulatedKeyboard>
{
public:
    CUSBControllerEmulatedKeyboard()
    {
        SetEnabled(true);
    }
protected:
    virtual USBDeviceDescriptorEtc GetDeviceDescriptor() override;
};

}
