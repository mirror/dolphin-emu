// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "../Core.h"
#include "USBInterface.h"
#include "StdThread.h"
#include "MsgHandler.h"
#include "libusb.h"
#include <set>
#include <map>

#ifdef libusb_hotplug_match_any
#define cusbdevice_supports_hotplug
#endif

namespace USBInterface
{

class CUSBControllerReal;

class CUSBDeviceReal : public IUSBDevice
{
public:
	CUSBDeviceReal(libusb_device* Device, TUSBDeviceOpenInfo OpenInfo, libusb_device_handle* Handle, CUSBControllerReal* Controller, IUSBDeviceClient* Client);

	virtual u32 SetConfig(int Config);
	virtual u32 SetInterfaceAltSetting(int Interface, int Setting);
	virtual void BulkRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData);
	virtual void InterruptRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData);
	virtual void IsochronousRequest(u8 Endpoint, size_t Length, size_t NumPackets, u16* PacketLengths, void* Payload, void* UserData);

protected:
	virtual void _ControlRequest(const USBSetup* Request, void* Payload, void* UserData);
	virtual void _Close();
	void CheckClose();
	friend class CUSBRequestReal;

private:
	libusb_device* m_Device;
	libusb_device_handle* m_DeviceHandle;
	int m_NumInterfaces;
	bool m_WasClosed;
};

class CUSBControllerReal : public IUSBController
{
public:
	CUSBControllerReal();
	virtual void Destroy();
	virtual IUSBDevice* OpenDevice(TUSBDeviceOpenInfo OpenInfo, IUSBDeviceClient* Client);
	virtual void UpdateShouldScan();
	virtual void DestroyDeviceList(std::vector<USBDeviceDescriptorEtc>* Old);
private:
	virtual ~CUSBControllerReal();
	void PollDevices(bool IsInitial);
	void USBThread();
	static void USBThreadFunc(CUSBControllerReal* Self) { Self->USBThread(); }
#ifdef CUSBDEVICE_SUPPORTS_HOTPLUG
	static int HotplugCallback(libusb_context* Ctx, libusb_device* Device, libusb_hotplug_event Event, void* Data);
#endif

	libusb_context* m_UsbContext;
#ifdef CUSBDEVICE_SUPPORTS_HOTPLUG
	libusb_hotplug_callback_handle* m_HotplugHandle;
	bool m_UseHotplug;
#endif
	std::thread* m_Thread;

	volatile bool m_ShouldDestroy;
};

} // namespace
