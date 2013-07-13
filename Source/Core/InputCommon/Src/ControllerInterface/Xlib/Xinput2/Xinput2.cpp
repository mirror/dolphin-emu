#include "Xinput2.h"
#include <X11/XKBlib.h>
#include <cmath>

#define MOUSE_AXIS_SENSITIVITY		8

namespace ciface
{
namespace Xinput2
{

void Init(std::vector<Core::Device*>& devices, void* const hwnd)
{
	Display *dpy;
	
	dpy = XOpenDisplay (NULL);
	
	// xi_opcode is important; it will be used to identify XInput events by 
	// the polling loop in UpdateInput.
	int xi_opcode, event, error;
	
	// verify that the XInput extension is available
	if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event, &error))
		return;
	
	// verify that the XInput extension is at at least version 2.0
	int major = 2, minor = 0;
	
	if (XIQueryVersion (dpy, &major, &minor) != Success)
		return;
	
	// now we can start finding master devices and registering them with
	// Dolphin
	
	XIDeviceInfo	*all_masters, *current_master;
	int				num_masters;
	
	all_masters = XIQueryDevice (dpy, XIAllMasterDevices, &num_masters);
	
	for (int i = 0; i < num_masters; i++)
	{
		current_master = &all_masters[i];
		if (current_master->use == XIMasterPointer)
			devices.push_back(new KeyboardMouse((Window)hwnd, xi_opcode, current_master->deviceid, current_master->attachment));
	}
	
	XCloseDisplay (dpy);
	
	XIFreeDeviceInfo (all_masters);
}

// Apply the event mask to the device and all its slaves.
void KeyboardMouse::SelectEventsForDevice (Window window, XIEventMask *mask, int deviceid)
{
	mask->deviceid = deviceid;
	XISelectEvents (m_display, window, mask, 1);
	
	XIDeviceInfo	*all_slaves, *current_slave;
	int				num_slaves;
	
	all_slaves = XIQueryDevice (m_display, XIAllDevices, &num_slaves);
	
	for (int i = 0; i < num_slaves; i++)
	{
		current_slave = &all_slaves[i];
		if ((current_slave->use != XISlavePointer && current_slave->use != XISlaveKeyboard) || current_slave->attachment != deviceid)
			continue;
		mask->deviceid = current_slave->deviceid;
		XISelectEvents (m_display, window, mask, 1);
	}
	
	XIFreeDeviceInfo (all_slaves);
}

KeyboardMouse::KeyboardMouse(Window window, int opcode, int pointer, int keyboard) 
	: m_window(window), xi_opcode(opcode), pointer_deviceid(pointer), keyboard_deviceid(keyboard)
{
	memset(&m_state, 0, sizeof(m_state));
	
	m_display = XOpenDisplay (NULL);
	
	int min_keycode, max_keycode;
	XDisplayKeycodes(m_display, &min_keycode, &max_keycode);
	
	int unused; // should always be 1
	XIDeviceInfo *pointer_device = XIQueryDevice (m_display, pointer_deviceid, &unused);
	name = std::string (pointer_device->name);
	XIFreeDeviceInfo (pointer_device);
	
	XIEventMask		mask;
	unsigned char	mask_buf[(XI_LASTEVENT + 7)/8];
	
	mask.mask_len = sizeof(mask_buf);
	mask.mask = mask_buf;
	memset (mask_buf, 0, sizeof(mask_buf));

	XISetMask (mask_buf, XI_ButtonPress);
	XISetMask (mask_buf, XI_ButtonRelease);
	XISetMask (mask_buf, XI_RawMotion);
	XISetMask (mask_buf, XI_KeyPress);
	XISetMask (mask_buf, XI_KeyRelease);

	SelectEventsForDevice (DefaultRootWindow(m_display), &mask, pointer_deviceid);
	SelectEventsForDevice (DefaultRootWindow(m_display), &mask, keyboard_deviceid);
	
	// Keyboard Keys
	for (int i = min_keycode; i <= max_keycode; ++i)
	{
		Key *temp_key = new Key(m_display, i, m_state.keyboard);
		if (temp_key->m_keyname.length())
			AddInput(temp_key);
		else
			delete temp_key;
	}

	// Mouse Buttons
	for (int i = 0; i < 5; i++)
		AddInput (new Button(i, m_state.buttons));

	// Mouse Cursor/Axis, X-/+ and Y-/+
	for (int i = 0; i != 4; ++i)
	{
		AddInput(new Cursor(!!(i & 2), !!(i & 1), (&m_state.cursor.x)[!!(i & 2)]));
		AddInput(new Axis(!!(i & 2), !!(i & 1), (&m_state.axis.x)[!!(i & 2)]));
	}
}

KeyboardMouse::~KeyboardMouse()
{
	XCloseDisplay(m_display);
}

void KeyboardMouse::UpdateCursor()
{
	double root_x, root_y, win_x, win_y;
	Window root, child;
	
	// stubs-- we're not interested in button presses here
	XIButtonState button_state = 
	{
		0, // mask_len
		NULL // mask
	};
	XIModifierState mods;
	XIGroupState group;
	
	XIQueryPointer(m_display, pointer_deviceid, m_window, &root, &child, &root_x, &root_y, &win_x, &win_y, &button_state, &mods, &group);

	// update mouse cursor
	XWindowAttributes win_attribs;
	XGetWindowAttributes(m_display, m_window, &win_attribs);

	// the mouse position as a range from -1 to 1
	m_state.cursor.x = win_x / (float)win_attribs.width * 2 - 1;
	m_state.cursor.y = win_y / (float)win_attribs.height * 2 - 1;
}

bool KeyboardMouse::UpdateInput()
{
	XFlush (m_display);
	
	// first, get the absolute position of the mouse pointer
	UpdateCursor ();
	
	float delta_x = 0.0f, delta_y = 0.0f;
	double delta_delta;
	
	// then, iterate through the events we're interested in
	XEvent event;
	while (XPending (m_display)) 
	{
		XNextEvent (m_display, &event);
		
		if (event.xcookie.type != GenericEvent)
			continue;
		if (event.xcookie.extension != xi_opcode)
			continue;
		if (!XGetEventData (m_display, &event.xcookie))
			continue;
		
		// only one of these will get used
		XIDeviceEvent *dev_event = (XIDeviceEvent*)event.xcookie.data;
		XIRawEvent *raw_event = (XIRawEvent*)event.xcookie.data;
		
		switch (event.xcookie.evtype)
		{
		case XI_ButtonPress:
			m_state.buttons |= 1<<(dev_event->detail-1);
			break;
		case XI_ButtonRelease:
			m_state.buttons &= ~(1<<(dev_event->detail-1));
			break;
		case XI_KeyPress:
			m_state.keyboard[dev_event->detail / 8] |= 1<<(dev_event->detail % 8);
			break;
		case XI_KeyRelease:
			m_state.keyboard[dev_event->detail / 8] &= ~(1<<(dev_event->detail % 8));
			break;
		case XI_RawMotion:
			// always safe because there is always at least one byte in 
			// raw_event->valuators.mask, and if a bit is set in the mask,
			// then the value in raw_values is also available.
			if (XIMaskIsSet (raw_event->valuators.mask, 0))
			{
			    delta_delta = raw_event->raw_values[0];
			    // test for inf and nan
			    if (delta_delta == delta_delta && 1+delta_delta != delta_delta)
				    delta_x += delta_delta;
			}
			if (XIMaskIsSet (raw_event->valuators.mask, 1))
			{
				delta_delta = raw_event->raw_values[1];
				// test for inf and nan
				if (delta_delta == delta_delta && 1+delta_delta != delta_delta)
				    delta_y += delta_delta;
			}
			break;
		}
		
		XFreeEventData (m_display, &event.xcookie);
	}
	
#define SMOOTHING_AMT 1.5f
    m_state.axis.x *= SMOOTHING_AMT;
	m_state.axis.x += delta_x;
	m_state.axis.x /= SMOOTHING_AMT+1.0f;
	m_state.axis.y *= SMOOTHING_AMT;
	m_state.axis.y += delta_y;
	m_state.axis.y /= SMOOTHING_AMT+1.0f;
	
	return true;
}

bool KeyboardMouse::UpdateOutput()
{
	return true;
}


std::string KeyboardMouse::GetName() const
{
	return name;
}

std::string KeyboardMouse::GetSource() const
{
	return "Xinput2";
}

int KeyboardMouse::GetId() const
{
	return -1;
}

KeyboardMouse::Key::Key(Display* const display, KeyCode keycode, const char* keyboard)
	: m_display(display), m_keyboard(keyboard), m_keycode(keycode)
{
	int i = 0;
	KeySym keysym = 0;
	do
	{
		keysym = XkbKeycodeToKeysym(m_display, keycode, i, 0);
		i++;
	}
	while (keysym == NoSymbol && i < 8);
	
	// Convert to upper case for the keyname
	if (keysym >= 97 && keysym <= 122)
		keysym -= 32;

	// 0x0110ffff is the top of the unicode character range according
	// to keysymdef.h although it is probably more than we need.
	if (keysym == NoSymbol || keysym > 0x0110ffff ||
		XKeysymToString(keysym) == NULL)
		m_keyname = std::string();
	else
		m_keyname = std::string(XKeysymToString(keysym));
}

ControlState KeyboardMouse::Key::GetState() const
{
	return (m_keyboard[m_keycode / 8] & (1 << (m_keycode % 8))) != 0;
}

ControlState KeyboardMouse::Button::GetState() const
{
	return ((m_buttons & (1 << m_index)) != 0);
}

KeyboardMouse::Cursor::Cursor(u8 index, bool positive, const float& cursor)
	: m_cursor(cursor), m_index(index), m_positive(positive)
{
	name = std::string ("Cursor ")+(char)('X' + m_index)+(m_positive ? '+' : '-');
}

ControlState KeyboardMouse::Cursor::GetState() const
{
	return std::max(0.0f, m_cursor / (m_positive ? 1.0f : -1.0f));
}

KeyboardMouse::Axis::Axis(u8 index, bool positive, const float& axis)
	: m_axis(axis), m_index(index), m_positive(positive)
{
	name = std::string ("Axis ")+(char)('X' + m_index)+(m_positive ? '+' : '-');
}

ControlState KeyboardMouse::Axis::GetState() const
{
	return std::max(0.0f, m_axis / (m_positive ? MOUSE_AXIS_SENSITIVITY : -MOUSE_AXIS_SENSITIVITY));
}

std::string KeyboardMouse::Button::GetName() const
{
	static char tmpstr[] = "Click .";
	tmpstr[6] = m_index + '1';
	return tmpstr;
}

}
}
