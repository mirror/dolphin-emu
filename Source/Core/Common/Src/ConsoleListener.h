// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _CONSOLELISTENER_H
#define _CONSOLELISTENER_H

#include "LogManager.h"

#ifdef _WIN32
#include <windows.h>
#endif

class ConsoleListener : public LogListener
{
public:
	ConsoleListener();
	~ConsoleListener();

	void Open(bool Hidden = false, int Width = 100, int Height = 100, const char * Name = "Console");
	void UpdateHandle();
	void Close();
	bool IsOpen();
	bool IsConhost();
	bool IsNonCmdConhost();
	void LetterSpace(int Width, int Height);
	void BufferWidthHeight(int BufferWidth, int BufferHeight, int ScreenWidth, int ScreenHeight, bool BufferFirst);
	void PixelSpace(int Left, int Top, int Width, int Height, bool);
#ifdef _WIN32
	COORD GetCoordinates(int BytesRead, int BufferWidth);
#endif
	void Log(LogTypes::LOG_LEVELS, const char *Text);
	void ClearScreen(bool Cursor = true);

private:
#ifdef _WIN32
	void AttachConsole();
	DWORD GetParentPID();
	HWND GetHwnd(void);
	void SetCmdhost();
	void Write(HANDLE hConsole_, LogTypes::LOG_LEVELS, const char *Text, WORD color);
	HANDLE hConsole;
	HANDLE hHostConsole;
	bool isNonCmdConhost;
#endif
	bool bUseColor;
};

#endif  // _CONSOLELISTENER_H
