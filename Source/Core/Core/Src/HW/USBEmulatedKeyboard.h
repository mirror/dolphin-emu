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

protected:
    virtual void UpdateKeyboardReport(u8* Data) override;
    virtual void SetKeyboardClientEnabled(bool Enabled) override;
private:
    bool m_PendingStateChange;
    CUSBRequest* m_PendingInterruptRequest;
    void* m_PendingPayload;
};

class CUSBControllerEmulatedKeyboard : public IUSBControllerEmulated<CUSBDeviceEmulatedKeyboard>, public CKeyboardClient
{
public:
    CUSBControllerEmulatedKeyboard();
    ~CUSBControllerEmulatedKeyboard();
protected:
    virtual USBDeviceDescriptorEtc GetDeviceDescriptor() override;
    virtual void SetKeyboardClientEnabled(bool Enabled) override;
};

}
