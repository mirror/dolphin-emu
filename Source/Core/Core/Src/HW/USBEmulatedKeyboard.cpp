// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "USBEmulatedKeyboard.h"
namespace USBInterface
{

CUSBDeviceEmulatedKeyboard::CUSBDeviceEmulatedKeyboard(TUSBDeviceOpenInfo OpenInfo, IUSBDeviceClient* Client, IUSBControllerEmulatedBase* Controller)
: IUSBDeviceEmulated(OpenInfo, Client, Controller)
{
    m_PendingStateChange = true;
    m_PendingInterruptRequest = NULL;
}

CUSBDeviceEmulatedKeyboard::~CUSBDeviceEmulatedKeyboard()
{
    std::lock_guard<std::mutex> Guard(s_KeyboardClientMutex);
    s_KeyboardClientInstance = NULL;
    if (m_PendingInterruptRequest)
    {
        // Don't bother completing it.
        delete m_PendingInterruptRequest;
    }
}

bool CUSBDeviceEmulatedKeyboard::ControlRequest(const USBSetup* Setup, void* Payload, void* UserData)
{
    if (IUSBDevice::ControlRequest(Setup, Payload, UserData))
    {
        return true;
    }
    if (Setup->bmRequestType == 0x21)
    {
        if ((Setup->bRequest == 0x0a /* SET_IDLE */ &&
             Setup->wValue == 0 /* wait forever */) ||
            (Setup->bRequest == 0x0b /* SET_PROTOCOL */ &&
             Setup->wValue == 0 /* boot protocol */) ||
            Setup->bRequest == 0x09 /* SET_REPORT - i.e. LEDs */)
        {
            // sure, if you want
            (new CUSBRequest(this, UserData, -1))->Complete(0);
            return true;
        }
    }

    WARN_LOG(USBINTERFACE, "USBEmulatedKeyboard: unknown control request: bmRequestType=%x bRequest=%x wIndex=%x wValue=%x", Setup->bmRequestType, Setup->bRequest, Setup->wIndex, Setup->wValue);
    return true;
}

void CUSBDeviceEmulatedKeyboard::InterruptRequest(u8 Endpoint, size_t Length, void* Payload, void* UserData)
{
    DEBUG_LOG(USBINTERFACE, "USBEmulatedKeyboard: interrupt request");
    if (Length != 8)
    {
        WARN_LOG(USBINTERFACE, "USBEmulatedKeyboard: length != 8");
        return;
    }

    std::lock_guard<std::mutex> Guard(s_KeyboardClientMutex);
    m_PendingInterruptRequest = new CUSBRequest(this, UserData, Endpoint);
    m_PendingPayload = Payload;
    if (m_PendingStateChange)
    {
        m_PendingStateChange = false;
        UpdateReport(s_Report.Data);
    }
}

// called with s_KeyboardClientMutex locked
void CUSBDeviceEmulatedKeyboard::UpdateReport(u8* Data)
{
    DEBUG_LOG(USBINTERFACE, "USBEmulatedKeyboard: Update Report: mod=%02x keys=%02x %02x %02x %02x %02x %02x mpir=%p", Data[0], Data[2], Data[3], Data[4], Data[5], Data[6], Data[7], m_PendingInterruptRequest);
    if (m_PendingInterruptRequest)
    {
        memcpy(m_PendingPayload, Data, 8);
        m_PendingInterruptRequest->Complete(8);
        m_PendingInterruptRequest = NULL;
    }
    else
    {
        m_PendingStateChange = true;
    }
}

USBDeviceDescriptorEtc CUSBControllerEmulatedKeyboard::GetDeviceDescriptor()
{
    USBDeviceDescriptorEtc Device;
    Device.bcdUSB = 0x100;
    Device.bDeviceClass = 0;
    Device.bDeviceSubClass = 0;
    Device.bDeviceProtocol = 0;
    Device.bMaxPacketSize0 = 8;
    Device.idVendor = 0xffff;
    Device.idProduct = 0xffff;
    Device.bcdDevice = 0;
    Device.iManufacturer = 0;
    Device.iProduct = 0;

    Device.Name = "Emulated USB Keyboard";

    USBConfigDescriptorEtc& Config = EmplaceBack(Device.Configs);
    Config.bConfigurationValue = 1;
    Config.iConfiguration = 0;
    Config.bmAttributes = 0;
    Config.MaxPower = 1;

    // Software doesn't care about the HID descriptor at all.

    USBInterfaceDescriptorEtc& Interface = EmplaceBack(Config.Interfaces);
    Interface.bInterfaceNumber = 0;
    Interface.bAlternateSetting = 0;
    Interface.bNumEndpoints = 1;
    Interface.bInterfaceClass = 3; // HID
    Interface.bInterfaceSubClass = 1; // Boot Keyboard
    Interface.bInterfaceProtocol = 1; // Keyboard
    Interface.iInterface = 0;

    USBEndpointDescriptorEtc& Endpoint = EmplaceBack(Interface.Endpoints);
    Endpoint.bEndpointAddress = 0x81;
    Endpoint.bmAttributes = 0x03;
    Endpoint.wMaxPacketSize = 8;
    Endpoint.bInterval = 0x0a;

    Endpoint.Fixup();
    Interface.Fixup();
    Config.Fixup();
    Device.Fixup();

    return std::move(Device);
}

}

