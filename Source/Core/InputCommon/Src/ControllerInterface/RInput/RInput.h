#ifndef _CIFACE_RINPUT_H_
#define _CIFACE_RINPUT_H_

#include "../../Core/Src/ConfigManager.h"
#include "../ControllerInterface.h"
#include <windows.h>

namespace ciface
{
namespace RInput
{

#define MAX_IN 20*2		// 0-19 = mouse, 20-39 = joystick
#define MAX_RAW_MOUSE_BUTTONS 5
#define MAX_OUT 4		// 0-3 = wiimotes

#define RAW_SYS_MOUSE 0      // The sys mouse combines all usb mice into one
#define MAX_MOUSE_NAME 1024

static bool rawinput_active = 0;

void Init(std::vector<ControllerInterface::Device*>& devices, HWND hwnd);
void SetHWND(HWND _hwnd);
void DetectDevice(int d);

class Mouse : public ControllerInterface::Device
{
public:
	Mouse(int, char guid[512]);
	~Mouse();

	bool UpdateInput();
	bool UpdateOutput() { return false; };

	std::string GetName() const;
	int GetId() const;
	std::string GetSource() const;

	void DetectDevice();

private:
	struct State
	{
		int button[MAX_RAW_MOUSE_BUTTONS];
		struct
		{
			float x, y, x_d, y_d;
		} cursor;
	};

	class Button : public Input
	{
	public:
		std::string GetName() const;
		Button(u8 index, const int& button, Mouse *parent) : m_index(index), m_button(button), m_parent(parent) {}
		ControlState GetState(bool relative = false) const;
	private:
		const u8 m_index;
		const int& m_button;
		Mouse* m_parent;
	};

	class Cursor : public Input
	{
	public:
		std::string GetName() const;
		bool IsDetectable() { return true; }
		Cursor(u8 index, const float& axis, const float& axis_d, const bool positive) : m_index(index), m_axis(axis), m_axis_d(axis_d), m_positive(positive)  {}
		ControlState GetState(bool relative = false) const;
	private:
		const u8 m_index;
		const float& m_axis;
		const float& m_axis_d;
		const bool m_positive;
	};

	State m_state_in;
	int mouse_x; int mouse_y;	
	int last_mouse_x; int last_mouse_y;
	int mouse_x_d; int mouse_y_d;
	int hid;
	char guid[512];
};

}
}

#endif