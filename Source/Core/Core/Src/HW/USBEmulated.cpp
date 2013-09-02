// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "USBEmulated.h"

namespace USBInterface
{

void IUSBControllerEmulatedBase::SetEnabled(bool Enabled)
{
	std::vector<USBDeviceDescriptorEtc> List;
	if (Enabled)
	{
		USBDeviceDescriptorEtc Desc = GetDeviceDescriptor();
		Desc.OpenInfo.first = this;
		List.push_back(std::move(Desc));
	}
	SetDeviceList(std::move(List));
}

u32 IUSBDeviceEmulated::SetConfig(int Config)
{
	if (Config != 1)
	{
		WARN_LOG(USBINTERFACE, "IUSBDeviceEmulated: SetConfig with config other than 1 (%d)", Config);
		return UsbErrDefault;
	}
	return 0;
}

u32 IUSBDeviceEmulated::SetDefaultConfig()
{
	return SetConfig(1);
}

u32 IUSBDeviceEmulated::SetInterfaceAltSetting(int Interface, int Setting)
{
	if (Setting != 0)
	{
		WARN_LOG(USBINTERFACE, "IUSBDeviceEmulated: SetInterfaceAltSetting with setting other than 0 (%d)", Setting);
		return UsbErrDefault;
	}
	return 0;
}

void IUSBDeviceEmulated::BulkRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData)
{
	WARN_LOG(USBINTERFACE, "IUSBDeviceEmulated: bulk request (ep=%02x) not implemented", Endpoint);
	(new CUSBRequest(this, UserData, -1, true))->Complete(UsbErrDefault);
}

void IUSBDeviceEmulated::InterruptRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData)
{
	WARN_LOG(USBINTERFACE, "IUSBDeviceEmulated: interrupt request (ep=%02x) not implemented", Endpoint);
	(new CUSBRequest(this, UserData, Endpoint, true))->Complete(UsbErrDefault);
}

void IUSBDeviceEmulated::IsochronousRequest(u8 Endpoint, size_t Length, size_t NumPackets, u16* PacketLengths, void* Payload, void* UserData)
{
	WARN_LOG(USBINTERFACE, "IUSBDeviceEmulated: isochronous request (ep=%02x) not implemented", Endpoint);
	(new CUSBRequest(this, UserData, Endpoint, true))->Complete(UsbErrDefault);
}


}
