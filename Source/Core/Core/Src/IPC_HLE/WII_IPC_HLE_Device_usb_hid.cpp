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

#include "../Core.h"
#include "../Debugger/Debugger_SymbolMap.h"
#include "../HW/WII_IPC.h"
#include "WII_IPC_HLE.h"
#include "WII_IPC_HLE_Device_usb_hid.h"
#include "errno.h"

CWII_IPC_HLE_Device_usb_hid::CWII_IPC_HLE_Device_usb_hid(const std::string& _rDeviceName)
	: IWII_IPC_HLE_Device(_rDeviceName)
{
	m_DeviceCommandAddress = 0;
	m_DidInitialList = false;
	USBInterface::RefInterface();
	USBInterface::RegisterDeviceChangeClient(this);
}

void CWII_IPC_HLE_Device_usb_hid::Shutdown()
{
	for (auto itr = m_OpenDevices.begin(); itr != m_OpenDevices.end(); ++itr)
	{
		itr->second->Close();
	}
	m_OpenDevices.clear();
}

USBInterface::IUSBDevice* CWII_IPC_HLE_Device_usb_hid::GetDevice(u32 DevNum)
{
	auto itr = m_OpenDevices.find(DevNum);
	if (itr != m_OpenDevices.end())
	{
		return itr->second;
	}
	DEBUG_LOG(WII_IPC_USB, "HID opening device %x", DevNum);
	auto uitr = m_Uids.find(DevNum);
	if (uitr == m_Uids.end())
	{
		WARN_LOG(WII_IPC_USB, "No such UID %x", DevNum);
		return NULL;
	}

	USBInterface::IUSBDevice* Device = USBInterface::OpenDevice(uitr->second, this);
	if (!Device)
	{
		WARN_LOG(WII_IPC_USB, "Couldn't open device %x", DevNum);
		return NULL;
	}

	m_OpenDevices[DevNum] = Device;
	return Device;
}

CWII_IPC_HLE_Device_usb_hid::~CWII_IPC_HLE_Device_usb_hid()
{
	Shutdown();
	USBInterface::DeregisterDeviceChangeClient(this);
}

void CWII_IPC_HLE_Device_usb_hid::DoState(PointerWrap& p)
{
	p.Do(m_DeviceCommandAddress);
	p.Do(m_DidInitialList);
	u32 Count;
	if (p.GetMode() == PointerWrap::MODE_READ)
	{
		p.Do(Count);
		for (u32 i = 0; i < Count; i++)
		{
			USBInterface::ReadDeviceState(p, this);
		}
	}
	else
	{
		Count = m_OpenDevices.size();
		p.Do(Count);
		for (auto itr = m_OpenDevices.begin(); itr != m_OpenDevices.end(); ++itr)
		{
			itr->second->WriteDeviceState(p);
		}
	}
}

bool CWII_IPC_HLE_Device_usb_hid::IOCtl(u32 _CommandAddress)
{
	u32 Parameter		= Memory::Read_U32(_CommandAddress + 0xC);
	u32 BufferIn		= Memory::Read_U32(_CommandAddress + 0x10);
	u32 BufferInSize	= Memory::Read_U32(_CommandAddress + 0x14);
	u32 BufferOut		= Memory::Read_U32(_CommandAddress + 0x18);
	u32 BufferOutSize	= Memory::Read_U32(_CommandAddress + 0x1C);

	SHidUserData UserData = { _CommandAddress, Parameter, NULL };

	u32 ReturnValue = 0;
	switch (Parameter)
	{
	case IOCTL_HID_GET_ATTACHED:
	{
		DEBUG_LOG(WII_IPC_USB, "HID::IOCtl(Get Attached) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			BufferIn, BufferInSize, BufferOut, BufferOutSize);
		if (m_DeviceCommandAddress)
		{
			// Already listening
			ReturnValue = -1000;
			break;
		}
		m_DeviceCommandAddress = _CommandAddress;
		if (!m_DidInitialList)
		{
			m_DidInitialList = true;
			auto List = USBInterface::GetDeviceList();
			USBDevicesChanged(List);
			USBInterface::ReleaseDeviceList();
		}
		return false;
	}
	case IOCTL_HID_OPEN:
	{
		DEBUG_LOG(WII_IPC_USB, "HID::IOCtl(Open) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			BufferIn, BufferInSize, BufferOut, BufferOutSize);

		//hid version, apparently
		ReturnValue = 0x40001;
		break;
	}
	case IOCTL_HID_SET_SUSPEND:
	{
		DEBUG_LOG(WII_IPC_USB, "HID::IOCtl(Set Suspend) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			BufferIn, BufferInSize, BufferOut, BufferOutSize);
		// not actually implemented in IOS
		ReturnValue = 0;
		break;
	}
	case IOCTL_HID_CANCEL_INTERRUPT:
	{
		_dbg_assert_(WII_IPC_USB, BufferInSize == 8 && BufferOutSize == 0);

		u32 DevNum = Memory::Read_U32(BufferIn);
		u8 Endpoint = Memory::Read_U8(BufferIn+4);
		u8 Unk = Memory::Read_U8(BufferIn+5);
		DEBUG_LOG(WII_IPC_USB, "HID::IOCtl(Cancel Interrupt) (DevNum: %u, Endpoint: %u, Unk: %u)",
			DevNum, Endpoint, Unk);
		USBInterface::IUSBDevice* Device = GetDevice(DevNum);
		if (!Device) {
			ReturnValue = HIDERR_NO_DEVICE_FOUND;
			break;
		}
		Device->CancelRequests(Endpoint);
		break;
	}
	case IOCTL_HID_CONTROL:
	{
		/*
			ERROR CODES:
			-4 Cant find device specified
		*/

		u32 DevNum  = Memory::Read_U32(BufferIn+0x10);
		USBInterface::USBSetup Setup = *(USBInterface::USBSetup*) Memory::GetPointer(BufferIn + 0x14);
		Setup.wValue = Common::le16(Setup.wValue);
		Setup.wIndex = Common::le16(Setup.wIndex);
		Setup.wLength = Common::le16(Setup.wLength);
		u32 Data = Memory::Read_U32(BufferIn+0x1C);

		USBInterface::IUSBDevice* Device = GetDevice(DevNum);
		if (!Device) {
			ReturnValue = HIDERR_NO_DEVICE_FOUND;
			break;
		}

		Device->ControlRequest(&Setup, Memory::GetPointer(Data), &UserData);
		return false;
	}
	case IOCTL_HID_INTERRUPT_OUT:
	case IOCTL_HID_INTERRUPT_IN:
	{
		u32 DevNum  = Memory::Read_U32(BufferIn+0x10);
		u32 Endpoint = Memory::Read_U32(BufferIn+0x14);
		u32 Length = Memory::Read_U32(BufferIn+0x18);
		u32 Data = Memory::Read_U32(BufferIn+0x1C);

		USBInterface::IUSBDevice* Device = GetDevice(DevNum);
		if (!Device) {
			ReturnValue = HIDERR_NO_DEVICE_FOUND;
			break;
		}

		Device->InterruptRequest(Endpoint, Length, Memory::GetPointer(Data), &UserData);
		return false;
	}
	case IOCTL_HID_SHUTDOWN:
	{
		Shutdown();
	}
	case IOCTL_HID_GET_US_STRING:
	{
		u32 DevNum  = Memory::Read_U32(BufferIn+0x10);
		u8 Index = Memory::Read_U8(BufferIn+0x14);

		USBInterface::IUSBDevice* Device = GetDevice(DevNum);
		if (!Device) {
			ReturnValue = HIDERR_NO_DEVICE_FOUND;
			break;
		}

		USBInterface::USBSetup Setup;
		Setup.bmRequestType = 0x80;
		Setup.bRequest = 6;
		Setup.wValue = 0x300 | Index;
		Setup.wIndex = 0x0409;
		Setup.wLength = 0x80;

		u8* DescriptorBuf = new u8[0x80];
		UserData.DescriptorBuf = DescriptorBuf;

		Device->ControlRequest(&Setup, DescriptorBuf, &UserData);
	}
	default:
	{
		DEBUG_LOG(WII_IPC_USB, "HID::IOCtl(0x%x) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			Parameter, BufferIn, BufferInSize, BufferOut, BufferOutSize);
		break;
	}
	}

	Memory::Write_U32(ReturnValue, _CommandAddress + 4);

	return true;
}


bool CWII_IPC_HLE_Device_usb_hid::FillAttachedReply(std::vector<USBInterface::USBDeviceDescriptorEtc*>& Devices, void* Buffer, size_t Size)
{
#define APPEND(Data, DataSize) do { OldPtr = Ptr; if (!AppendRaw(&Ptr, &Size, Data, DataSize)) return false; } while (0)
	using namespace USBInterface;
	u32 Uid = 0;
	u8* Ptr = (u8*) Buffer;
	u8* OldPtr;
	u32 Dummy = 0;
	for (auto DItr = Devices.begin(); DItr != Devices.end(); ++DItr)
	{
		if (Uid >= 100)
		{
			// too many devices, game will crash
			break;
		}
		USBDeviceDescriptorEtc* Device = *DItr;
		if (Device->IsOpen || Device->Configs.size() == 0)
		{
			continue;
		}
		u8* DeviceStart = Ptr;
		APPEND(&Dummy, 4); // size
		u32 UidSwapped = Common::swap32(Uid);
		APPEND(&UidSwapped, 4);
		m_Uids[Uid] = Device->OpenInfo;
		Uid++;

		APPEND(Device, sizeof(USBDeviceDescriptor));
		SwapDeviceDescriptor((USBDeviceDescriptor*) OldPtr);

		USBConfigDescriptorEtc* Config = &Device->Configs.front();
		APPEND(Config, sizeof(USBConfigDescriptor));
		SwapConfigDescriptor((USBConfigDescriptor*) OldPtr);

		for (auto IItr = Config->Interfaces.begin(); IItr != Config->Interfaces.end(); ++IItr)
		{
			USBInterfaceDescriptorEtc* Interface = &*IItr;
			APPEND(Interface, sizeof(USBInterfaceDescriptor));
			SwapInterfaceDescriptor((USBInterfaceDescriptor*) OldPtr);

			if (Interface->bNumEndpoints != 1 && Interface->bNumEndpoints != 2)
			{
				Ptr = DeviceStart;
				goto BadDevice;
			}

			for (auto EItr = Interface->Endpoints.begin(); EItr != Interface->Endpoints.end(); ++EItr)
			{
				USBEndpointDescriptorEtc* Endpoint = &*EItr;
				// IOS only copies out 8 bytes of the endpoint descriptor.
				APPEND(Endpoint, 8);
				SwapEndpointDescriptor((USBEndpointDescriptor*) OldPtr);
			}
		}
		*(u32*) DeviceStart = Common::swap32(Ptr - DeviceStart);
		BadDevice:;
	}
	u32 End = 0xffffffff;
	APPEND(&End, sizeof(End));
#undef APPEND
	return true;
}

void CWII_IPC_HLE_Device_usb_hid::USBDevicesChanged(std::vector<USBInterface::USBDeviceDescriptorEtc*>& Devices)
{
	m_Uids.clear();
	if (!m_DeviceCommandAddress) return;

	Memory::Write_U32(8, m_DeviceCommandAddress);
	// IOS seems to write back the command that was responded to
	Memory::Write_U32(/*COMMAND_IOCTL*/ 6, m_DeviceCommandAddress + 8);
	u32 Buffer = Memory::Read_U32(m_DeviceCommandAddress + 0x18);
	u32 Size = Memory::Read_U32(m_DeviceCommandAddress + 0x1C);
	bool Result = FillAttachedReply(Devices, Memory::GetPointer(Buffer), Size);

	// Return value
	Memory::Write_U32(Result ? 0 : -1000, m_DeviceCommandAddress + 4);

	WII_IPC_HLE_Interface::EnqReply(m_DeviceCommandAddress);
	m_DeviceCommandAddress = 0;
}

void CWII_IPC_HLE_Device_usb_hid::USBRequestComplete(void* UserData, u32 Status)
{
	SHidUserData* Data = (SHidUserData*) UserData;
	u32 CommandAddress = Data->CommandAddress;
	if (Data->DescriptorBuf) // GET_US_STRING
	{
		if (Status == 0) {
			u32 BufferOut = Memory::Read_U32(CommandAddress + 0x18);
			u32 BufferOutSize = Memory::Read_U32(CommandAddress + 0x1C);
			u8* OutPtr = (u8*) Memory::GetPointer(BufferOut);
			USBInterface::USBStringDescriptor* Desc =
				(USBInterface::USBStringDescriptor*) Data->DescriptorBuf;
			u8 Length = Desc->bLength;
			if (Desc->bDescriptorType != 3 ||
				Length > 0x80 - offsetof(USBInterface::USBStringDescriptor, bString))
			{
				Status = -1;
			}
			else
			{
				Length = std::min((u32) Length, BufferOutSize);
				for (u8 i = 0; i < Length; i++)
				{
					u16 Char = Common::le16(Desc->bString[i]);
					OutPtr[i] = Char >= 0x100 ? '?' : Char;
				}
				Status = Length;
			}
		}
		delete (u8*) Data->DescriptorBuf;
	}

	Memory::Write_U32(8, CommandAddress);
	Memory::Write_U32(Status, CommandAddress + 4);
	Memory::Write_U32(Data->Parameter, CommandAddress + 8);
	WII_IPC_HLE_Interface::EnqReply(CommandAddress);
}
