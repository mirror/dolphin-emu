#ifndef _CIFACE_DINPUT_KBM_H_
#define _CIFACE_DINPUT_KBM_H_

#include "../ControllerInterface.h"

#define DIRECTINPUT_VERSION 0x0800
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dinput.h>

namespace ciface
{
namespace DInput
{

void InitKeyboardMouse(IDirectInput8* const idi8, std::vector<ControllerInterface::Device*>& devices);

class KeyboardMouse : public ControllerInterface::Device
{
private:
	struct State
	{
		BYTE			keyboard[256];
		DIMOUSESTATE2	mouse;
		struct
		{
			float x, y;
		} cursor;
	};

	class Key : public Input
	{
	public:
		std::string GetName() const;
		Key(u8 index, const BYTE& key) : m_index(index), m_key(key) {}
		ControlState GetState() const;
	private:
		const BYTE& m_key;
		const u8 m_index;
	};

	class Button : public Input
	{
	public:
		std::string GetName() const;
		Button(u8 index, const BYTE& button) : m_index(index), m_button(button) {}
		ControlState GetState() const;
	private:
		const BYTE& m_button;
		const u8 m_index;
	};

	class Axis : public Input
	{
	public:
		std::string GetName() const;
		Axis(u8 index, const LONG& axis, LONG range) : m_index(index), m_axis(axis), m_range(range) {}
		ControlState GetState() const;
	private:
		const LONG& m_axis;
		const LONG m_range;
		const u8 m_index;
	};

	class Cursor : public Input
	{
	public:
		std::string GetName() const;
		bool IsDetectable() { return false; }
		Cursor(u8 index, const float& axis, const bool positive) : m_index(index), m_axis(axis), m_positive(positive) {}
		ControlState GetState() const;
	private:
		const float& m_axis;
		const u8 m_index;
		const bool m_positive;
	};

	class Light : public Output
	{
	public:
		std::string GetName() const;
		Light(u8 index) : m_index(index) {}
		void SetState(ControlState state);
	private:
		const u8 m_index;
	};

public:
	bool UpdateInput();
	bool UpdateOutput();

	KeyboardMouse(const LPDIRECTINPUTDEVICE8 kb_device, const LPDIRECTINPUTDEVICE8 mo_device);
	~KeyboardMouse();

	std::string GetName() const;
	int GetId() const;
	std::string GetSource() const;

private:
	const LPDIRECTINPUTDEVICE8	m_kb_device;
	const LPDIRECTINPUTDEVICE8	m_mo_device;

	DWORD				m_last_update;
	State				m_state_in;
	unsigned char		m_state_out[3];	// NUM CAPS SCROLL
	bool				m_current_state_out[3];	// NUM CAPS SCROLL
};

}
}

#endif
