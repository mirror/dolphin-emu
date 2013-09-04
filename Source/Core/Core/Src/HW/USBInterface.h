// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "Common.h"
#include "IntrusiveList.h"
#include "Thread.h"
#include <vector>
#include <memory>
#include <stddef.h>

#if !defined(final) && __cplusplus < 201103
#define final
#endif

#ifdef USB_DEBUG
#undef DEBUG_LOG
#define DEBUG_LOG WARN_LOG
#endif

// need somewhere better to put this
template <typename P, template<typename> class BaseHash = std::hash>
struct PairHash
{
	size_t operator()(const P& Pair) const
	{
		return BaseHash<typename P::first_type>()(Pair.first) ^
			   BaseHash<typename P::second_type>()(Pair.second);
	}
};

class PointerWrap;

static inline void AppendData(std::vector<u8>& Buf, const void* Data, size_t Size)
{
	size_t Off = Buf.size();
	Buf.resize(Off + Size);
	memcpy(&Buf[Off], Data, Size);
}

static inline bool AppendRaw(u8** Ptr, size_t* Remaining, const void* Data, size_t Size)
{
	if (Size > *Remaining)
	{
		return false;
	}
	memcpy(*Ptr, Data, Size);
	*Ptr += Size;
	*Remaining -= Size;
	return true;
}

template <typename T>
static inline T& EmplaceBack(std::vector<T>& Vec)
{
	T Result;
	Vec.push_back(Result);
	return Vec.back();
}

namespace USBInterface
{

class IUSBController;
typedef std::pair<IUSBController*, void*> TUSBDeviceOpenInfo;

enum
{
	UsbUserDataSize = 16
};

enum
{
	/*
		-7022: returned from insertion callback when cancelled
			   returned from transfers when device removed
			   RB3 intr_in callback checks for this error
		-(7000+OHCI condition code): printf'ed from IOS, not actually
									 returned
		   -6: returned from insertion callback when fd closed 
		   -1, -4: returned from various ioctls on error
			0: actually returned on USB error?
			   returned from cancelled HID request
	*/

	UsbErrDisconnected = -7022,
	UsbErrDefault = -7000
};

// size: 0x14
struct USBDeviceDescriptor
{
	u8 bLength;
	u8 bDescriptorType;
	u16 bcdUSB;
	u8 bDeviceClass;
	u8 bDeviceSubClass;
	u8 bDeviceProtocol;
	u8 bMaxPacketSize0;
	u16 idVendor;
	u16 idProduct;
	u16 bcdDevice;
	u8 iManufacturer;
	u8 iProduct;
	u8 iSerialNumber;
	u8 bNumConfigurations;
	u8 pad[2];
};

// size: 0xc
struct USBConfigDescriptor
{
	u8 bLength;
	u8 bDescriptorType;
	u16 wTotalLength;
	u8 bNumInterfaces;
	u8 bConfigurationValue;
	u8 iConfiguration;
	u8 bmAttributes;
	u8 MaxPower;
	u8 pad[3];
};

// size: 0xc
struct USBInterfaceDescriptor
{
	u8 bLength;
	u8 bDescriptorType;
	u8 bInterfaceNumber;
	u8 bAlternateSetting;
	u8 bNumEndpoints;
	u8 bInterfaceClass;
	u8 bInterfaceSubClass;
	u8 bInterfaceProtocol;
	u8 iInterface;
	u8 pad[3];
};

// size: ? (8)
struct USBEndpointDescriptor
{
	u8 bLength;
	u8 bDescriptorType;
	u8 bEndpointAddress;
	u8 bmAttributes;
	u16 wMaxPacketSize;
	u8 bInterval;
	u8 bRefresh;
	u8 bSynchAddress;
	u8 pad[1];
};

struct USBStringDescriptor
{
	u8 bLength;
	u8 bDescriptorType;
	u16 bString[0];
};

struct USBEndpointDescriptorEtc : public USBEndpointDescriptor
{
	bool operator==(const USBEndpointDescriptorEtc& other) const {
		return !memcmp(this, &other, offsetof(USBEndpointDescriptor, pad));
	}
	void Fixup()
	{
		bLength = 0x07;
		bDescriptorType = 5;
	}
};

struct USBInterfaceDescriptorEtc : public USBInterfaceDescriptor
{
	std::vector<USBEndpointDescriptorEtc> Endpoints;
	bool operator==(const USBInterfaceDescriptorEtc& other) const {
		return !memcmp(this, &other, offsetof(USBInterfaceDescriptor, pad)) &&
		       Endpoints == other.Endpoints;
	}
	void Fixup()
	{
		bLength = 0x09;
		bDescriptorType = 4;
		bNumEndpoints = Endpoints.size();
	}
};

struct USBConfigDescriptorEtc : public USBConfigDescriptor
{
	std::vector<u8> Rest;
	std::vector<USBInterfaceDescriptorEtc> Interfaces;
	bool operator==(const USBConfigDescriptorEtc& other) const {
		return !memcmp(this, &other, offsetof(USBConfigDescriptor, pad)) &&
		       Rest == other.Rest &&
		       Interfaces == other.Interfaces;
	}
	void Fixup()
	{
		bLength = 0x09;
		bDescriptorType = 2;
		bNumInterfaces = Interfaces.size();

		wTotalLength = 0;
		for (auto& Interface : Interfaces)
		{
			wTotalLength += Interface.bLength + 0x07 * Interface.Endpoints.size();
		}
		wTotalLength += Rest.size();
	}
};

struct USBDeviceDescriptorEtc : public USBDeviceDescriptor
{
	std::string Name;
	TUSBDeviceOpenInfo OpenInfo;
	std::vector<USBConfigDescriptorEtc> Configs;
	bool operator==(const USBDeviceDescriptorEtc& other) const {
		return OpenInfo == other.OpenInfo &&
		       !memcmp(this, &other, offsetof(USBDeviceDescriptor, pad)) &&
		       Configs == other.Configs;
	}
	void Fixup()
	{
		bLength = 0x12;
		bDescriptorType = 1;
		bNumConfigurations = Configs.size();
	}
};

struct USBSetup
{
	u8 bmRequestType;
	u8 bRequest;
	u16 wValue;
	u16 wIndex;
	u16 wLength;
};

static inline void SwapDeviceDescriptor(USBDeviceDescriptor* Desc)
{
	Desc->bcdUSB = Common::swap16(Desc->bcdUSB);
	Desc->idVendor = Common::swap16(Desc->idVendor);
	Desc->idProduct = Common::swap16(Desc->idProduct);
	Desc->bcdDevice = Common::swap16(Desc->bcdDevice);
}

static inline void SwapConfigDescriptor(USBConfigDescriptor* Desc)
{
	Desc->wTotalLength = Common::swap16(Desc->wTotalLength);
}

static inline void SwapInterfaceDescriptor(USBInterfaceDescriptor* Desc)
{
}

static inline void SwapEndpointDescriptor(USBEndpointDescriptor* Desc)
{
	Desc->wMaxPacketSize = Common::swap16(Desc->wMaxPacketSize);
}

class IUSBDevice;
class IUSBDeviceClient;

class CUSBRequest : public IntrusiveMember<CUSBRequest>
{
public:
	CUSBRequest(IUSBDevice* Device, void* UserData, s16 Endpoint, bool IsFake = false);
	virtual ~CUSBRequest() {}
	virtual void Complete(u32 Status);
	virtual void Cancel() {}

	char m_UserData[UsbUserDataSize];
	u32 m_Status;
	s16 m_Endpoint;
	bool m_IsFake;
protected:
	IUSBDevice* m_Device;
};

class IUSBController
{
public:
	IUSBController() { m_NewDeviceList = NULL; }
	virtual void Destroy() = 0;
	virtual IUSBDevice* OpenDevice(TUSBDeviceOpenInfo OpenInfo, IUSBDeviceClient* Client) = 0;
	virtual void UpdateShouldScan() = 0;
	virtual void DestroyDeviceList(std::vector<USBDeviceDescriptorEtc>& Old) = 0;

	bool UpdateDeviceList();
	void DestroyOldDeviceList();
	static void UpdateGlobalDeviceList();
protected:
	void SetDeviceList(std::vector<USBDeviceDescriptorEtc>&& List);
	virtual ~IUSBController();
private:
	std::vector<USBDeviceDescriptorEtc>* volatile m_NewDeviceList;
	std::vector<USBDeviceDescriptorEtc> m_DeviceList, m_OldDeviceList;
};

// public
class IUSBDeviceClient
{
public:
	virtual void USBRequestComplete(void* UserData, u32 Status, bool WasThawed) = 0;
};

class IUSBDeviceChangeClient
{
public:
	virtual void USBDevicesChanged(std::vector<USBDeviceDescriptorEtc*>& Devices) = 0;
};

class IUSBDevice : public IntrusiveMember<IUSBDevice>
{
public:
	void CancelRequests(u8 Endpoint);
	// return value is whether this superclass handled it
	virtual bool ControlRequest(const USBSetup* Setup, void* Payload, void* UserData);
	virtual void Close();
	virtual void ProcessPending();
	void WriteDeviceState(PointerWrap& p);

	virtual u32 SetConfig(int Config) = 0;
	virtual u32 SetDefaultConfig() = 0;
	virtual u32 SetInterfaceAltSetting(int Interface, int Setting) = 0;
	virtual void BulkRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData) = 0;
	virtual void InterruptRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData) = 0;
	virtual void IsochronousRequest(u8 Endpoint, size_t Length, size_t NumPackets, u16* PacketLengths, void* Payload, void* UserData) = 0;
protected:
	IUSBDevice(TUSBDeviceOpenInfo OpenInfo, IUSBDeviceClient* Client);

	friend class CUSBRequest;
	IntrusiveList<CUSBRequest> m_IncompleteRequests;
	IntrusiveList<CUSBRequest> m_PendingRequests;
	bool m_Pending;

private:
	void CancelRequestsInList(IntrusiveList<CUSBRequest>& List, u8 Endpoint);

	IUSBDeviceClient* m_Client;
	TUSBDeviceOpenInfo m_OpenInfo;
};

// private
	void ReadDeviceState(PointerWrap& p, IUSBDeviceClient* Client);
	extern volatile bool g_ShouldScan;
	extern std::mutex g_QueueMutex;

// public
	std::vector<USBDeviceDescriptorEtc*>& GetDeviceList();
	bool IsOpen(TUSBDeviceOpenInfo OpenInfo);
	void RefInterface();
	void ResetInterface();
	void RegisterDeviceChangeClient(IUSBDeviceChangeClient* Client);
	void DeregisterDeviceChangeClient(IUSBDeviceChangeClient* Client);

	std::pair<TUSBDeviceOpenInfo, IUSBDevice*> OpenVidPid(u16 Vid, u16 Pid, IUSBDeviceClient* Client);
	IUSBDevice* OpenDevice(TUSBDeviceOpenInfo OpenInfo, IUSBDeviceClient* Client);
} // namespace
