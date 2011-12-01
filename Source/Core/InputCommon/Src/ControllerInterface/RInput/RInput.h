// Copyright (C) 2011 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

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

static bool is_init = false;
static bool is_init_done = false;

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
			float x, y;
		} cursor;
	};

	class Button : public Input
	{
	public:
		std::string GetName() const;
		Button(u8 index, const int& button, Mouse *parent) : m_index(index), m_button(button), m_parent(parent) {}
		ControlState GetState() const;
	private:
		const u8 m_index;
		const int& m_button;
		Mouse* m_parent;
	};

	class Cursor : public Input
	{
	public:
		std::string GetName() const;
		bool IsDetectable() { return m_relative; }
		bool IsRelative() { return true; }
		Cursor(u8 index, const float& axis, const bool positive, const bool relative) : m_index(index), m_axis(axis), m_positive(positive), m_relative(relative) {}
		ControlState GetState() const;
	private:
		const u8 m_index;
		const float& m_axis;		
		const bool m_positive;
		const bool m_relative;
	};

	State m_state_in;
	int hid;
	char guid[512];
};

}
}

#endif