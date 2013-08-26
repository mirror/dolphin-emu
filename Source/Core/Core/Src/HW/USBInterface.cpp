// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "USBInterface.h"
#include "Thread.h"
#include "CoreTiming.h"
#if defined(__LIBUSB__) || defined (_WIN32)
#include "USBReal.h"
#endif
#include <set>

namespace USBInterface
{

enum {
	USBEventRequestsCompleted,
	USBEventDevicesChanged
};

std::mutex g_QueueMutex;
std::mutex g_DeviceListMutex;
IntrusiveList<IUSBDevice> g_PendingDevices;
IUSBController* g_Controllers[NumControllerIds];
int g_InterfaceRefCount;
std::set<IUSBDeviceChangeClient*> g_DeviceChangeClients;
std::set<TUSBDeviceOpenInfo> g_OpenDevices;
std::map<IUSBController*, std::vector<USBDeviceDescriptorEtc>> g_DeviceLists;
bool g_NeedDevicesChangedEvent;
bool g_DidOneTimeSetup;
int g_USBInterfaceEvent;
volatile bool g_ShouldScan;

CUSBRequest::CUSBRequest(IUSBDevice* Device, void* UserData, s16 Endpoint, bool IsFake)
: m_Endpoint(Endpoint), m_IsFake(IsFake), m_Device(Device)
{
	memcpy(m_UserData, UserData, UsbUserDataSize);
	std::lock_guard<std::mutex> Guard(g_QueueMutex);
	ListInsert(&Device->m_IncompleteRequests);
}

void CUSBRequest::Complete(u32 Status)
{
	m_Status = Status;
	g_QueueMutex.lock();
	ListRemove();
	ListInsert(&m_Device->m_PendingRequests);
	if (!m_Device->m_Pending)
	{
		m_Device->ListInsert(&g_PendingDevices);
		m_Device->m_Pending = true;
		g_QueueMutex.unlock();
		CoreTiming::ScheduleEvent_Threadsafe_Immediate(g_USBInterfaceEvent, USBEventRequestsCompleted);
	}
	else
	{
		g_QueueMutex.unlock();
	}
}

std::vector<USBDeviceDescriptorEtc> IUSBController::SetDeviceList(std::vector<USBDeviceDescriptorEtc>&& List, bool IsInitial)
{
	std::lock_guard<std::mutex> Guard(g_DeviceListMutex);
	std::vector<USBDeviceDescriptorEtc>& Existing = g_DeviceLists[this];
	if (Existing == List)
	{
		// nothing to do
		std::vector<USBDeviceDescriptorEtc> Empty;
		return std::move(Empty);
	}
	else
	{
		Existing.swap(List);
		if (!IsInitial)
		{
			CoreTiming::ScheduleEvent_Threadsafe(0, g_USBInterfaceEvent, USBEventDevicesChanged);
			g_NeedDevicesChangedEvent = true;
		}
		return std::move(List);
	}
}

IUSBDevice::IUSBDevice(IUSBDeviceClient* Client, TUSBDeviceOpenInfo OpenInfo)
: m_Pending(false), m_Client(Client), m_OpenInfo(OpenInfo) {
	g_OpenDevices.insert(OpenInfo);
}

void IUSBDevice::CancelRequestsInList(IntrusiveList<CUSBRequest>& List, u8 Endpoint)
{
	auto itr = m_PendingRequests.begin(), end = m_PendingRequests.end();
	while (itr != end)
	{
		CUSBRequest* Request = &*itr;
		++itr;
		if (Request->m_Endpoint == Endpoint && !Request->m_IsFake)
		{
			CancelRequest(Request);
		}
	}

}

void IUSBDevice::CancelRequests(u8 Endpoint)
{
	std::lock_guard<std::mutex> Guard(g_QueueMutex);
	CancelRequestsInList(m_IncompleteRequests, Endpoint);
	CancelRequestsInList(m_PendingRequests, Endpoint);
}

void IUSBDevice::ProcessPending()
{
	auto itr = m_PendingRequests.begin(), end = m_PendingRequests.end();
	while (itr != end)
	{
		CUSBRequest* Request = &*itr;
		DEBUG_LOG(USBINTERFACE, "Request complete: %x status:%x", *(u32*) Request->m_UserData, Request->m_Status);
		m_Client->USBRequestComplete(Request->m_UserData, Request->m_Status);
		++itr;
		delete Request;
	}
	m_PendingRequests.Clear();
	m_Pending = false;
}

void IUSBDevice::ControlRequest(const USBSetup* Setup, void* Payload, void* UserData)
{
	switch (Setup->bRequest)
	{
	case 0x09: // SET_CONFIGURATION
	{
		int Config = Setup->wValue;
		u32 Result = SetConfig(Config);
		(new CUSBRequest(this, UserData, -1, true))->Complete(Result);
		break;
	}
	case 0x0b: // SET_INTERFACE
	{
		int Interface = Setup->wIndex;
		int Setting = Setup->wValue;
		u32 Result = SetInterfaceAltSetting(Interface, Setting);
		(new CUSBRequest(this, UserData, -1, true))->Complete(Result);
		break;
	}
	default:
		return _ControlRequest(Setup, Payload, UserData);
	}
}

void IUSBDevice::Close()
{
	_Close();
	g_OpenDevices.erase(m_OpenInfo);
}

static void USBInterfaceCallback(u64 UserData, int CyclesLate)
{
	switch (UserData)
	{
	case USBEventRequestsCompleted:
	{
		std::lock_guard<std::mutex> Guard(g_QueueMutex);
		for (auto itr = g_PendingDevices.begin(); itr != g_PendingDevices.end(); ++itr)
		{
			itr->ProcessPending();
		}
		g_PendingDevices.Clear();
		break;
	}
	case USBEventDevicesChanged:
	{
		if (g_ShouldScan)
		{
			std::vector<USBDeviceDescriptorEtc*> Devices = GetDeviceList();
			for (auto itr = g_DeviceChangeClients.begin(); itr != g_DeviceChangeClients.end(); )
			{
				IUSBDeviceChangeClient* Client = *itr;
				++itr;
				// This might cause the previous iterator to be invalidated.
				Client->USBDevicesChanged(Devices);
			}
			ReleaseDeviceList();
		}
		break;
	}
	}
}

static void ReadDeviceStateInList(PointerWrap& p, IUSBDeviceClient* Client)
{
	u32 Count = 0;
	p.Do(Count);
	for (u32 i = 0; i < Count; i++)
	{
		char UserData[UsbUserDataSize];
		p.DoArray(UserData, UsbUserDataSize);
		Client->USBRequestComplete(UserData, -1);
	}
}

static void WriteDeviceStateInList(PointerWrap& p, IntrusiveList<CUSBRequest>& List)
{
	u32 Count = List.size();
	p.Do(Count);
	for (auto itr = List.begin(); itr != List.end(); ++itr)
	{
		p.DoArray(itr->m_UserData, UsbUserDataSize);
	}
}

void ReadDeviceState(PointerWrap& p, IUSBDeviceClient* Client)
{
	ReadDeviceStateInList(p, Client);
	ReadDeviceStateInList(p, Client);
}

void IUSBDevice::WriteDeviceState(PointerWrap& p)
{
	WriteDeviceStateInList(p, m_IncompleteRequests);
	WriteDeviceStateInList(p, m_PendingRequests);
}

std::vector<USBDeviceDescriptorEtc*> GetDeviceList()
{
	g_DeviceListMutex.lock();
	std::vector<USBDeviceDescriptorEtc*> List;
	for (auto litr = g_DeviceLists.begin(); litr != g_DeviceLists.end(); ++litr)
	{
		std::vector<USBDeviceDescriptorEtc>& ControllerList = litr->second;
		for (auto itr = ControllerList.begin(); itr != ControllerList.end(); ++itr)
		{
			USBDeviceDescriptorEtc* Desc = &*itr;
			Desc->IsOpen = g_OpenDevices.find(Desc->OpenInfo) != g_OpenDevices.end();
			List.push_back(Desc);
		}
	}
	return std::move(List);
}

void ReleaseDeviceList()
{
	g_DeviceListMutex.unlock();
}

bool IsOpen(TUSBDeviceOpenInfo OpenInfo)
{
	return g_OpenDevices.find(OpenInfo) != g_OpenDevices.end();
}

void RefInterface()
{
	if (!g_DidOneTimeSetup)
	{
		g_USBInterfaceEvent = CoreTiming::RegisterEvent("USBInterface", USBInterfaceCallback);
		g_DidOneTimeSetup = true;
	}

	if (g_InterfaceRefCount++ == 0)
	{
		DEBUG_LOG(USBINTERFACE, "USB coming up");
#if defined(__LIBUSB__) || defined (_WIN32)
		g_Controllers[UsbRealControllerId] = new CUSBControllerReal();
#endif
		CoreTiming::ScheduleEvent_Threadsafe(0, g_USBInterfaceEvent, USBEventDevicesChanged);
	}
}

void ResetInterface()
{
	DEBUG_LOG(USBINTERFACE, "USB shutting down");
	// All devices should be closed by now.
	g_InterfaceRefCount = 0;
	for (int i = 0; i < NumControllerIds; i++)
	{
		if (g_Controllers[i])
		{
			g_Controllers[i]->Destroy();
			g_Controllers[i] = NULL;
		}
	}
}

static void SetShouldScan(bool ShouldScan)
{
	g_ShouldScan = ShouldScan;
	for (int i = 0; i < NumControllerIds; i++)
	{
		IUSBController* Controller = g_Controllers[i];
		if (Controller)
		{
			Controller->UpdateShouldScan();
		}
	}
}

void RegisterDeviceChangeClient(IUSBDeviceChangeClient* Client)
{
	DEBUG_LOG(USBINTERFACE, "RegisterDeviceChangeClient(%p)", Client);
	g_DeviceChangeClients.insert(Client);
	if (!g_ShouldScan)
	{
		SetShouldScan(true);
	}
}

void DeregisterDeviceChangeClient(IUSBDeviceChangeClient* Client)
{
	g_DeviceChangeClients.erase(Client);
	DEBUG_LOG(USBINTERFACE, "DeregisterDeviceChangeClient(%p), now %zu", Client, g_DeviceChangeClients.size());
	if (g_ShouldScan && g_DeviceChangeClients.empty())
	{
		SetShouldScan(false);
	}
}

std::pair<TUSBDeviceOpenInfo, IUSBDevice*> OpenVidPid(u16 Vid, u16 Pid, IUSBDeviceClient* Client)
{
	std::vector<USBDeviceDescriptorEtc*> List = GetDeviceList();
	for (auto itr = List.begin(); itr != List.end(); ++itr)
	{
		USBDeviceDescriptorEtc* Desc = *itr;
		if (Desc->idVendor == Vid && Desc->idProduct == Pid)
		{
			IUSBDevice* Device = OpenDevice(Desc->OpenInfo, Client);
			if (Device)
			{
				ReleaseDeviceList();
				return std::make_pair(Desc->OpenInfo, Device);
			}
		}
	}
	ReleaseDeviceList();
	std::pair<TUSBDeviceOpenInfo, IUSBDevice*> Result;
	Result.second = NULL;
	return Result;
}

IUSBDevice* OpenDevice(TUSBDeviceOpenInfo OpenInfo, IUSBDeviceClient* Client)
{
	return OpenInfo.first->OpenDevice(OpenInfo, Client);
}

} // namespace
