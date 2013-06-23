// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _CIFACE_RINPUT_H_
#define _CIFACE_RINPUT_H_

#include "../../Core/Src/ConfigManager.h"
#include "../ControllerInterface.h"
#include <windows.h>

namespace ciface
{
namespace RInput
{

#define MAX_RAW_MOUSE_BUTTONS 5
#define MAX_RAW_MOUSE_AXES 3

static bool is_init = false;
static bool is_init_done = false;

void Init(std::vector<ControllerInterface::Device*>& devices, HWND hwnd);
void Shutdown();
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

private:
	struct State
	{
		int button[MAX_RAW_MOUSE_BUTTONS];
		struct
		{
			LONG x, y, z;
		} axis;
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

	class Axis : public Input
	{
	public:
		std::string GetName() const;
		bool IsRelative() { return true; }
		Axis(u8 index, LONG& axis, LONG range) : m_index(index), m_axis(axis), m_range(range) {}
		ControlState GetState() const;
	private:
		LONG& m_axis;
		const LONG m_range;
		const u8 m_index;
	};

	State m_state_in;
	int hid;
	char guid[512];
};


// Copyright 2006 Jake Stookey

#define RAW_SYS_MOUSE 0
#define MAX_MOUSE_NAME 1024

typedef struct STRUCT_RAW_MOUSE {
	// Identifier for the mouse.  WM_INPUT passes the device HANDLE as lparam when registering a mousemove
	HANDLE device_handle;

	// The running tally of mouse moves received from WM_INPUT (mouse delta).
	//    Calling get_raw_mouse_[x | y] will reset the value so that every time
	//    get_raw_mouse_[x | y] is called, the relative value from the last time
	//    get_raw_mouse_[x | y] was called will be returned.
	ULONG x;
	ULONG y;
	ULONG z;

	// Used to determine if the HID is using absolute mode or relative mode
	BOOL is_absolute;
	// This indicates if the coordinates are coming from a multi-monitor setup
	BOOL is_virtual_desktop;

	int buttonpressed[MAX_RAW_MOUSE_BUTTONS];

	// Identifying the name of the button may be useful in the future as a way to
	//   use a mousewheel as a button and other neat tricks (button name: "wheel up", "wheel down")
	//   -- not a bad way to look at it for a rotary joystick
	char *button_name[MAX_RAW_MOUSE_BUTTONS];
	char *name[MAX_MOUSE_NAME];

} RAW_MOUSE, *PRAW_MOUSE;

extern BOOL bHasBeenInitialized;

BOOL is_rm_rdp_mouse(char cDeviceString[]);

BOOL read_raw_input(PRAWINPUT);

// register to reviece WM_INPUT
BOOL register_raw_mouse(void);

// Number of mice stored in pRawMice array
int raw_mouse_count();

BOOL init_raw_mouse(BOOL, BOOL, BOOL);
BOOL register_raw_mouse(HWND hwnd);

// Free up the memory allocated for the raw mouse array
void destroy_raw_mouse(void);
BOOL unregister_raw_mouse(HWND hwnd);

// Every time the WM_INPUT message is received, the lparam must be passed to this function to keep a running tally of
//     every mouse move to maintain accurate results for get_raw_mouse_x_delta() & get_raw_mouse_y_delta().
BOOL add_to_raw_mouse_x_and_y(HANDLE); // device handle, x val, y val

// Fetch the relative position of the mouse since the last time get_raw_mouse_x_delta() or get_raw_mouse_y_delta
//    was called
ULONG get_raw_mouse_x_delta(int);
ULONG get_raw_mouse_y_delta(int);
ULONG get_raw_mouse_z_delta(int);

// pass the mousenumber, button number, returns 0 if the button is up, 1 if the button is down
BOOL is_raw_mouse_button_pressed(int, int);
char *get_raw_mouse_button_name(int, int);

// get GUID name
char *get_raw_mouse_name(int);

// Used to determine if the HID is using absolute mode or relative mode
// NOTE: this value isn't updated until the device registers a WM_INPUT message
BOOL is_raw_mouse_absolute(int);

// This indicates if the coordinates are coming from a multi-monitor setup
// NOTE: this value isn't updated until the device registers a WM_INPUT message
BOOL is_raw_mouse_virtual_desktop(int);

void reset_raw_mouse_data(int);

}
}

#endif