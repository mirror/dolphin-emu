// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "../ConfigManager.h"
#include "../Core.h" // Local core functions
#include "WII_IPC_HLE_Device_usb_kbd.h"
#include "FileUtil.h"

#ifdef _WIN32
#include <windows.h>
#endif

CWII_IPC_HLE_Device_usb_kbd::CWII_IPC_HLE_Device_usb_kbd(const std::string& _rDeviceName)
: IWII_IPC_HLE_Device(_rDeviceName)
{
	m_CommandAddress = 0;
	m_WasEnabled = false;
	AddKeyboardClient();
}

CWII_IPC_HLE_Device_usb_kbd::~CWII_IPC_HLE_Device_usb_kbd()
{
	DestroyKeyboardClient();
}

void CWII_IPC_HLE_Device_usb_kbd::UpdateKeyboardReport(u8* Data)
{
	SMessageData Message;
	Message.MsgType = MSG_EVENT;
	Message.Unk1 = 0;
	memcpy(Message.Data, Data, 8);
	PushMessage(Message);
}

void CWII_IPC_HLE_Device_usb_kbd::SetKeyboardClientEnabled(bool Enabled)
{
	if (Enabled != m_WasEnabled)
	{
		SMessageData Message;
		memset(&Message, 0, sizeof(Message));
		Message.MsgType = Enabled ? MSG_KBD_CONNECT : MSG_KBD_DISCONNECT;
		PushMessage(Message);
	}
	m_WasEnabled = Enabled;
}

void CWII_IPC_HLE_Device_usb_kbd::PushMessage(SMessageData& Message)
{
	m_MessageQueue.push(Message);
	if (m_CommandAddress)
	{
		u32 _CommandAddress = m_CommandAddress;
		m_CommandAddress = 0;
		IOCtl(_CommandAddress);
	}
}

bool CWII_IPC_HLE_Device_usb_kbd::Write(u32 _CommandAddress)
{
	DEBUG_LOG(WII_IPC_USB, "Ignoring write to CWII_IPC_HLE_Device_usb_kbd");
	return true;
}

bool CWII_IPC_HLE_Device_usb_kbd::IOCtl(u32 _CommandAddress)
{
    std::lock_guard<std::recursive_mutex> Lock(s_KeyboardClientMutex);
	if (!m_MessageQueue.empty())
	{
		u32 BufferOut = Memory::Read_U32(_CommandAddress + 0x18);
		*(SMessageData*) Memory::GetPointer(BufferOut) = m_MessageQueue.front();
		m_MessageQueue.pop();
		Memory::Write_U32(0, _CommandAddress + 0x4);
		return true;
	}
	else
	{
		m_CommandAddress = _CommandAddress;
		return false;
	}
}
