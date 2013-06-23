#include "../ControllerInterface.h"

#ifdef CIFACE_USE_DINPUT

#include "DInput.h"

#include "StringUtil.h"

#ifdef CIFACE_USE_DINPUT_JOYSTICK
	#include "DInputJoystick.h"
#endif
#ifdef CIFACE_USE_DINPUT_KBM
	#include "DInputKeyboardMouse.h"
#endif

#pragma comment(lib, "Dinput8.lib")
#pragma comment(lib, "dxguid.lib")

namespace ciface
{
namespace DInput
{

bool is_init = false, is_init_done = false;
HWND hwnd = NULL;

//BOOL CALLBACK DIEnumEffectsCallback(LPCDIEFFECTINFO pdei, LPVOID pvRef)
//{
//	((std::list<DIEFFECTINFO>*)pvRef)->push_back(*pdei);
//	return DIENUM_CONTINUE;
//}

BOOL CALLBACK DIEnumDeviceObjectsCallback(LPCDIDEVICEOBJECTINSTANCE lpddoi, LPVOID pvRef)
{
	((std::list<DIDEVICEOBJECTINSTANCE>*)pvRef)->push_back(*lpddoi);
	return DIENUM_CONTINUE;
}

BOOL CALLBACK DIEnumDevicesCallback(LPCDIDEVICEINSTANCE lpddi, LPVOID pvRef)
{
	((std::list<DIDEVICEINSTANCE>*)pvRef)->push_back(*lpddi);
	return DIENUM_CONTINUE;
}

std::string GetDeviceName(const LPDIRECTINPUTDEVICE8 device)
{
	DIPROPSTRING str = {};
	str.diph.dwSize = sizeof(str);
	str.diph.dwHeaderSize = sizeof(str.diph);
	str.diph.dwHow = DIPH_DEVICE;

	std::string result;
	if (SUCCEEDED(device->GetProperty(DIPROP_PRODUCTNAME, &str.diph)))
	{
		result = StripSpaces(UTF16ToUTF8(str.wsz));
	}

	return result;
}

void Init(std::vector<ControllerInterface::Device*>& devices, HWND _hwnd)
{
	if (is_init) return;
	is_init = true;

	hwnd = _hwnd;

	IDirectInput8* idi8;
	if (FAILED(DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8, (LPVOID*)&idi8, NULL)))
	{
		is_init = false;
		return;
	}

#ifdef CIFACE_USE_DINPUT_KBM
	InitKeyboardMouse(idi8, devices);
#endif
#ifdef CIFACE_USE_DINPUT_JOYSTICK
	InitJoystick(idi8, devices);
#endif

	idi8->Release();
	is_init_done = true;
}

void Shutdown()
{
	is_init = false; is_init_done = false;
}

void SetHWND(HWND _hwnd)
{
	hwnd = _hwnd;
}

}
}

#endif
