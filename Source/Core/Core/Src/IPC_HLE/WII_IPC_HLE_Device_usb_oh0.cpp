// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "../Core.h"
#include "WII_IPC_HLE.h"
#include "WII_IPC_HLE_Device_usb_oh0.h"

IWII_IPC_HLE_Device_usb_x::IWII_IPC_HLE_Device_usb_x(const std::string& _rDeviceName)
: IWII_IPC_HLE_Device(_rDeviceName)
{
	USBInterface::RefInterface();
	USBInterface::RegisterDeviceChangeClient(this);
}

IWII_IPC_HLE_Device_usb_x::~IWII_IPC_HLE_Device_usb_x()
{
	USBInterface::DeregisterDeviceChangeClient(this);
}

CWII_IPC_HLE_Device_usb_oh0::CWII_IPC_HLE_Device_usb_oh0(const std::string& _rDeviceName)
: IWII_IPC_HLE_Device_usb_x(_rDeviceName) {}

bool CWII_IPC_HLE_Device_usb_oh0::MatchDevice(USBInterface::USBDeviceDescriptorEtc* Desc, TDeviceInsertionHooks::iterator iitr)
{
	if (USBInterface::IsOpen(Desc->OpenInfo))
	{
		return false;
	}
	SVidPidClass& Match = iitr->second.first;
	if (Match.Class == 256) {
		if (Desc->idVendor == 0)
		{
			return false;
		}
	}
	else
	{
		if (Match.Class != 0) {
			if (Desc->bDeviceClass == Match.Class)
			{
				goto ok;
			}
			for (std::vector<USBInterface::USBConfigDescriptorEtc>::iterator
				 citr = Desc->Configs.begin(); citr != Desc->Configs.end(); ++citr)
			{
				for (std::vector<USBInterface::USBInterfaceDescriptorEtc>::iterator
					 nitr = citr->Interfaces.begin(); nitr != citr->Interfaces.end(); ++nitr)
				{
					if (nitr->bInterfaceClass == Match.Class)
					{
						goto ok;
					}
				}
			}
			return false;
			ok:;
		}
		if (Match.Vid != 0 && Desc->idVendor != Match.Vid)
		{
			return false;
		}
		if (Match.Pid != 0 && Desc->idProduct != Match.Pid)
		{
			return false;
		}
	}
	// It's a match
	DEBUG_LOG(WII_IPC_USB, "oh0 new device matched hook");
	u32 CommandAddress = iitr->second.second;
	Memory::Write_U32(0, CommandAddress + 4);
	WII_IPC_HLE_Interface::EnqReply(CommandAddress);
	m_DeviceInsertionHooks.erase(iitr);
	Unref();
	return true;
}

void CWII_IPC_HLE_Device_usb_oh0::USBDevicesChanged(std::vector<USBInterface::USBDeviceDescriptorEtc*>& Devices)
{
	DEBUG_LOG(WII_IPC_USB, "oh0 devices changed");
	std::set<USBInterface::TUSBDeviceOpenInfo> NewSeenDevices;
	for (auto itr = Devices.begin(); itr != Devices.end(); ++itr)
	{
		USBInterface::USBDeviceDescriptorEtc* Desc = *itr;
		NewSeenDevices.insert(Desc->OpenInfo);
		auto sitr = m_SeenDevices.find(Desc->OpenInfo);
		if (sitr == m_SeenDevices.end())
		{
			// New device inserted
			DEBUG_LOG(WII_IPC_USB, "oh0 new device, %u hooks", (unsigned int) m_DeviceInsertionHooks.size());
			for (auto iitr = m_DeviceInsertionHooks.begin();
				 iitr != m_DeviceInsertionHooks.end(); ++iitr)
			{
				if (MatchDevice(Desc, iitr))
				{
					break;
				}
			}
		}
	}
	m_SeenDevices.swap(NewSeenDevices);
}

bool CWII_IPC_HLE_Device_usb_oh0::IOCtl(u32 _CommandAddress) 
{
	u32 BufferIn		= Memory::Read_U32(_CommandAddress + 0x10);
	u32 BufferOut		= Memory::Read_U32(_CommandAddress + 0x18);
	u32 Command			= Memory::Read_U32(_CommandAddress + 0x0C);
	u32 ReturnValue = 0;
	DEBUG_LOG(WII_IPC_USB, "oh0 ioctl %u", Command);

	switch (Command)
	{
	case USBV0_ROOT_IOCTL_GETRHDESC:
	{
		Memory::Write_U32(0xdeadbeef, BufferOut);
		break;
	}
	case USBV0_ROOT_IOCTL_CANCELINSERTIONNOTIFY:
	{
		u32 Id = Memory::Read_U32(BufferIn);
		auto itr = m_DeviceInsertionHooks.find(Id);
		if (itr != m_DeviceInsertionHooks.end())
		{
			u32 CommandAddress = itr->second.second;
			Memory::Write_U32(-7022, CommandAddress + 4);
			WII_IPC_HLE_Interface::EnqReply(CommandAddress);
			m_DeviceInsertionHooks.erase(itr);
			Close(0, true);
		}
		else
		{
			ReturnValue = -4;
		}
		break;
	}
	default:
		ReturnValue = -4;
		break;
	}

	Memory::Write_U32(ReturnValue, _CommandAddress + 0x4);
	return true;
}

u32 CWII_IPC_HLE_Device_usb_oh0::AddDeviceInsertionHook(SVidPidClass& Match, u32 _CommandAddress)
{
	u32 Id = ++m_InsertionHookId;
	auto iitr = m_DeviceInsertionHooks.insert(std::make_pair(Id, std::make_pair(Match, _CommandAddress))).first;
	Ref();

	auto List = USBInterface::GetDeviceList();
	DEBUG_LOG(WII_IPC_USB, "%zu existing devices", List.size());
	for (auto it = List.begin(); it != List.end(); ++it)
	{
		DEBUG_LOG(WII_IPC_USB, "trying to match");
		if (MatchDevice(*it, iitr))
		{
			break;
		}
	}

	return Id;
}

bool CWII_IPC_HLE_Device_usb_oh0::IOCtlV(u32 _CommandAddress)
{
	u32 ReturnValue = 0;

	SIOCtlVBuffer CommandBuffer(_CommandAddress);
	DEBUG_LOG(WII_IPC_USB, "oh0 ioctlv %u", CommandBuffer.Parameter);

	switch (CommandBuffer.Parameter)
	{
	case USBV0_ROOT_IOCTLV_GETDEVLIST:
	{
		struct SDevList
		{
			u32 Unk;
			u16 Vid;
			u16 Pid;
		};
		u8 Count = Memory::Read_U8(CommandBuffer.InBuffer[0].m_Address);
		u8 Class = Memory::Read_U8(CommandBuffer.InBuffer[1].m_Address);
		SDevList* OutBuf = (SDevList*) Memory::GetPointer(CommandBuffer.PayloadBuffer[1].m_Address);
		auto List = USBInterface::GetDeviceList();
		u8 RealCount = 0;
		for (auto it = List.begin(); it != List.end() && Count; ++it)
		{
			USBInterface::USBDeviceDescriptorEtc* Device = *it;
			if (!Class || Device->bDeviceClass == Class)
			{
				OutBuf->Vid = Device->idVendor;
				OutBuf->Pid = Device->idProduct;
				Count--;
				OutBuf++;
				RealCount++;
			}
		}
		Memory::Write_U8(RealCount, CommandBuffer.PayloadBuffer[2].m_Address);
		break;
	}
	case USBV0_ROOT_IOCTLV_GETRHPORTSTATUS:
	{
		INFO_LOG(WII_IPC_USB, "oh0 - Get RH port status stub");
		Memory::Write_U32(0, CommandBuffer.PayloadBuffer[1].m_Address);
		break;
	}
	case USBV0_ROOT_IOCTLV_SETRHPORTSTATUS:
	{

		INFO_LOG(WII_IPC_USB, "oh0 - Set RH port status stub");
		break;
	}
	case USBV0_ROOT_IOCTLV_DEVINSERTHOOK:
	{
		SVidPidClass Match = {
			Memory::Read_U16(CommandBuffer.InBuffer[0].m_Address),
			Memory::Read_U16(CommandBuffer.InBuffer[1].m_Address),
			0
		};
		INFO_LOG(WII_IPC_USB, "oh0 - DEVINSERTHOOK %u, %u", Match.Vid, Match.Pid);
		AddDeviceInsertionHook(Match, _CommandAddress);
		return false;
	}
	case USBV0_ROOT_IOCTLV_DEVCLASSCHANGE:
	{
		SVidPidClass Match = {
			0,
			0,
			Memory::Read_U8(CommandBuffer.InBuffer[0].m_Address)
		};
		INFO_LOG(WII_IPC_USB, "oh0 - DEVCLASSCHANGE %u", Match.Class);
		AddDeviceInsertionHook(Match, _CommandAddress);
		return false;
	}
	case USBV0_ROOT_IOCTLV_DEVINSERTHOOKID:
	{
		SVidPidClass Match = {
			Memory::Read_U16(CommandBuffer.InBuffer[0].m_Address),
			Memory::Read_U16(CommandBuffer.InBuffer[1].m_Address),
			Memory::Read_U8(CommandBuffer.InBuffer[2].m_Address)
		};
		INFO_LOG(WII_IPC_USB, "oh0 - DEVINSERTHOOKID %u, %u, %u", Match.Vid, Match.Pid, Match.Class);
		u32 Id = AddDeviceInsertionHook(Match, _CommandAddress);
		Memory::Write_U32(Id, CommandBuffer.PayloadBuffer[0].m_Address);
		return false;
	}
	default:
		ReturnValue = -4;
		break;
	}

	Memory::Write_U32(ReturnValue, _CommandAddress+4);
	return true;
}

void CWII_IPC_HLE_Device_usb_oh0::DoState(PointerWrap& p)
{
	p.Do(m_DeviceInsertionHooks);
	p.Do(m_InsertionHookId);
}

CWII_IPC_HLE_Device_usb_oh0_dev::CWII_IPC_HLE_Device_usb_oh0_dev(const std::string& _rDeviceName, u16 Vid, u16 Pid)
: IWII_IPC_HLE_Device_usb_x(_rDeviceName)
{
	auto P = OpenVidPid(Vid, Pid, this);
	m_OpenInfo = P.first;
	m_Device = P.second;
}

CWII_IPC_HLE_Device_usb_oh0_dev::~CWII_IPC_HLE_Device_usb_oh0_dev()
{
	if (m_Device)
	{
		m_Device->Close();
	}
}

u32 CWII_IPC_HLE_Device_usb_oh0_dev::Open(u32 _CommandAddress, u32 _Mode)
{
	if (!m_Device)
	{
		WARN_LOG(WII_IPC_HLE, "oh0 device open failed");
		return FS_ENOENT;
	}
	return IWII_IPC_HLE_Device::Open(_CommandAddress, _Mode);
}

void CWII_IPC_HLE_Device_usb_oh0_dev::Unref()
{
	delete this;
}

bool CWII_IPC_HLE_Device_usb_oh0_dev::IOCtl(u32 _CommandAddress)
{
	if (!m_Device)
	{
		Memory::Write_U32(-1, _CommandAddress + 0x4);
		return true;
	}

	u32 Command			= Memory::Read_U32(_CommandAddress + 0x0C);
	u32 ReturnValue = 0;
	DEBUG_LOG(WII_IPC_USB, "oh0dev ioctl %u", Command);

	switch (Command)
	{
	case USBV0_DEV_IOCTL_SUSPENDDEV:
		break;
	case USBV0_DEV_IOCTL_RESUMEDEV:
		break;
	case USBV0_DEV_IOCTL_DEVREMOVALHOOK:
	{
		if (m_RemovalCommandAddress)
		{
			ReturnValue = -4;
			break;
		}
		m_RemovalCommandAddress = _CommandAddress;
		return false;
	}
	case USBV0_DEV_IOCTL_RESETDEV:
		WARN_LOG(WII_IPC_USB, "USBV0: received reset request, ignoring");
		break;
	}

	Memory::Write_U32(ReturnValue, _CommandAddress + 0x4);
	return true;
}

// XXX before submission, tighten all these pointers
bool CWII_IPC_HLE_Device_usb_oh0_dev::IOCtlV(u32 _CommandAddress)
{
	if (!m_Device)
	{
		Memory::Write_U32(-1, _CommandAddress + 0x4);
		return true;
	}

	u32 ReturnValue = 0;
	u32 UserData[3];
	UserData[0] = _CommandAddress;
	UserData[1] = 0;

	SIOCtlVBuffer CommandBuffer(_CommandAddress);
	DEBUG_LOG(WII_IPC_USB, "oh0dev ioctlv %u", CommandBuffer.Parameter);

	switch (CommandBuffer.Parameter)
	{
	case USBV0_DEV_IOCTLV_CTRLMSG:
	{
		USBInterface::USBSetup Setup;
		Setup.bmRequestType = Memory::Read_U8(CommandBuffer.InBuffer[0].m_Address);
		Setup.bRequest = Memory::Read_U8(CommandBuffer.InBuffer[1].m_Address);
		// These are little endian, unlike corresponding values in other requests.
		u16 Values[3];
		for (int i = 0; i < 3; i++)
		{
			Values[i] = Common::le16(*(u16*) Memory::GetPointer(CommandBuffer.InBuffer[i+2].m_Address));
		}
		Setup.wValue = Values[0];
		Setup.wIndex = Values[1];
		Setup.wLength = Values[2];
		u8 Unk = Memory::Read_U8(CommandBuffer.InBuffer[5].m_Address);
		void* Payload = Memory::GetPointer(CommandBuffer.PayloadBuffer[0].m_Address);
		DEBUG_LOG(WII_IPC_USB, "USBV0: bRequest=%x wValue=%x wIndex=%x wLength=%x unk=%x",
				 Setup.bRequest, Setup.wValue, Setup.wIndex, Setup.wLength, Unk);
		m_Device->ControlRequest(&Setup, Payload, UserData);
		return false;
	}
	case USBV0_DEV_IOCTLV_BLKMSG:
	case USBV0_DEV_IOCTLV_INTRMSG:
	{
		u8 Endpoint = Memory::Read_U8(CommandBuffer.InBuffer[0].m_Address);
		u16 Length = Memory::Read_U16(CommandBuffer.InBuffer[1].m_Address);
		void* Payload = Memory::GetPointer(CommandBuffer.InBuffer[2].m_Address);
		if (CommandBuffer.Parameter == USBV0_DEV_IOCTLV_BLKMSG)
		{
			m_Device->BulkRequest(Endpoint, Length, Payload, UserData);
		}
		else
		{
			m_Device->InterruptRequest(Endpoint, Length, Payload, UserData);
		}
		return false;
	}
	case USBV0_DEV_IOCTLV_ISOMSG:
	{
		u8 Endpoint = Memory::Read_U8(CommandBuffer.InBuffer[0].m_Address);
		u16 Length = Memory::Read_U16(CommandBuffer.InBuffer[1].m_Address);
		u8 NumPackets = Memory::Read_U8(CommandBuffer.InBuffer[2].m_Address);
		u32 LengthsBufAddr = CommandBuffer.PayloadBuffer[0].m_Address;
		u16* LengthsBuf = (u16*) Memory::GetPointer(LengthsBufAddr);
		void* Payload = Memory::GetPointer(CommandBuffer.PayloadBuffer[1].m_Address);
		for (u8 i = 0; i < NumPackets; i++) {
			LengthsBuf[i] = Common::swap16(LengthsBuf[i]) & 0xfff;
		}
		UserData[1] = NumPackets;
		UserData[2] = LengthsBufAddr;
		m_Device->IsochronousRequest(Endpoint, Length, NumPackets, LengthsBuf, Payload, UserData);
		return false;
	}
	case USBV0_DEV_IOCTLV_LONGITRMSG:
		WARN_LOG(WII_IPC_USB, "USBV0: long message unimplemented");
		ReturnValue = -4;
		break;
	}

	Memory::Write_U32(ReturnValue, _CommandAddress + 0x4);
	return true;
}

void CWII_IPC_HLE_Device_usb_oh0_dev::DoState(PointerWrap& p)
{
	p.Do(m_OpenInfo);
	p.Do(m_RemovalCommandAddress);
	if (p.GetMode() == PointerWrap::MODE_READ)
	{
		USBInterface::ReadDeviceState(p, this);
		m_Device = NULL;
	}
	else
	{
		m_Device->WriteDeviceState(p);
	}
}

void CWII_IPC_HLE_Device_usb_oh0_dev::USBDevicesChanged(std::vector<USBInterface::USBDeviceDescriptorEtc*>& Devices)
{
	if (!m_RemovalCommandAddress)
	{
		return;
	}
	for (auto itr = Devices.begin(); itr != Devices.end(); ++itr)
	{
		if ((*itr)->OpenInfo == m_OpenInfo)
		{
			return;
		}
	}
	DEBUG_LOG(WII_IPC_USB, "USBV0: removal command");
	Memory::Write_U32(0, m_RemovalCommandAddress + 0x4);
	WII_IPC_HLE_Interface::EnqReply(m_RemovalCommandAddress);
	m_RemovalCommandAddress = 0;
}

void CWII_IPC_HLE_Device_usb_oh0_dev::USBRequestComplete(void* UserData, u32 Status)
{
	u32* Data = (u32*) UserData;
	u32 _CommandAddress = Data[0];
	u32 NumPackets = Data[1];
	u32 LengthsBufAddr = Data[2];
	if (NumPackets && Status <= 0x7fffffff)
	{
		// Isochronous request
		u16* LengthsBuf = (u16*) Memory::GetPointer(LengthsBufAddr);
		for (u32 i = 0; i < NumPackets; i++)
		{
			LengthsBuf[i] = Common::swap16(LengthsBuf[i]);
		}
	}
	Memory::Write_U32(Status, _CommandAddress + 0x4);
	WII_IPC_HLE_Interface::EnqReply(_CommandAddress);
}

IWII_IPC_HLE_Device* CWII_IPC_HLE_Device_usb_oh0_dev::Create(const std::string& Name)
{
	unsigned int Vid, Pid;
	int End;
	const char* CName = Name.c_str();

	if (sscanf(CName, "/dev/usb/oh0/%x/%x%n", &Vid, &Pid, &End) != 2 ||
		CName[End] != 0)
	{
		return NULL;
	}
	DEBUG_LOG(WII_IPC_USB, "OH0: Opening %s", CName);
	USBInterface::RefInterface();
	return new CWII_IPC_HLE_Device_usb_oh0_dev(Name, Vid, Pid); 
}
