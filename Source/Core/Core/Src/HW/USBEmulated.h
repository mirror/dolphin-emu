// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "USBInterface.h"

namespace USBInterface
{

class IUSBControllerEmulatedBase : public IUSBController
{
public:
	virtual void Destroy() override
	{
		delete this;
	}

	virtual void UpdateShouldScan() override
	{}

	virtual void DestroyDeviceList(std::vector<USBDeviceDescriptorEtc>& Old) override
	{}

	void SetEnabled(bool Enabled);

protected:
	virtual USBDeviceDescriptorEtc GetDeviceDescriptor() = 0;
};

template <typename Device>
class IUSBControllerEmulated: public IUSBControllerEmulatedBase
{
	virtual IUSBDevice* OpenDevice(TUSBDeviceOpenInfo OpenInfo, IUSBDeviceClient* Client) override
	{
		return new Device(OpenInfo, Client, this);
	}
};

class IUSBDeviceEmulated : public IUSBDevice
{
public:
	IUSBDeviceEmulated(TUSBDeviceOpenInfo OpenInfo, IUSBDeviceClient* Client, IUSBControllerEmulatedBase* Controller)
	: IUSBDevice(OpenInfo, Client), m_Controller(Controller) {}

	virtual u32 SetConfig(int Config) override;
	virtual u32 SetDefaultConfig() override;
	virtual u32 SetInterfaceAltSetting(int Interface, int Setting) override;
	virtual void BulkRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData) override;
	virtual void InterruptRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData) override;
	virtual void IsochronousRequest(u8 Endpoint, size_t Length, size_t NumPackets, u16* PacketLengths, void* Payload, void* UserData) override;
protected:
	IUSBControllerEmulatedBase* m_Controller;
};

}
