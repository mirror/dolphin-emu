// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "../Core.h"
#include "USBReal.h"
#include "CoreTiming.h"
#include "Timer.h"

#if defined(_MSC_VER) || defined(__MINGW32__)
#  include <time.h>
#ifndef _TIMEVAL_DEFINED /* also in winsock[2].h */
#define _TIMEVAL_DEFINED
struct timeval {
	long tv_sec;
	long tv_usec;
};
#endif /* _TIMEVAL_DEFINED */
#else
#  include <sys/time.h>
#endif

#ifdef __APPLE__
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USB.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

template <typename T>
static inline T& EmplaceBack(std::vector<T>& Vec)
{
	T Result;
	Vec.push_back(Result);
	return Vec.back();
}

namespace USBInterface
{

class CUSBRequestReal : public CUSBRequest
{
public:
	CUSBRequestReal(IUSBDevice* Device, void* UserData, s16 Endpoint, libusb_transfer* Transfer, void* UserPayload = NULL)
	: CUSBRequest(Device, UserData, Endpoint), m_Transfer(Transfer), m_UserPayload(UserPayload) {
		m_WasCancelled = false;
	}
	virtual ~CUSBRequestReal();
	virtual void Complete(u32 Status);
	virtual void Cancel();

	static void LIBUSB_CALL TransferCallback(libusb_transfer* Transfer);
protected:
	libusb_transfer* m_Transfer;
	void* m_UserPayload;
	bool m_WasCancelled;
};

CUSBRequestReal::~CUSBRequestReal()
{
	// This was also in Complete, but the result was a crash and I think this
	// really is a bug.
	libusb_free_transfer(m_Transfer);
}

void CUSBRequestReal::Complete(u32 Status)
{
	DEBUG_LOG(USBINTERFACE, "USBReal: complete %p status:%x", m_Transfer, Status);
	CUSBRequest::Complete(Status);
}

void CUSBRequestReal::Cancel()
{
	DEBUG_LOG(USBINTERFACE, "USBReal: cancel request %p", this);
	m_WasCancelled = true;
	libusb_cancel_transfer(m_Transfer);
}

void CUSBRequestReal::TransferCallback(libusb_transfer* Transfer)
{
	CUSBRequestReal* Self = (CUSBRequestReal*) Transfer->user_data;
	DEBUG_LOG(USBINTERFACE, "USBReal: transfer callback transfer=%p request=%p status=%d cancelled=%u", Transfer, Self, Transfer->status, Self->m_WasCancelled);
	if (Transfer->type == LIBUSB_TRANSFER_TYPE_CONTROL)
	{
		memcpy(Self->m_UserPayload, Transfer->buffer + sizeof(USBSetup), Transfer->length - LIBUSB_CONTROL_SETUP_SIZE);
		delete[] Transfer->buffer;
	}

	if (Self->m_WasCancelled)
	{
		Self->Complete(0);
	}

	if (Transfer->status == LIBUSB_TRANSFER_COMPLETED)
	{
		if (Transfer->type == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS)
		{
			u16* PacketLengths = (u16*) Self->m_UserPayload;
			for (int i = 0; i < Transfer->num_iso_packets; i++)
			{
				libusb_iso_packet_descriptor* Desc = &Transfer->iso_packet_desc[i];
				PacketLengths[i] = Desc->status < 0 ? 0 : Desc->actual_length;
			}
		}
		Self->Complete(Transfer->actual_length);
	}
	else
	{
		Self->Complete(UsbErrDefault);
	}
}

CUSBDeviceReal::CUSBDeviceReal(libusb_device* Device, TUSBDeviceOpenInfo OpenInfo, libusb_device_handle* Handle, CUSBControllerReal* Controller, IUSBDeviceClient* Client)
: IUSBDevice(Client, OpenInfo),
m_Device(Device), m_DeviceHandle(Handle)
{
	m_NumInterfaces = 0;
}

CUSBDeviceReal::~CUSBDeviceReal()
{
	DEBUG_LOG(USBINTERFACE, "USBReal: actually closing device handle %p", m_DeviceHandle);
	for (int i = 0; i < m_NumInterfaces; i++)
	{
		libusb_release_interface(m_DeviceHandle, i);
		libusb_attach_kernel_driver(m_DeviceHandle, i);
	}
	libusb_close(m_DeviceHandle);
}

void CUSBDeviceReal::_Close()
{
	DEBUG_LOG(USBINTERFACE, "USBReal: requested close");
	// We might have to wait for outstanding requests.
	m_WasClosed = true;
	std::lock_guard<std::mutex> Guard(g_QueueMutex);
	CheckClose();
}

// Called with g_QueueMutex held.
void CUSBDeviceReal::CheckClose()
{
	if (m_WasClosed && m_IncompleteRequests.empty() && m_PendingRequests.empty())
	{
		delete this;
	}
}

void CUSBDeviceReal::ProcessPending()
{
	IUSBDevice::ProcessPending();
	CheckClose();
}

u32 CUSBDeviceReal::SetConfig(int Config)
{
	DEBUG_LOG(USBINTERFACE, "USBReal: set config %d", Config);
	for (int i = 0; i < m_NumInterfaces; i++)
	{
		libusb_release_interface(m_DeviceHandle, i);
	}
	int Ret = libusb_set_configuration(m_DeviceHandle, Config);
	if (Ret)
	{
		WARN_LOG(USBINTERFACE, "libusb_set_configuration failed with error: %d", Ret);
		return UsbErrDefault;
	}
	struct libusb_config_descriptor *ConfigDesc = NULL;
	Ret = libusb_get_config_descriptor_by_value(m_Device, Config, &ConfigDesc);
	if (Ret)
	{
		WARN_LOG(USBINTERFACE, "libusb_get_config_descriptor_by_value failed with error: %d", Ret);
		return UsbErrDefault;
	}
	m_NumInterfaces = ConfigDesc->bNumInterfaces;
	libusb_free_config_descriptor(ConfigDesc);
	for (int i = 0; i < m_NumInterfaces; i++)
	{
		Ret = libusb_kernel_driver_active(m_DeviceHandle, i);
		if (Ret == 1)
		{
			Ret = libusb_detach_kernel_driver(m_DeviceHandle, i);
			if (Ret && Ret != LIBUSB_ERROR_NOT_SUPPORTED)
			{
				WARN_LOG(USBINTERFACE, "libusb_detach_kernel_driver failed with error: %d", Ret);
			}
		}
		else if (Ret && Ret != LIBUSB_ERROR_NOT_SUPPORTED)
		{
			WARN_LOG(USBINTERFACE, "libusb_kernel_driver_active error ret = %d", Ret);
		}

		Ret = libusb_claim_interface(m_DeviceHandle, i);
		if (Ret)
		{
			WARN_LOG(USBINTERFACE, "libusb_claim_interface(%d) failed with error: %d", i, Ret);
			continue;
		}
	}
	return 0;
}

u32 CUSBDeviceReal::SetDefaultConfig()
{
	// Try the first config - if that doesn't work (Windows), at least get the
	// current one (SetConfig claims all the interfaces)
	struct libusb_config_descriptor *ConfigDesc = NULL;
	int UsbRet = libusb_get_config_descriptor(m_Device, 0, &ConfigDesc);
	if (UsbRet)
	{
		WARN_LOG(USBINTERFACE, "libusb_get_config_descriptor failed with error: %d", UsbRet);
		return UsbErrDefault;
	}
	u32 Ret = SetConfig(ConfigDesc->bConfigurationValue);
	libusb_free_config_descriptor(ConfigDesc);
	if (!Ret)
	{
		return 0;
	}

	int CurrentConfig;
	UsbRet = libusb_get_configuration(m_DeviceHandle, &CurrentConfig);
	if (UsbRet)
	{
		WARN_LOG(USBINTERFACE, "libusb_get_config_descriptor failed with error: %d", UsbRet);
		return UsbErrDefault;
	}
	return SetConfig(CurrentConfig);
}

u32 CUSBDeviceReal::SetInterfaceAltSetting(int Interface, int Setting)
{
	DEBUG_LOG(USBINTERFACE, "USBReal: set %d alt setting %d", Interface, Setting);
	int Ret = libusb_set_interface_alt_setting(m_DeviceHandle, Interface, Setting);
	if (Ret)
	{
		WARN_LOG(USBINTERFACE, "libusb_set_interface_alt_setting failed with error: %d", Ret);
		return UsbErrDefault;
	}
	return 0;
}

void CUSBDeviceReal::_ControlRequest(const USBSetup* Request, void* Payload, void* UserData)
{
	DEBUG_LOG(USBINTERFACE, "USBReal: control request");
	size_t Length = Common::swap16(Request->wLength);
	u8* Buf = new u8[sizeof(USBSetup) + Length];
	memcpy(Buf, Request, sizeof(USBSetup));
	memcpy(Buf + sizeof(USBSetup), Payload, Length);

	libusb_transfer* Transfer = libusb_alloc_transfer(0);
	CUSBRequest* URequest = new CUSBRequestReal(this, UserData, -1, Transfer, Payload);

	libusb_fill_control_transfer(Transfer, m_DeviceHandle, (unsigned char*) Buf, CUSBRequestReal::TransferCallback, URequest, 0);
	int Err = libusb_submit_transfer(Transfer);
	if (Err)
	{
		URequest->Complete(UsbErrDefault);
	}
}

void CUSBDeviceReal::BulkRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData)
{
	DEBUG_LOG(USBINTERFACE, "USBReal: bulk request");
	libusb_transfer* Transfer = libusb_alloc_transfer(0);
	CUSBRequest* URequest = new CUSBRequestReal(this, UserData, Endpoint, Transfer);
	libusb_fill_bulk_transfer(Transfer, m_DeviceHandle, Endpoint, (unsigned char*) Payload, Length, CUSBRequestReal::TransferCallback, URequest, 0);
	int Err = libusb_submit_transfer(Transfer);
	if (Err)
	{
		URequest->Complete(UsbErrDefault);
	}
}

void CUSBDeviceReal::InterruptRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData)
{
	DEBUG_LOG(USBINTERFACE, "USBReal: interrupt request");
	libusb_transfer* Transfer = libusb_alloc_transfer(0);
	CUSBRequest* Request = new CUSBRequestReal(this, UserData, Endpoint, Transfer);
	libusb_fill_interrupt_transfer(Transfer, m_DeviceHandle, Endpoint, (unsigned char*) Payload, Length, CUSBRequestReal::TransferCallback, Request, 0);
	int Err = libusb_submit_transfer(Transfer);
	if (Err)
	{
		Request->Complete(UsbErrDefault);
	}
}

void CUSBDeviceReal::IsochronousRequest(u8 Endpoint, size_t Length, size_t NumPackets, u16* PacketLengths, void* Payload, void* UserData)
{
	DEBUG_LOG(USBINTERFACE, "USBReal: isochronous request");
	libusb_transfer* Transfer = libusb_alloc_transfer(NumPackets);
	CUSBRequest* Request = new CUSBRequestReal(this, UserData, Endpoint, Transfer, PacketLengths);
	libusb_fill_iso_transfer(Transfer, m_DeviceHandle, Endpoint, (unsigned char*) Payload, Length, NumPackets, CUSBRequestReal::TransferCallback, Request, 0);
	for (size_t i = 0; i < NumPackets; i++)
	{
		Transfer->iso_packet_desc[i].length = PacketLengths[i];
	}
	int Err = libusb_submit_transfer(Transfer);
	if (Err)
	{
		Request->Complete(UsbErrDefault);
	}

}

CUSBControllerReal::CUSBControllerReal()
{
	if (libusb_init(&m_UsbContext))
	{
		PanicAlert("Couldn't initialize libusb");
		return;
	}
	libusb_set_debug(m_UsbContext, 2);
#ifdef CUSBDEVICE_SUPPORTS_HOTPLUG
	m_HotplugActive = false;
	m_HotplugTriggered = false;
	m_UseHotplug = libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG);
#endif
	m_ShouldDestroy = false;

	m_Thread = new std::thread(&USBThreadFunc, this);
}

CUSBControllerReal::~CUSBControllerReal()
{
	auto NullResults = new std::vector<USBDeviceDescriptorEtc>;
	SetDeviceList(NullResults, false);
#ifdef CUSBDEVICE_SUPPORTS_HOTPLUG
	if (m_HotplugActive)
	{
		libusb_hotplug_deregister_callback(m_UsbContext, m_HotplugHandle);
	}
#endif
	libusb_exit(m_UsbContext);
	m_Thread->detach();
	delete m_Thread;
}

static void TryGetName(libusb_device* Device, libusb_device_descriptor* Desc, std::string* Name)
{
	char IdBuf[32];
	sprintf(IdBuf, "%04x/%04x", Desc->idVendor, Desc->idProduct);
	*Name = IdBuf;

#ifdef __APPLE__
	// Not very useful now, but could be useful if device names ever become
	// part of the UI.  TODO: other platforms.

	CFMutableDictionaryRef Matching = IOServiceMatching(kIOUSBDeviceClassName);
    if (!Matching)
    {
		return;
	}

	s64 Address = libusb_get_device_address(Device);
    CFDictionarySetValue(Matching, CFSTR(kUSBDevicePropertyAddress), CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &Address));
	s32 Vid = Desc->idVendor;
	s32 Pid = Desc->idProduct;
    CFDictionarySetValue(Matching, CFSTR(kUSBVendorName), CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &Vid));
    CFDictionarySetValue(Matching, CFSTR(kUSBProductName), CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &Pid));
	io_service_t Service = IOServiceGetMatchingService(kIOMasterPortDefault, Matching);
	if (!Service)
	{
		return;
	}
	CFStringRef CFName = (CFStringRef) IORegistryEntryCreateCFProperty(Service, CFSTR(kUSBProductString), NULL, 0);
	if (!CFName)
	{
		return;
	}
	char NameBuf[256];
	if (!CFStringGetCString(CFName, NameBuf, sizeof(NameBuf), kCFStringEncodingUTF8))
	{
		return;
	}
	*Name += ": ";
	*Name += NameBuf;
#endif
}

void CUSBControllerReal::PollDevices(bool IsInitial)
{
	if (!g_ShouldScan) {
		return;
	}

	auto Results = new std::vector<USBDeviceDescriptorEtc>;
	libusb_device** List;
	ssize_t Count = libusb_get_device_list(m_UsbContext, &List);
	if (Count < 0)
	{
		DEBUG_LOG(USBINTERFACE, "libusb_get_device_list returned %zd", Count);
		return;
	}
	//|| (Count == m_OldCount && !memcmp(List, m_OldList, Count * sizeof(List))))
	for (ssize_t i = 0; i < Count; i++)
	{
		libusb_device* Device = List[i];

		std::vector<u8> DevBuffer;

		libusb_device_descriptor Desc;
		int Err = libusb_get_device_descriptor(Device, &Desc);
		if (Err)
		{
			WARN_LOG(USBINTERFACE, "libusb_get_device_descriptor failed with error: %d", Err);
			continue;
		}

		USBDeviceDescriptorEtc& WiiDevice = EmplaceBack(*Results);
		memcpy(&WiiDevice, &Desc, sizeof(USBDeviceDescriptor));
		WiiDevice.OpenInfo.first = this;
		WiiDevice.OpenInfo.second = Device;
		TryGetName(Device, &Desc, &WiiDevice.Name);

		for (u8 c = 0; c < Desc.bNumConfigurations; c++)
		{
			struct libusb_config_descriptor *Config = NULL;
			Err = libusb_get_config_descriptor(Device, c, &Config);

			if (Err)
			{
				WARN_LOG(USBINTERFACE, "libusb_get_config_descriptor(%d) failed with error: %d", c, Err);
				break;
			}

			USBConfigDescriptorEtc& WiiConfig = EmplaceBack(WiiDevice.Configs);
			memcpy(&WiiConfig, Config, sizeof(USBConfigDescriptor));
			if (Config->extra_length)
			{
				WiiConfig.Rest.resize(Config->extra_length);
				memcpy(&WiiConfig.Rest[0], Config->extra, Config->extra_length);
			}

			for (u8 ic = 0; ic < Config->bNumInterfaces; ic++)
			{
				const struct libusb_interface *interfaceContainer = &Config->interface[ic];
				for (int ia = 0; ia < interfaceContainer->num_altsetting; ia++)
				{
					const struct libusb_interface_descriptor *Interface = &interfaceContainer->altsetting[ia];
					if (Interface->bInterfaceClass == LIBUSB_CLASS_HID &&
						(Interface->bInterfaceProtocol == 1 || Interface->bInterfaceProtocol == 2))
					{
						// Mouse or keyboard.  Don't even try using this device, as it could mess up the host.
						Results->erase(Results->end() - 1);
						libusb_free_config_descriptor(Config);
						goto BadDevice;
					}


					USBInterfaceDescriptorEtc& WiiInterface = EmplaceBack(WiiConfig.Interfaces);
					memcpy(&WiiInterface, Interface, sizeof(USBInterfaceDescriptor));

					for (u8 ie = 0; ie < Interface->bNumEndpoints; ie++)
					{
						const struct libusb_endpoint_descriptor *Endpoint = &Interface->endpoint[ie];

						USBEndpointDescriptorEtc& WiiEndpoint = EmplaceBack(WiiInterface.Endpoints);
						memcpy(&WiiEndpoint, Endpoint, sizeof(USBEndpointDescriptor));
					}
				}
			}
			libusb_free_config_descriptor(Config);
		}

		// Apparently it is possible for libusb_get_config_descriptor to fail.
		WiiDevice.bNumConfigurations = WiiDevice.Configs.size();
		if (WiiDevice.bNumConfigurations == 0)
		{
			Results->erase(Results->end() - 1);
		}

		BadDevice:;
	}
	libusb_free_device_list(List, false);

	SetDeviceList(Results, IsInitial);
}

void CUSBControllerReal::USBThread()
{
	Common::SetCurrentThreadName("libusb thread");
	timeval Tv;
	Tv.tv_sec = 0;
	Tv.tv_usec = 300000;
	Common::Timer Timer;
	Timer.Start();
	while (1)
	{
		int Err = libusb_handle_events_timeout(m_UsbContext, &Tv);
		if (Err)
		{
			PanicAlert("libusb error %d", Err);
			return;
		}
		if (m_ShouldDestroy)
		{
			delete this;
			return;
		}
#ifdef CUSBDEVICE_SUPPORTS_HOTPLUG
		if (m_UseHotplug)
		{
			if (m_HotplugTriggered)
			{
				PollDevices(false);
				m_HotplugTriggered = false;
			}
		}
		else
#endif
		{
			if (Timer.GetTimeDifference() < 300)
			{
				continue;
			}
			Timer.Update();
			PollDevices(false);
		}
	}
}

void CUSBControllerReal::Destroy()
{
	m_ShouldDestroy = true;
}

#ifdef CUSBDEVICE_SUPPORTS_HOTPLUG
int CUSBControllerReal::HotplugCallback(libusb_context* Ctx, libusb_device* Device, libusb_hotplug_event Event, void* Data)
{
	CUSBControllerReal* Self = (CUSBControllerReal*) Data;
	Self->m_HotplugTriggered = true;
	return 0;
}
#endif

void CUSBControllerReal::UpdateShouldScan()
{
#ifdef CUSBDEVICE_SUPPORTS_HOTPLUG
	if (m_UseHotplug)
	{
		if (g_ShouldScan)
		{
			int Err = libusb_hotplug_register_callback(
				m_UsbContext,
				(libusb_hotplug_event) (LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
				LIBUSB_HOTPLUG_ENUMERATE,
				LIBUSB_HOTPLUG_MATCH_ANY,
				LIBUSB_HOTPLUG_MATCH_ANY,
				LIBUSB_HOTPLUG_MATCH_ANY,
				HotplugCallback,
				this,
				&m_HotplugHandle
			);
			if (Err)
			{
				PanicAlert("Couldn't ask libusb for hotplug events");
				return;
			}
			m_HotplugActive = true;
		}
		else
		{
			if (m_HotplugActive)

			{
				libusb_hotplug_deregister_callback(m_UsbContext, m_HotplugHandle);
				m_HotplugActive = false;
			}
		}
	}
#endif

	if (g_ShouldScan)
	{
		// Ensure it's ready immediately
		PollDevices(true);
	}
}

void CUSBControllerReal::DestroyDeviceList(std::vector<USBDeviceDescriptorEtc>* Old)
{
	for (auto itr = Old->begin(); itr != Old->end(); ++itr)
	{
		libusb_unref_device((libusb_device*) itr->OpenInfo.second);
	}
}

IUSBDevice* CUSBControllerReal::OpenDevice(TUSBDeviceOpenInfo OpenInfo, IUSBDeviceClient* Client)
{
	libusb_device* Device = (libusb_device*) OpenInfo.second;
	libusb_device_handle* Handle;
	int Ret = libusb_open(Device, &Handle);
	if (Ret)
	{
		WARN_LOG(USBINTERFACE, "USBReal: libusb open %p failed", Device);
		return NULL;
	}
	DEBUG_LOG(USBINTERFACE, "USBReal: open device %p -> handle %p", Device, Handle);

	CUSBDeviceReal* USBDevice = new CUSBDeviceReal(Device, OpenInfo, Handle, this, Client);
	if (USBDevice->SetDefaultConfig())
	{
		USBDevice->Close();
		return NULL;
	}
	return USBDevice;
}

} // interface
