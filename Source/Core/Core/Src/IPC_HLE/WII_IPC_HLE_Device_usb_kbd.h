// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _WII_IPC_HLE_DEVICE_USB_KBD_H_
#define _WII_IPC_HLE_DEVICE_USB_KBD_H_

#include "WII_IPC_HLE_Device.h"
#include "KeyboardClient.h"

class CWII_IPC_HLE_Device_usb_kbd :
public IWII_IPC_HLE_Device, public CWII_IPC_HLE_Device_Singleton<CWII_IPC_HLE_Device_usb_kbd>,
public CKeyboardClient
{
public:
	CWII_IPC_HLE_Device_usb_kbd(const std::string& _rDeviceName);
	virtual ~CWII_IPC_HLE_Device_usb_kbd();

	virtual bool Write(u32 _CommandAddress);
	virtual bool IOCtl(u32 _CommandAddress);

	static const char* GetBaseName() { return "/dev/usb/kbd"; }
protected:
    virtual void UpdateKeyboardReport(u8* Data) override;
    virtual void SetKeyboardClientEnabled(bool Enabled) override;
private:
	enum
	{
		MSG_KBD_CONNECT = 0,
		MSG_KBD_DISCONNECT,
		MSG_EVENT
	};

	struct SMessageData
	{
		u32 MsgType;
		u32 Unk1;
		u8 Data[8];
	};

	void PushMessage(SMessageData& Message);

	std::queue<SMessageData> m_MessageQueue;
	u32 m_CommandAddress;
	bool m_WasEnabled;
};

#endif // _WII_IPC_HLE_DEVICE_USB_KBD_H_
