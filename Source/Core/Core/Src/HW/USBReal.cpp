// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "../Core.h"
#include "USBReal.h"
#include "CoreTiming.h"

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
	virtual ~CUSBRequestReal()
	{
		libusb_free_transfer(m_Transfer);
	}
	virtual void Complete(u32 Status)
	{
		CUSBRequest::Complete(Status);
		((CUSBDeviceReal*) m_Device)->CheckClose();
	}
	libusb_transfer* m_Transfer;
	void* m_UserPayload;
	bool m_WasCancelled;
};

CUSBDeviceReal::CUSBDeviceReal(libusb_device* Device, TUSBDeviceOpenInfo OpenInfo, libusb_device_handle* Handle, CUSBControllerReal* Controller, IUSBDeviceClient* Client)
: IUSBDevice(Client, OpenInfo),
m_Device(Device), m_DeviceHandle(Handle)
{
	m_NumInterfaces = 0;
}

void CUSBDeviceReal::_Close()
{
	DEBUG_LOG(USBINTERFACE, "USBReal: closing");
	// We might have to wait for outstanding requests.
	m_WasClosed = true;
	CheckClose();
}

void CUSBDeviceReal::CheckClose()
{
	if (m_WasClosed && m_IncompleteRequests.empty())
	{
		for (int i = 0; i < m_NumInterfaces; i++)
		{
			libusb_release_interface(m_DeviceHandle, i);
			libusb_attach_kernel_driver(m_DeviceHandle, i);
		}
		libusb_close(m_DeviceHandle);
	}
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
		return -1000;
	}
	struct libusb_config_descriptor *ConfigDesc = NULL;
	Ret = libusb_get_config_descriptor(m_Device, Config, &ConfigDesc);
	if (Ret)
	{
		WARN_LOG(USBINTERFACE, "libusb_get_config_descriptor failed with error: %d", Ret);
		return -1000;
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

u32 CUSBDeviceReal::SetInterfaceAltSetting(int Interface, int Setting)
{
	DEBUG_LOG(USBINTERFACE, "USBReal: set %d alt setting %d", Interface, Setting);
	int Ret = libusb_set_interface_alt_setting(m_DeviceHandle, Interface, Setting);
	if (Ret)
	{
		WARN_LOG(USBINTERFACE, "libusb_set_interface_alt_setting failed with error: %d", Ret);
		return -1000;
	}
	return 0;
}

void CUSBDeviceReal::CancelRequest(CUSBRequest* BaseRequest)
{
	CUSBRequestReal* Request = (CUSBRequestReal*) BaseRequest;
	DEBUG_LOG(USBINTERFACE, "USBReal: cancel request %p", Request);
	Request->m_WasCancelled = true;
	libusb_cancel_transfer(Request->m_Transfer);
}

void CUSBDeviceReal::TransferCallback(libusb_transfer* Transfer)
{
	CUSBRequestReal* Request = (CUSBRequestReal*) Transfer->user_data;
	DEBUG_LOG(USBINTERFACE, "USBReal: transfer callback Request=%p status=%d", Request, Transfer->status);
	if (Transfer->type == LIBUSB_TRANSFER_TYPE_CONTROL)
	{
		memcpy(Request->m_UserPayload, Transfer->buffer + sizeof(USBSetup), Transfer->length - LIBUSB_CONTROL_SETUP_SIZE);
		delete[] Transfer->buffer;
	}

	if (Request->m_WasCancelled)
	{
		Request->Complete(0);
	}

	if (Transfer->status == LIBUSB_TRANSFER_COMPLETED)
	{
		if (Transfer->type == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS)
		{
			u16* PacketLengths = (u16*) Request->m_UserPayload;
			for (int i = 0; i < Transfer->num_iso_packets; i++)
			{
				libusb_iso_packet_descriptor* Desc = &Transfer->iso_packet_desc[i];
				PacketLengths[i] = Desc->status < 0 ? 0 : Desc->actual_length;
			}
		}
		Request->Complete(Transfer->actual_length);
	}
	else
	{
		Request->Complete(-7022);
	}
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

	libusb_fill_control_transfer(Transfer, m_DeviceHandle, (unsigned char*) Buf, CUSBDeviceReal::TransferCallback, URequest, 0);
	int Err = libusb_submit_transfer(Transfer);
	if (Err)
	{
		URequest->Complete(-7022);
	}
}

void CUSBDeviceReal::BulkRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData)
{
	DEBUG_LOG(USBINTERFACE, "USBReal: bulk request");
	libusb_transfer* Transfer = libusb_alloc_transfer(0);
	CUSBRequest* URequest = new CUSBRequestReal(this, UserData, Endpoint, Transfer);
	libusb_fill_bulk_transfer(Transfer, m_DeviceHandle, Endpoint, (unsigned char*) Payload, Length, TransferCallback, URequest, 0);
	int Err = libusb_submit_transfer(Transfer);
	if (Err)
	{
		URequest->Complete(-7022);
	}
}

void CUSBDeviceReal::InterruptRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData)
{
	DEBUG_LOG(USBINTERFACE, "USBReal: interrupt request");
	libusb_transfer* Transfer = libusb_alloc_transfer(0);
	CUSBRequest* Request = new CUSBRequestReal(this, UserData, Endpoint, Transfer);
	libusb_fill_interrupt_transfer(Transfer, m_DeviceHandle, Endpoint, (unsigned char*) Payload, Length, TransferCallback, Request, 0);
	int Err = libusb_submit_transfer(Transfer);
	if (Err)
	{
		Request->Complete(-7022);
	}
}

void CUSBDeviceReal::IsochronousRequest(u8 Endpoint, size_t Length, size_t NumPackets, u16* PacketLengths, void* Payload, void* UserData)
{
	DEBUG_LOG(USBINTERFACE, "USBReal: isochronous request");
	libusb_transfer* Transfer = libusb_alloc_transfer(NumPackets);
	CUSBRequest* Request = new CUSBRequestReal(this, UserData, Endpoint, Transfer, PacketLengths);
	libusb_fill_iso_transfer(Transfer, m_DeviceHandle, Endpoint, (unsigned char*) Payload, Length, NumPackets, TransferCallback, Request, 0);
	for (size_t i = 0; i < NumPackets; i++)
	{
		Transfer->iso_packet_desc[i].length = PacketLengths[i];
	}
	int Err = libusb_submit_transfer(Transfer);
	if (Err)
	{
		Request->Complete(-1002);
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
	m_HotplugHandle = NULL;
	m_UseHotplug = libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG);
#endif
	m_ShouldDestroy = false;

	m_Thread = new std::thread(&USBThreadFunc, this);
}

CUSBControllerReal::~CUSBControllerReal()
{
	std::vector<USBDeviceDescriptorEtc> NullResults;
	std::vector<USBDeviceDescriptorEtc> Old = SetDeviceList(std::move(NullResults), false);
	for (auto itr = Old.begin(); itr != Old.end(); ++itr)
	{
		libusb_unref_device((libusb_device*) itr->OpenInfo.second);
	}
#ifdef CUSBDEVICE_SUPPORTS_HOTPLUG
	if (m_HotplugHandle != NULL)
	{
		libusb_hotplug_deregister_callback(m_UsbContext, m_HotplugHandle);
	}
#endif
	libusb_exit(m_UsbContext);
	m_Thread->detach();
	delete m_Thread;
}

void CUSBControllerReal::PollDevices(bool IsInitial)
{
	if (!g_ShouldScan) {
		return;
	}

	std::vector<USBDeviceDescriptorEtc> Results;
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

		USBDeviceDescriptorEtc& WiiDevice = EmplaceBack(Results);
		memcpy(&WiiDevice, &Desc, sizeof(USBDeviceDescriptor));
		WiiDevice.OpenInfo.first = this;
		WiiDevice.OpenInfo.second = Device;

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
			WiiConfig.Rest.resize(Config->extra_length);
			memcpy(&WiiConfig.Rest[0], Config->extra, Config->extra_length);

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
						Results.erase(Results.end() - 1);
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
			Results.erase(Results.end() - 1);
		}

		BadDevice:;
	}
	libusb_free_device_list(List, false);

	std::vector<USBDeviceDescriptorEtc> Old = SetDeviceList(std::move(Results), IsInitial);
	for (auto itr = Old.begin(); itr != Old.end(); ++itr)
	{
		libusb_unref_device((libusb_device*) itr->OpenInfo.second);
	}
}

void CUSBControllerReal::USBThread()
{
	timeval Tv;
	Tv.tv_sec = 0;
	Tv.tv_usec = 300000;
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
		if (!m_UseHotplug)
		{
			PollDevices(false);
		}
#else
		PollDevices(false);
#endif
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
	Self->PollDevices(false);
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
				LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
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
		}
		else
		{
			if (m_HotplugHandle != NULL)

			{
				libusb_hotplug_deregister_callback(m_UsbContext, m_HotplugHandle);
				m_HotplugHandle = NULL;
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

IUSBDevice* CUSBControllerReal::OpenDevice(TUSBDeviceOpenInfo OpenInfo, IUSBDeviceClient* Client)
{
	libusb_device* Device = (libusb_device*) OpenInfo.second;
	DEBUG_LOG(USBINTERFACE, "USBReal: open device %p", Device);
	libusb_device_handle* Handle;
	int Ret = libusb_open(Device, &Handle);
	if (Ret)
	{
		WARN_LOG(USBINTERFACE, "USBReal: libusb open %p failed", Device);
		return NULL;
	}

	CUSBDeviceReal* USBDevice = new CUSBDeviceReal(Device, OpenInfo, Handle, this, Client);
	if (USBDevice->SetConfig(0))
	{
		USBDevice->Close();
		return NULL;
	}
	return USBDevice;
}

} // interface
