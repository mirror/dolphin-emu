// Code partially copied from nuvee pcsx2 guncon plugin
#include "../ControllerInterface.h"

#ifdef CIFACE_USE_RINPUT

#include "RInput.h"

namespace ciface
{
namespace RInput
{

void write(const char *fmt, ...) {
	return;
	if (!GetConsoleWindow()) {
		AllocConsole();
		int iWidth = 120, iHeight = 50;
		COORD Co = {iWidth, 1000};
		bool SB = SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), Co);
		MoveWindow(GetConsoleWindow(), 0,0, 3000*8,iHeight*14, true);
	}
	static int i = 0; i++;
	static const int MAX_BYTES = 1024*8;
	char s[MAX_BYTES];
	va_list argptr;
	int cnt;
	va_start(argptr, fmt);
	cnt = vsnprintf(s, MAX_BYTES, fmt, argptr);
	va_end(argptr);
	DWORD cCharsWritten;
	WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), s, strlen(s), &cCharsWritten, NULL);
}

std::vector<ControllerInterface::Device*> *m_devices;
bool UpdateInput(LPARAM lParam);
WNDPROC pOldWinProc = 0;
HWND hwnd = 0;

typedef WINUSERAPI INT (WINAPI *pGetRawInputDeviceList)(OUT PRAWINPUTDEVICELIST pRawInputDeviceList, IN OUT PINT puiNumDevices, IN UINT cbSize);
typedef WINUSERAPI INT (WINAPI *pGetRawInputData)(IN HRAWINPUT hRawInput, IN UINT uiCommand, OUT LPVOID pData, IN OUT PINT pcbSize, IN UINT cbSizeHeader);
typedef WINUSERAPI INT (WINAPI *pGetRawInputDeviceInfoA)(IN HANDLE hDevice, IN UINT uiCommand, OUT LPVOID pData, IN OUT PINT pcbSize);
typedef WINUSERAPI BOOL (WINAPI *pRegisterRawInputDevices)(IN PCRAWINPUTDEVICE pRawInputDevices, IN UINT uiNumDevices, IN UINT cbSize);
pGetRawInputDeviceList _GRIDL;
pGetRawInputData _GRID;
pGetRawInputDeviceInfoA _GRIDIA;
pRegisterRawInputDevices _RRID;

typedef struct STRUCT_RAW_MOUSE
{
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

static RAW_MOUSE RawMouse[50];
static PRAW_MOUSE raw_mice;

int nraw_mouse_count;

BOOL include_sys_mouse;
BOOL include_rdp_mouse;
BOOL include_individual_mice;  
BOOL bHasBeenInitialized = 0;

int excluded_sysmouse_devices_count;

BOOL is_rm_rdp_mouse(char cDeviceString[]);

BOOL read_raw_input(PRAWINPUT);

// register to reviece WM_INPUT messages in WNDPROC
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
//    The Act Labs PC USB Light Gun is absolute mode (returns screen coordinates)
//    and mice are relative mode (returns delta)
// NOTE: this value isn't updated until the device registers a WM_INPUT message
BOOL is_raw_mouse_absolute(int);

// This indicates if the coordinates are coming from a multi-monitor setup
// NOTE: this value isn't updated until the device registers a WM_INPUT message
BOOL is_raw_mouse_virtual_desktop(int);

void reset_raw_mouse_data(int);

LRESULT WINAPI RWndproc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message) {
		case WM_INPUT: UpdateInput(lParam); break;
	}
	return CallWindowProc(pOldWinProc, hwnd, message, wParam, lParam);
}

// override wndproc
void SetHWND(HWND _hwnd)
{
	// restore old wndproc pointer
	if(pOldWinProc) {
		SetWindowLongPtr(hwnd, GWLP_WNDPROC, (long)pOldWinProc);
		write("old %d %d\n", hwnd, pOldWinProc);
	}
	// update pointer
	hwnd = _hwnd;
	pOldWinProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
	SetWindowLongPtr(hwnd, GWLP_WNDPROC, (long)RWndproc);
	write("new %d %d\n", hwnd, pOldWinProc);
}

void Init(std::vector<ControllerInterface::Device*>& devices, HWND _hwnd)
{
	write("Init\n");
	char guid[512];	
	m_devices = &devices;
	SetHWND(_hwnd);
	// prevent double hooking
	if(rawinput_active) return;	
	// registers for (sysmouse=yes, terminal services mouse=no, HID_mice=yes)
	if(!init_raw_mouse(0, 0, 1)) return;
	register_raw_mouse(hwnd);
	for(int i=0; i<raw_mouse_count(); i++) {
		strcpy(guid, get_raw_mouse_name(i));
		devices.push_back(new Mouse(i,guid));
		write("push_back %d %s\n", i, guid);
	}
	rawinput_active = true;
}

bool MouseOver(HWND hwnd)
{
	POINT p;
	GetCursorPos(&p);
	ScreenToClient(hwnd, &p);
	RECT r;
	GetClientRect(hwnd, &r);
	if(p.x >= 0 && p.y >= 0 && p.x <= r.right && p.y <= r.bottom)
		return true;
	else
		return false;
}

// called by windows
bool UpdateInput(LPARAM lParam)
{
	size_t ok_count = 0;
	std::vector<ControllerInterface::Device*>::const_iterator
		d = m_devices->begin(),
		e = m_devices->end();
	for ( ;d != e; ++d ) {
		if ((*d)->UpdateInput(lParam)) ++ok_count;
	}
	return (m_devices->size() == ok_count);
}

Mouse::~Mouse() {
	unregister_raw_mouse(hwnd);
	bHasBeenInitialized = 0;
	rawinput_active = false;
}
Mouse::Mouse(int _hid, char _guid[512])
{
	hid = _hid;
	mouse_x = 0; mouse_y = 0;
	mouse_sensitivity = 30;
	ZeroMemory(&m_state_in, sizeof(m_state_in));
	strcpy(guid, _guid);
	
	// buttons
	for (u8 i = 0; i < MAX_RAW_MOUSE_BUTTONS; ++i)
		AddInput(new Button(i, m_state_in.button[i], this));
	// cursor
	for (unsigned int i=0; i<4; ++i)
		AddInput(new Cursor(!!(i&2), (&m_state_in.cursor.x)[i/2], !!(i&1), this));
}
void Mouse::DetectDevice() {
	int done;
	int i;
	for(i = 0; i < raw_mouse_count(); i++)
		reset_raw_mouse_data(i);
	done = 0;
	while(!done) {
		for(i = raw_mouse_count()-1; i >= 0; i--) {
			// check for active device
			if(is_raw_mouse_button_pressed(i,0) ||
					is_raw_mouse_button_pressed(i,1) ||
					is_raw_mouse_button_pressed(i,2))
			{
				done = 1;
				hid = i;
				strcpy(guid, get_raw_mouse_name(i));
			}
		}
		Sleep(1);
	}
}

std::string Mouse::GetName() const { return "Mouse"; }
int Mouse::GetId() const { return hid; }
std::string Mouse::GetSource() const { return "RInput"; }

ControlState Mouse::Button::GetState() const
{
	m_parent->_UpdateOutput();
	return (m_button != 0);
}
ControlState Mouse::Cursor::GetState() const
{
	m_parent->_UpdateOutput();
	return std::max(0.0f, ControlState(m_axis) / (m_positive ? 1.0f : -1.0f));
}

std::string Mouse::Button::GetName() const
{
	return std::string("Click ") + char('0' + m_index);
}
std::string Mouse::Cursor::GetName() const
{
	static char tmpstr[] = "Cursor ..";
	tmpstr[7] = (char)('X' + m_index);
	tmpstr[8] = (m_positive ? '+' : '-');
	return tmpstr;
}

bool Mouse::_UpdateOutput()
{
	if(!MouseOver(hwnd)) return false;

	int data_x, data_y;
	BOOL data_absolute;

	// retriev raw data
	data_x = get_raw_mouse_x_delta(hid);
	data_y = get_raw_mouse_y_delta(hid);	
	for(int i = 0; i < MAX_RAW_MOUSE_BUTTONS; i++) {
		m_state_in.button[i] = is_raw_mouse_button_pressed(hid,i);
	}
	data_absolute = is_raw_mouse_absolute(hid);

	data_x = ceil((double)data_x * mouse_sensitivity / 100.0-0.5);
	data_y = ceil((double)data_y * mouse_sensitivity / 100.0-0.5);

	// relative mouse
	if(!data_absolute) {
		mouse_x += data_x;
		mouse_y += data_y;
	} else {
		mouse_x = data_x;
		mouse_y = data_y;
	}
	// containing box
	RECT r;
	GetWindowRect(hwnd, &r);
	if(mouse_x < 0) mouse_x = 0;
	if(mouse_y < 0) mouse_y = 0;
	if(mouse_x > r.right) mouse_x = r.right;
	if(mouse_y > r.bottom) mouse_y = r.bottom;

	// save
	m_state_in.cursor.x = ((float)mouse_x-((float)(r.right-r.left)/2.0)) / ((float)(r.right-r.left)/2.0);
	m_state_in.cursor.y = ((float)mouse_y-((float)(r.bottom-r.top)/2.0)) / ((float)(r.bottom-r.top)/2.0);

	// sync cursor
	if(!(mouse_x == last_mouse_x && mouse_y == last_mouse_y)) {
		// force default cursor
		ShowCursor(1);
		// windowed mode adjustments
		if(r.top != 0 && !SConfig::GetInstance().m_LocalCoreStartupParameter.bRenderToMain) {
			r.top += 32;
			r.top += 0;
			r.left += 4;
			r.right -= 4;
			r.bottom -= 4;
		}
		SetCursorPos(r.left + mouse_x,r.top + mouse_y);
	}
	last_mouse_x = mouse_x; last_mouse_y = mouse_y;
	write("UpdateOutput %d %d %d %dd | %d %d | %d %d | %0.2f %0.2f\n", r.top, r.left, r.right, r.bottom, data_x, data_y, mouse_x, mouse_y,
		m_state_in.cursor.x, m_state_in.cursor.y);
	return true;
}

bool Mouse::UpdateInput(LPARAM lParam)
{
	add_to_raw_mouse_x_and_y((HRAWINPUT)lParam);	
	return true;
}

//	raw_mouse_count
int raw_mouse_count() {
    return nraw_mouse_count;
}

//	is_rm_rdp_mouse
BOOL is_rm_rdp_mouse(char cDeviceString[])
{
	int i;
	char cRDPString[] = "\\\\?\\Root#RDP_MOU#0000#";
	if (strlen(cDeviceString) < 22) return 0;
	for (i = 0; i < 22; i++)
		if (cRDPString[i] != cDeviceString[i]) return 0;
	return 1;
}

//	init_raw_mouse
BOOL init_raw_mouse(BOOL in_include_sys_mouse, BOOL in_include_rdp_mouse, BOOL in_include_individual_mice)
{
	int nInputDevices, i, j;
	PRAWINPUTDEVICELIST pRawInputDeviceList;	
	
	int currentmouse = 0;
	int nSize;
	char *psName;

	char buffer[80];

	// Return 0 if rawinput is not available
	HMODULE user32 = LoadLibrary(L"user32.dll");
	if (!user32) return 0;
	_RRID = (pRegisterRawInputDevices)GetProcAddress(user32,"RegisterRawInputDevices");
	if (!_RRID) return 0;
	_GRIDL = (pGetRawInputDeviceList)GetProcAddress(user32,"GetRawInputDeviceList");
	if (!_GRIDL) return 0;
	_GRIDIA = (pGetRawInputDeviceInfoA)GetProcAddress(user32,"GetRawInputDeviceInfoA");
	if (!_GRIDIA) return 0;
	_GRID = (pGetRawInputData)GetProcAddress(user32,"GetRawInputData");
	if (!_GRID) return 0;
  
	excluded_sysmouse_devices_count = 0;
	nraw_mouse_count = 0;

	if (bHasBeenInitialized) {
		fprintf(stderr, "WARNING: rawmouse init called after initialization already completed.");
		bHasBeenInitialized = 1;
		return 0;
	}

	include_sys_mouse = in_include_sys_mouse;
	include_rdp_mouse = in_include_rdp_mouse;
	include_individual_mice = in_include_individual_mice;

	// 1st call to GetRawInputDeviceList: Pass NULL to get the number of devices.
	if (/* GetRawInputDeviceList */ (*_GRIDL)(NULL, &nInputDevices, sizeof(RAWINPUTDEVICELIST)) != 0) {
		fprintf(stderr, "ERROR: Unable to count raw input devices.\n");
		return 0;
	}

	// Allocate the array to hold the DeviceList
	if ((pRawInputDeviceList = (PRAWINPUTDEVICELIST) malloc(sizeof(RAWINPUTDEVICELIST) * nInputDevices)) == NULL) {
		fprintf(stderr, "ERROR: Unable to allocate memory for raw input device list.\n");
		return 0;
	}

	// 2nd call to GetRawInputDeviceList: Pass the pointer to our DeviceList and GetRawInputDeviceList() will fill the array
	if (/* GetRawInputDeviceList */ (*_GRIDL)(pRawInputDeviceList, &nInputDevices, sizeof(RAWINPUTDEVICELIST)) == -1)  {
		fprintf(stderr, "ERROR: Unable to get raw input device list.\n");
		return 0;
	}

	// Loop through all devices and count the mice
	for (i = 0; i < nInputDevices; i++) {
		if (pRawInputDeviceList[i].dwType == RIM_TYPEMOUSE) {
		        /* Get the device name and use it to determine if it's the RDP Terminal Services virtual device. */

			// 1st call to GetRawInputDeviceInfo: Pass NULL to get the size of the device name 
		        if (/* GetRawInputDeviceInfo */ (*_GRIDIA)(pRawInputDeviceList[i].hDevice, RIDI_DEVICENAME, NULL, &nSize) != 0) {
				fprintf(stderr, "ERROR: Unable to get size of raw input device name.\n");
				return 0;
			}
				
			// Allocate the array to hold the name
			if ((psName = (char *)malloc(sizeof(TCHAR) * nSize)) == NULL)  {
				fprintf(stderr, "ERROR: Unable to allocate memory for device name.\n");
				return 0;
			}

			// 2nd call to GetRawInputDeviceInfo: Pass our pointer to get the device name
			if ((int)/* GetRawInputDeviceInfo */ (*_GRIDIA)(pRawInputDeviceList[i].hDevice, RIDI_DEVICENAME, psName, &nSize) < 0)  {
				fprintf(stderr, "ERROR: Unable to get raw input device name.\n");
				return 0;
			} 

			// Count this mouse for allocation if it's not an RDP mouse or if we want to include the rdp mouse
			if (is_rm_rdp_mouse(psName)) {
				if (include_rdp_mouse) nraw_mouse_count++;
			}
			else { // It's an ordinary mouse
				nraw_mouse_count++;
				if (!include_individual_mice) excluded_sysmouse_devices_count++;     // Don't count this in the final nraw_mouse_count value
			}
		}
	}

	if (include_sys_mouse)
		nraw_mouse_count++;

	// Allocate the array for the raw mice
	raw_mice = (PRAW_MOUSE)(&RawMouse);

	// Define the sys mouse
	if (include_sys_mouse) {
		raw_mice[RAW_SYS_MOUSE].device_handle = 0;
		raw_mice[RAW_SYS_MOUSE].x = 0;
		raw_mice[RAW_SYS_MOUSE].y = 0;
		raw_mice[RAW_SYS_MOUSE].z = 0;
		raw_mice[RAW_SYS_MOUSE].is_absolute = 0;
		raw_mice[RAW_SYS_MOUSE].is_virtual_desktop = 0;
		raw_mice[RAW_SYS_MOUSE].name[0] = 0;

		currentmouse++;
	}

	// Loop through all devices and set the device handles and initialize the mouse values
	for (i = 0; i < nInputDevices; i++) {
		if (pRawInputDeviceList[i].dwType == RIM_TYPEMOUSE) {
			// 1st call to GetRawInputDeviceInfo: Pass NULL to get the size of the device name 
		        if (/* GetRawInputDeviceInfo */ (*_GRIDIA)(pRawInputDeviceList[i].hDevice, RIDI_DEVICENAME, NULL, &nSize) != 0)  {
				fprintf(stderr, "ERROR: Unable to get size of raw input device name (2).\n");
				return 0;
			}
			
			// Allocate the array to hold the name
			if ((psName = (char *)malloc(sizeof(TCHAR) * nSize)) == NULL) {
				fprintf(stderr, "ERROR: Unable to allocate memory for raw input device name (2).\n");
				return 0;
			}
		  
			// 2nd call to GetRawInputDeviceInfo: Pass our pointer to get the device name
			if ((int)/* GetRawInputDeviceInfo */ (*_GRIDIA)(pRawInputDeviceList[i].hDevice, RIDI_DEVICENAME, psName, &nSize) < 0) {
				fprintf(stderr, "ERROR: Unable to get raw input device name (2).\n");
				return 0;
			} 

			// Add this mouse to the array if it's not an RDPMouse or if we wish to include the RDP mouse
			if ((!is_rm_rdp_mouse(psName)) || include_rdp_mouse ) {
				raw_mice[currentmouse].device_handle = pRawInputDeviceList[i].hDevice;
				raw_mice[currentmouse].x = 0;
				raw_mice[currentmouse].y = 0;
				raw_mice[currentmouse].z = 0;
				raw_mice[currentmouse].is_absolute = 0;
				raw_mice[currentmouse].is_virtual_desktop = 0;

				strcpy( (char *) (raw_mice[currentmouse].name), psName );

				currentmouse++;
			}
		}
	}
    
	// free the RAWINPUTDEVICELIST
	free(pRawInputDeviceList);

	for (i = 0; i < nraw_mouse_count; i++) {
		for (j = 0; j < MAX_RAW_MOUSE_BUTTONS; j++) {
			raw_mice[i].buttonpressed[j] = 0;

			// Create the name for this button
			sprintf(buffer, "Button %i", j);
			raw_mice[i].button_name[j] = (char *)malloc(strlen(buffer) + 1);
			sprintf(raw_mice[i].button_name[j], "%s", buffer);
		}
	}

	nraw_mouse_count -= excluded_sysmouse_devices_count;

	bHasBeenInitialized = 1;
	return 1;
}

//	register_raw_mouse
BOOL register_raw_mouse(HWND hwnd)
{
	RAWINPUTDEVICE Rid[1];
	Rid[0].usUsagePage = 0x01;
	Rid[0].usUsage = 0x02;
	Rid[0].dwFlags = RIDEV_INPUTSINK;
	Rid[0].hwndTarget = hwnd;
	if (!(*_RRID)(Rid, 1, sizeof(Rid[0]))) return 0;
	return 1;
}

// unregister_raw_mouse
BOOL unregister_raw_mouse(HWND hwnd)
{
	RAWINPUTDEVICE Rid[1];
	Rid[0].usUsagePage = 0x01;
	Rid[0].usUsage = 0x02;
	Rid[0].dwFlags = RIDEV_REMOVE;
	Rid[0].hwndTarget = hwnd;
	if (!(*_RRID)(Rid, 1, sizeof(Rid[0]))) return 0;
	return 1;
}

// destroy_raw_mouse
void destroy_raw_mouse(void)
{
	free(raw_mice);
}

// read_raw_input
BOOL read_raw_input(PRAWINPUT raw)
{
	  // should be static when I get around to it
	  int i;

	  // mouse 0 is sysmouse, so if there is not sysmouse start loop @0
	  i = 0;
	  if (include_sys_mouse) i++; 

	  for ( ; i < (nraw_mouse_count + excluded_sysmouse_devices_count); i++) {
			if (raw_mice[i].device_handle == raw->header.hDevice)
			{
				// Update the values for the specified mouse
				if (include_individual_mice) {
					if (raw_mice[i].is_absolute) {
						raw_mice[i].x = raw->data.mouse.lLastX;
						raw_mice[i].y = raw->data.mouse.lLastY;
					}
					else { // relative
						raw_mice[i].x += raw->data.mouse.lLastX;
						raw_mice[i].y += raw->data.mouse.lLastY;
					}
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_DOWN) raw_mice[i].buttonpressed[0] = 1;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_UP)   raw_mice[i].buttonpressed[0] = 0;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_DOWN) raw_mice[i].buttonpressed[1] = 1;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_UP)   raw_mice[i].buttonpressed[1] = 0;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_DOWN) raw_mice[i].buttonpressed[2] = 1;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_UP)   raw_mice[i].buttonpressed[2] = 0;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) raw_mice[i].buttonpressed[3] = 1;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP)   raw_mice[i].buttonpressed[3] = 0;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) raw_mice[i].buttonpressed[4] = 1;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP)   raw_mice[i].buttonpressed[4] = 0;

					if (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)          raw_mice[i].is_absolute = 1;
					else																												raw_mice[i].is_absolute = 0;
					if (raw->data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP)        raw_mice[i].is_virtual_desktop = 1;
					else                                                        raw_mice[i].is_virtual_desktop = 0;

					if (raw->data.mouse.usButtonFlags & RI_MOUSE_WHEEL) {      // If the current message has a mouse_wheel message
						if ((SHORT)raw->data.mouse.usButtonData > 0) {
							raw_mice[i].z++;
						}
						if ((SHORT)raw->data.mouse.usButtonData < 0) {
							raw_mice[i].z--;
						}
					}
				}

				// Feed the values for every mouse into the system mouse
				if (include_sys_mouse) { 
					if (raw_mice[i].is_absolute) {
						raw_mice[RAW_SYS_MOUSE].x = raw->data.mouse.lLastX;
						raw_mice[RAW_SYS_MOUSE].y = raw->data.mouse.lLastY;
					}
					else { // relative
						raw_mice[RAW_SYS_MOUSE].x += raw->data.mouse.lLastX;
						raw_mice[RAW_SYS_MOUSE].y += raw->data.mouse.lLastY;
					}			  
					// This is innacurate:  If 2 mice have their buttons down and I lift up on one, this will register the
					//   system mouse as being "up".  I checked out on my windows desktop, and Microsoft was just as
					//   lazy as I'm going to be.  Drag an icon with the 2 left mouse buttons held down & let go of one.
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_DOWN) raw_mice[RAW_SYS_MOUSE].buttonpressed[0] = 1;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_UP) raw_mice[RAW_SYS_MOUSE].buttonpressed[0] = 0;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_DOWN) raw_mice[RAW_SYS_MOUSE].buttonpressed[1] = 1;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_UP) raw_mice[RAW_SYS_MOUSE].buttonpressed[1] = 0;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_DOWN) raw_mice[RAW_SYS_MOUSE].buttonpressed[2] = 1;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_UP) raw_mice[RAW_SYS_MOUSE].buttonpressed[2] = 0;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) raw_mice[RAW_SYS_MOUSE].buttonpressed[3] = 1;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP) raw_mice[RAW_SYS_MOUSE].buttonpressed[3] = 0;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) raw_mice[RAW_SYS_MOUSE].buttonpressed[4] = 1;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP) raw_mice[RAW_SYS_MOUSE].buttonpressed[4] = 0;
				  
					// If an absolute mouse is triggered, sys mouse will be considered absolute till the end of time.
					if (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)          raw_mice[RAW_SYS_MOUSE].is_absolute = 1;
					// Same goes for virtual desktop
					if (raw->data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP)        raw_mice[RAW_SYS_MOUSE].is_virtual_desktop = 1;

					if (raw->data.mouse.usButtonFlags & RI_MOUSE_WHEEL) {      // If the current message has a mouse_wheel message
						if ((SHORT)raw->data.mouse.usButtonData > 0) {
							raw_mice[RAW_SYS_MOUSE].z++;
						}
						if ((SHORT)raw->data.mouse.usButtonData < 0) {
							raw_mice[RAW_SYS_MOUSE].z--;
						}
					}

				}
			}
	  }	  
	  return 1;
}

//	is_raw_mouse_button_pressed
BOOL is_raw_mouse_button_pressed(int mousenum, int buttonnum) {
	// It's ok to ask if buttons are pressed for unitialized mice - just tell 'em no button's pressed
	if (mousenum >= nraw_mouse_count || buttonnum >= MAX_RAW_MOUSE_BUTTONS || raw_mice == NULL) return 0;

	return (raw_mice[mousenum].buttonpressed[buttonnum]);
}

//	is_raw_mouse_absolute
BOOL is_raw_mouse_absolute(int mousenum)
{
	return (raw_mice[mousenum].is_absolute);
}

//	is_raw_mouse_virtual_desktop
BOOL is_raw_mouse_virtual_desktop(int mousenum)
{
	return (raw_mice[mousenum].is_virtual_desktop);
}

//	get_raw_mouse_button_name
char *get_raw_mouse_button_name(int mousenum, int buttonnum) {
	if (mousenum >= nraw_mouse_count || buttonnum >= MAX_RAW_MOUSE_BUTTONS || raw_mice == NULL) return NULL;
	return (raw_mice[mousenum].button_name[buttonnum]);
}

//	add_to_raw_mouse_x_and_y
BOOL add_to_raw_mouse_x_and_y(HANDLE in_device_handle)
{
	// When the WM_INPUT message is received, the lparam must be passed to this function to keep a running tally of
	//     every mouse moves to maintain accurate results for get_raw_mouse_?_delta().
	// This function will take the HANDLE of the device and find the device in the raw_mice arrayand add the 
	//      x and y mousemove values according to the information stored in the RAWINPUT structure.

	LPBYTE lpb;
	int dwSize;

	if (/* GetRawInputData */(*_GRID)((HRAWINPUT)in_device_handle, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER)) == -1) {
		fprintf(stderr, "ERROR: Unable to add to get size of raw input header.\n");
		return 0;
	}

	lpb = (LPBYTE)malloc(sizeof(LPBYTE) * dwSize);
	if (lpb == NULL) {
		fprintf(stderr, "ERROR: Unable to allocate memory for raw input header.\n");
		return 0;
	} 
  
	if (/* GetRawInputData */(*_GRID)((HRAWINPUT)in_device_handle, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) != dwSize ) {
		fprintf(stderr, "ERROR: Unable to add to get raw input header.\n");
		return 0;
	} 

	read_raw_input((RAWINPUT*)lpb);

	free(lpb); 

	return 1;
}

//	get_raw_mouse_x_delta
ULONG get_raw_mouse_x_delta(int mousenum)
{
	ULONG nReturn = 0;
	if (raw_mice != NULL && mousenum < nraw_mouse_count) {
		nReturn = raw_mice[mousenum].x;
		if(!raw_mice[mousenum].is_absolute) raw_mice[mousenum].x = 0;
	}
	return nReturn;
}

//	get_raw_mouse_y_delta
ULONG get_raw_mouse_y_delta(int mousenum)
{
	ULONG nReturn = 0;
	if (raw_mice != NULL && mousenum < nraw_mouse_count) {
		nReturn = raw_mice[mousenum].y;
		if(!raw_mice[mousenum].is_absolute) raw_mice[mousenum].y = 0;
	}
	return nReturn;
}

//	get_raw_mouse_z_delta
ULONG get_raw_mouse_z_delta(int mousenum)
{
	ULONG nReturn = 0;
	if (raw_mice != NULL && mousenum < nraw_mouse_count) {
		nReturn = raw_mice[mousenum].z;
		if(!raw_mice[mousenum].is_absolute) raw_mice[mousenum].z = 0;
	}
	return nReturn;
}

//	get_raw_mouse_name
char *get_raw_mouse_name(int mousenum)
{
	char * nReturn = 0;
	if (raw_mice != NULL && mousenum < nraw_mouse_count) {
		nReturn = (char *) (raw_mice[mousenum].name);
	}
	return nReturn;
}

//	reset_raw_mouse_data
void reset_raw_mouse_data(int mousenum)
{
	if (raw_mice != NULL && mousenum < nraw_mouse_count) {
		raw_mice[mousenum].buttonpressed[0] = 0;
		raw_mice[mousenum].buttonpressed[1] = 0;
		raw_mice[mousenum].buttonpressed[2] = 0;
		raw_mice[mousenum].buttonpressed[3] = 0;
		raw_mice[mousenum].buttonpressed[4] = 0;
		raw_mice[mousenum].x = 0;
		raw_mice[mousenum].y = 0;
		raw_mice[mousenum].z = 0;
	}
}

}
}

#endif