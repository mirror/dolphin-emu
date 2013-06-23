// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _HOST_H
#define _HOST_H


// Host - defines an interface for the emulator core to communicate back to the
// OS-specific layer

// The emulator core is abstracted from the OS using 2 interfaces:
// Common and Host.

// Common simply provides OS-neutral implementations of things like threads, mutexes,
// INI file manipulation, memory mapping, etc. 

// Host is an abstract interface for communicating things back to the host. The emulator
// core is treated as a library, not as a main program, because it is far easier to
// write GUI interfaces that control things than to squash GUI into some model that wasn't
// designed for it.

// The host can be just a command line app that opens a window, or a full blown debugger
// interface.

bool Host_RendererHasFocus();
bool Host_PadConfigOpen();
bool Host_WiimoteConfigOpen();
void Host_ConnectWiimote(int wm_idx, bool connect);
bool Host_GetKeyState(int keycode);
void Host_GetRenderWindowSize(int& x, int& y, int& width, int& height);
void Host_Message(int Id);
void Host_NotifyMapLoaded();
void Host_RefreshDSPDebuggerWindow();
void Host_RequestRenderWindowSize(int width, int height);
void Host_SetStartupDebuggingParameters();
void Host_SetWiiMoteConnectionState(int _State);
void Host_ShowJitResults(unsigned int address);
void Host_SysMessage(const char *fmt, ...);
void Host_UpdateBreakPointView();
void Host_UpdateDisasmDialog();
void Host_UpdateLogDisplay();
void Host_Yield();
void Host_UpdateMainFrame();
void Host_UpdateStatusBar(const char* _pText, int Filed = 0);
void Host_UpdateTitle(const char* title);

// TODO (neobrain): Remove these from host!
void* Host_GetInstance();
void* Host_GetRenderHandle();

#endif
