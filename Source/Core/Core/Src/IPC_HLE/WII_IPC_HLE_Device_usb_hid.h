// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

#include "WII_IPC_HLE.h"
#include "WII_IPC_HLE_Device.h"
#include "HW/USBInterface.h"
#include <unordered_map>

#define HID_ID_MASK 0x0000FFFFFFFFFFFF
#define MAX_HID_INTERFACES 1

#define HIDERR_NO_DEVICE_FOUND -4

/* Connection timed out */

class CWII_IPC_HLE_Device_usb_hid :
public IWII_IPC_HLE_Device, public CWII_IPC_HLE_Device_Singleton<CWII_IPC_HLE_Device_usb_hid>,
public USBInterface::IUSBDeviceChangeClient, USBInterface::IUSBDeviceClient
{
public:
	CWII_IPC_HLE_Device_usb_hid(const std::string& _rDeviceName);
	virtual ~CWII_IPC_HLE_Device_usb_hid();

	virtual void DoState(PointerWrap& p);

	virtual bool IOCtl(u32 _CommandAddress);

	virtual void USBDevicesChanged(std::vector<USBInterface::USBDeviceDescriptorEtc*>& Devices);
	virtual void USBRequestComplete(void* UserData, u32 Status);

	static const char* GetBaseName() { return "/dev/usb/hid"; }
private:
	enum
	{
		IOCTL_HID_GET_ATTACHED		= 0x00,
		IOCTL_HID_SET_SUSPEND		= 0x01,
		IOCTL_HID_CONTROL			= 0x02,
		IOCTL_HID_INTERRUPT_IN		= 0x03,
		IOCTL_HID_INTERRUPT_OUT	= 0x04,
		IOCTL_HID_GET_US_STRING	= 0x05,
		IOCTL_HID_OPEN				= 0x06,
		IOCTL_HID_SHUTDOWN			= 0x07,
		IOCTL_HID_CANCEL_INTERRUPT	= 0x08,
	};

	struct SHidUserData
	{
		u32 CommandAddress;
		u32 Parameter;
		u8* DescriptorBuf; // for GET_US_STRING
	};
	static_assert(sizeof(SHidUserData) <= USBInterface::UsbUserDataSize, "too big");

	void Shutdown();
	USBInterface::IUSBDevice* GetDevice(u32 DevNum);
	void UpdateDevices(std::vector<USBInterface::USBDeviceDescriptorEtc*>& Devices);
	bool FillAttachedReply(std::vector<USBInterface::USBDeviceDescriptorEtc*>& Devices, void* Buffer, size_t Size);

	enum { NumUids = 20 };
	int m_NextUid;
	std::unordered_map<u32, USBInterface::IUSBDevice*> m_OpenDevices;
	typedef std::unordered_map<u32, USBInterface::TUSBDeviceOpenInfo> TUidMap;
	typedef std::unordered_map<USBInterface::TUSBDeviceOpenInfo, u32, PairHash<USBInterface::TUSBDeviceOpenInfo>> TUidMapRev;
	TUidMap m_UidMap;
	TUidMapRev m_UidMapRev;

	u32 m_DeviceCommandAddress;
	bool m_DidInitialList;
};
