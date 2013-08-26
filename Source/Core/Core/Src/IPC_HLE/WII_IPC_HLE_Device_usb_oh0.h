// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "WII_IPC_HLE.h"
#include "WII_IPC_HLE_Device.h"
#include "HW/USBInterface.h"
#include <set>
#include <map>

enum
{
	USBV0_DEV_IOCTL_SUSPENDDEV = 5, // ()
	USBV0_DEV_IOCTL_RESUMEDEV = 6, // ()
	USBV0_DEV_IOCTL_DEVREMOVALHOOK = 26, // ()
	USBV0_DEV_IOCTL_RESETDEV = 29, // ()

	USBV0_DEV_IOCTLV_CTRLMSG = 0, // see wiki
	USBV0_DEV_IOCTLV_BLKMSG = 1,
	USBV0_DEV_IOCTLV_INTRMSG = 2,
	USBV0_DEV_IOCTLV_ISOMSG = 9, // (u8, u16, u8 -> 2*in[2], in[1])
	USBV0_DEV_IOCTLV_LONGITRMSG = 10, // (u8, u32 -> in[1])

	USBV0_ROOT_IOCTL_GETRHDESC = 15, // (-> u32)
	USBV0_ROOT_IOCTL_CANCELINSERTIONNOTIFY = 31, // (u32)

	USBV0_ROOT_IOCTLV_GETDEVLIST = 12, // (u8 count, u8 cls -> u8 realcount,
									   // {u32 ?, u16 vid, u16 pid}*in[0])
	USBV0_ROOT_IOCTLV_GETRHPORTSTATUS = 20, // (u8 -> u32)
	USBV0_ROOT_IOCTLV_SETRHPORTSTATUS = 25, // (u8, u32)
	USBV0_ROOT_IOCTLV_DEVINSERTHOOK = 27, // (u16, u16)
	USBV0_ROOT_IOCTLV_DEVCLASSCHANGE = 28, // (u8)
	USBV0_ROOT_IOCTLV_DEVINSERTHOOKID = 30, // (u16 vid, u16 pid, u8 class -> u32 id)
};

class IWII_IPC_HLE_Device_usb_x : public IWII_IPC_HLE_Device, public USBInterface::IUSBDeviceChangeClient
{
public:
	IWII_IPC_HLE_Device_usb_x(const std::string& _rDeviceName);
	~IWII_IPC_HLE_Device_usb_x();
protected:
	int m_RefCount;
};

class CWII_IPC_HLE_Device_usb_oh0 :
public IWII_IPC_HLE_Device_usb_x, public CWII_IPC_HLE_Device_Singleton<CWII_IPC_HLE_Device_usb_oh0>
{
public:
	CWII_IPC_HLE_Device_usb_oh0(const std::string& _rDeviceName);

	virtual bool IOCtl(u32 _CommandAddress);
	virtual bool IOCtlV(u32 _CommandAddress);

	virtual void DoState(PointerWrap& p);

	virtual void USBDevicesChanged(std::vector<USBInterface::USBDeviceDescriptorEtc*>& Devices);

	static const char* GetBaseName() { return "/dev/usb/oh0"; }
private:
	struct SVidPidClass
	{
		u16 Vid, Pid, Class;
	};
	typedef std::map<u32, std::pair<SVidPidClass, u32>> TDeviceInsertionHooks;

	bool MatchDevice(USBInterface::USBDeviceDescriptorEtc* Desc, TDeviceInsertionHooks::iterator iitr);
	u32 AddDeviceInsertionHook(SVidPidClass& Match, u32 _CommandAddress);

	std::set<USBInterface::TUSBDeviceOpenInfo> m_SeenDevices;
	TDeviceInsertionHooks m_DeviceInsertionHooks;
	u32 m_InsertionHookId;
};

class CWII_IPC_HLE_Device_usb_oh0_dev :
public IWII_IPC_HLE_Device_usb_x, public USBInterface::IUSBDeviceClient
{
public:
	CWII_IPC_HLE_Device_usb_oh0_dev(const std::string& _rDeviceName, u16 Vid, u16 Pid);
	~CWII_IPC_HLE_Device_usb_oh0_dev();

	virtual u32 Open(u32 _CommandAddress, u32 _Mode);
	virtual void Unref();
	virtual bool IOCtl(u32 _CommandAddress);
	virtual bool IOCtlV(u32 _CommandAddress);

	virtual void DoState(PointerWrap& p);

	virtual void USBDevicesChanged(std::vector<USBInterface::USBDeviceDescriptorEtc*>& Devices);
	virtual void USBRequestComplete(void* UserData, u32 Status);

	static IWII_IPC_HLE_Device* Create(const std::string& Name);

private:
	USBInterface::IUSBDevice* m_Device;
	USBInterface::TUSBDeviceOpenInfo m_OpenInfo;
	u32 m_RemovalCommandAddress;
};

