#ifndef _CIFACE_X11_XINPUT2_H_
#define _CIFACE_X11_XINPUT2_H_

#include "../../Device.h"

extern "C" {
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>
}

namespace ciface
{
namespace Xinput2
{

void Init(std::vector<Core::Device*>& devices, void* const hwnd);

class KeyboardMouse : public Core::Device
{

private:
	struct State
	{
		char keyboard[32];
		unsigned int buttons;
		struct
		{
			float x, y;
		} cursor, axis;
	};
	
	class Key : public Input
	{
		friend class KeyboardMouse;
	public:
		std::string GetName() const;
		Key(Display* display, KeyCode keycode, const char* keyboard);
		ControlState GetState() const;
		
	private:
		std::string	m_keyname;
		Display* const	m_display;
		const char* const m_keyboard;
		const KeyCode	m_keycode;
	};
	
	class Button : public Input
	{
	public:
		std::string GetName() const;
		Button(unsigned int index, unsigned int& buttons)
			: m_buttons(buttons), m_index(index) {}
		ControlState GetState() const;
		
	private:
		const unsigned int& m_buttons;
		const unsigned int m_index;
	};

	class Cursor : public Input
	{
	public:
		std::string GetName() const;
		bool IsDetectable() { return false; }
		Cursor(u8 index, bool positive, const float& cursor)
			: m_cursor(cursor), m_index(index), m_positive(positive) {}
		ControlState GetState() const;

	private:
		const float& m_cursor;
		const u8 m_index;
		const bool m_positive;
	};
	
	class Axis : public Input
	{
	public:
		std::string GetName() const;
		bool IsDetectable() { return false; }
		Axis(u8 index, bool positive, float& axis)
			: m_axis(axis), m_index(index), m_positive(positive) {}
		ControlState GetState() const;

	private:
		float& m_axis; // deliberately not const
		const u8 m_index;
		const bool m_positive;
	};
	
private:
	void SelectEventsForDevice (Window window, XIEventMask *mask, int deviceid);
	void UpdateCursor ();
	
public:
	bool UpdateInput();
	bool UpdateOutput();
	
	KeyboardMouse(Window window, int opcode, int pointer_deviceid, int keyboard_deviceid);
	~KeyboardMouse();
	
	std::string GetName() const;
	std::string GetSource() const;
	int GetId() const;
	
private:
	Window m_window;
	Display* m_display;
	State m_state;
	int				xi_opcode;
	const int		pointer_deviceid, keyboard_deviceid;
	std::string		name;
};

}
}

#endif
