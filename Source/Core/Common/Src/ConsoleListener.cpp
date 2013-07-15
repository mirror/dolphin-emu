// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <algorithm>  // min
#include <string> // System: To be able to add strings with "+"
#include <stdio.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#include <iostream>
#include <array>
#include <fcntl.h>
#include <io.h>
#include <tlhelp32.h>
#else
#include <stdarg.h>
#endif

#include "../../Core/Src/Host.h"

#include "Common.h"
#include "LogManager.h" // Common
#include "ConsoleListener.h" // Common

#ifdef _WIN32
DWORD ConsoleListener::GetParentPID()
{
	DWORD pid = 0;
	HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 pe = { 0 };
	pe.dwSize = sizeof(PROCESSENTRY32);
	pid = GetCurrentProcessId();
	if (Process32First(h, &pe))
	{
		do {
			if (pe.th32ProcessID == pid)
				return pe.th32ParentProcessID;
		} while(Process32Next(h, &pe));
	}
	return 0;
}

void ConsoleListener::AttachConsole()
{
	// attach to console host
	::AttachConsole(GetParentPID());

	// point stdout to the console
	long lStdHandle = (long)GetStdHandle(STD_OUTPUT_HANDLE);
	if (!lStdHandle) return;
	int hConHandle = _open_osfhandle(lStdHandle, _O_BINARY);
	FILE *fp = _fdopen(hConHandle, "w");
	*stdout = *fp;
	setvbuf( stdout, NULL, _IONBF, 0 );

	// point stderr to the console
	lStdHandle = (long)GetStdHandle(STD_ERROR_HANDLE);
	hConHandle = _open_osfhandle(lStdHandle, _O_BINARY);
	fp = _fdopen(hConHandle, "w");
	*stderr = *fp;
	setvbuf(stderr, NULL, _IONBF, 0);

	// point cout, wcout, cin, wcin, wcerr, cerr, wclog, clog to console
	std::ios::sync_with_stdio();
}
#endif

ConsoleListener::ConsoleListener()
{
#ifdef _WIN32
	hConsole = NULL;
	isNonCmdConhost = false;
	bUseColor = true;

	if (Host_IsCLI()) AttachConsole();

	// determine host capability
	hHostConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD charsWritten;
	bool isCmdhost = WriteConsoleA(hHostConsole, "", 0, &charsWritten, NULL);
	if (hHostConsole && !isCmdhost)
		isNonCmdConhost = true;
	if (!isCmdhost)
		hHostConsole = NULL;
	if (Host_IsCLI() && !isCmdhost)
		FreeConsole();
#else
	bUseColor = isatty(fileno(stdout));
#endif
}

ConsoleListener::~ConsoleListener()
{
	Close();
}

// 100, 100, "Dolphin Log Console"
// Open console window - width and height is the size of console window
// Name is the window title
void ConsoleListener::Open(bool Hidden, int Width, int Height, const char *Title)
{
#ifdef _WIN32
	if (!GetConsoleWindow())
	{
		// Open the console window and create the window handle for GetStdHandle()
		AllocConsole();
		// Hide
		if (Hidden) ShowWindow(GetConsoleWindow(), SW_HIDE);
		// Save the window handle that AllocConsole() created
		hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		// Set the console window title
		SetConsoleTitle(UTF8ToTStr(Title).c_str());
		// Set letter space
		LetterSpace(80, 4000);
		//MoveWindow(GetConsoleWindow(), 200,200, 800,800, true);
	}
	else
	{
		hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	}
#endif
}

void ConsoleListener::UpdateHandle()
{
#ifdef _WIN32
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
}

// Close the console window and close the eventual file handle
void ConsoleListener::Close()
{
#ifdef _WIN32
	if (hConsole == NULL)
		return;
	FreeConsole();
	hConsole = NULL;
#else
	fflush(NULL);
#endif
}

bool ConsoleListener::IsOpen()
{
#ifdef _WIN32
	return (hConsole != NULL);
#else
	return true;
#endif
}

bool ConsoleListener::IsConhost()
{
#ifdef _WIN32
	return !!hHostConsole;
#else
	return true;
#endif
}

bool ConsoleListener::IsNonCmdConhost()
{
#ifdef _WIN32
	return isNonCmdConhost;
#else
	return true;
#endif
}

/*
  LetterSpace: SetConsoleScreenBufferSize and SetConsoleWindowInfo are
	dependent on each other, that's the reason for the additional checks.  
*/
void ConsoleListener::BufferWidthHeight(int BufferWidth, int BufferHeight, int ScreenWidth, int ScreenHeight, bool BufferFirst)
{
#ifdef _WIN32
	BOOL SB, SW;
	if (BufferFirst)
	{
		// Change screen buffer size
		COORD Co = {BufferWidth, BufferHeight};
		SB = SetConsoleScreenBufferSize(hConsole, Co);
		// Change the screen buffer window size
		SMALL_RECT coo = {0,0,ScreenWidth, ScreenHeight}; // top, left, right, bottom
		SW = SetConsoleWindowInfo(hConsole, TRUE, &coo);
	}
	else
	{
		// Change the screen buffer window size
		SMALL_RECT coo = {0,0, ScreenWidth, ScreenHeight}; // top, left, right, bottom
		SW = SetConsoleWindowInfo(hConsole, TRUE, &coo);
		// Change screen buffer size
		COORD Co = {BufferWidth, BufferHeight};
		SB = SetConsoleScreenBufferSize(hConsole, Co);
	}
#endif
}
void ConsoleListener::LetterSpace(int Width, int Height)
{
#ifdef _WIN32
	// Get console info
	CONSOLE_SCREEN_BUFFER_INFO ConInfo;
	GetConsoleScreenBufferInfo(hConsole, &ConInfo);

	//
	int OldBufferWidth = ConInfo.dwSize.X;
	int OldBufferHeight = ConInfo.dwSize.Y;
	int OldScreenWidth = (ConInfo.srWindow.Right - ConInfo.srWindow.Left);
	int OldScreenHeight = (ConInfo.srWindow.Bottom - ConInfo.srWindow.Top);
	//
	int NewBufferWidth = Width;
	int NewBufferHeight = Height;
	int NewScreenWidth = NewBufferWidth - 1;
	int NewScreenHeight = OldScreenHeight;

	// Width
	BufferWidthHeight(NewBufferWidth, OldBufferHeight, NewScreenWidth, OldScreenHeight, (NewBufferWidth > OldScreenWidth-1));
	// Height
	BufferWidthHeight(NewBufferWidth, NewBufferHeight, NewScreenWidth, NewScreenHeight, (NewBufferHeight > OldScreenHeight-1));

	// Resize the window too
	//MoveWindow(GetConsoleWindow(), 200,200, (Width*8 + 50),(NewScreenHeight*12 + 200), true);
#endif
}
#ifdef _WIN32
COORD ConsoleListener::GetCoordinates(int BytesRead, int BufferWidth)
{
	COORD Ret = {0, 0};
	// Full rows
	int Step = (int)floor((float)BytesRead / (float)BufferWidth);
	Ret.Y += Step;
	// Partial row
	Ret.X = BytesRead - (BufferWidth * Step);
	return Ret;
}
#endif
void ConsoleListener::PixelSpace(int Left, int Top, int Width, int Height, bool Resize)
{
#ifdef _WIN32
	// Check size
	if (Width < 8 || Height < 12) return;

	bool DBef = true;
	bool DAft = true;
	std::string SLog = "";

	const HWND hWnd = GetConsoleWindow();
	const HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

	// Get console info
	CONSOLE_SCREEN_BUFFER_INFO ConInfo;
	GetConsoleScreenBufferInfo(hConsole, &ConInfo);
	DWORD BufferSize = ConInfo.dwSize.X * ConInfo.dwSize.Y;

	// ---------------------------------------------------------------------
	//  Save the current text
	// ------------------------
	DWORD cCharsRead = 0;
	COORD coordScreen = { 0, 0 };

	static const int MAX_BYTES = 1024 * 16;

	std::vector<std::array<TCHAR, MAX_BYTES>> Str;
	std::vector<std::array<WORD, MAX_BYTES>> Attr;

	// ReadConsoleOutputAttribute seems to have a limit at this level
	static const int ReadBufferSize = MAX_BYTES - 32;

	DWORD cAttrRead = ReadBufferSize;
	DWORD BytesRead = 0;
	while (BytesRead < BufferSize)
	{
		Str.resize(Str.size() + 1);
		if (!ReadConsoleOutputCharacter(hConsole, Str.back().data(), ReadBufferSize, coordScreen, &cCharsRead))
			SLog += StringFromFormat("WriteConsoleOutputCharacter error");

		Attr.resize(Attr.size() + 1);
		if (!ReadConsoleOutputAttribute(hConsole, Attr.back().data(), ReadBufferSize, coordScreen, &cAttrRead))
			SLog += StringFromFormat("WriteConsoleOutputAttribute error");

		// Break on error
		if (cAttrRead == 0) break;
		BytesRead += cAttrRead;
		coordScreen = GetCoordinates(BytesRead, ConInfo.dwSize.X);
	}
	// Letter space
	int LWidth = (int)(floor((float)Width / 8.0f) - 1.0f);
	int LHeight = (int)(floor((float)Height / 12.0f) - 1.0f);
	int LBufWidth = LWidth + 1;
	int LBufHeight = (int)floor((float)BufferSize / (float)LBufWidth);
	// Change screen buffer size
	LetterSpace(LBufWidth, LBufHeight);


	ClearScreen(true);
	coordScreen.Y = 0;
	coordScreen.X = 0;
	DWORD cCharsWritten = 0;

	int BytesWritten = 0;
	DWORD cAttrWritten = 0;
	for (size_t i = 0; i < Attr.size(); i++)
	{
		if (!WriteConsoleOutputCharacter(hConsole, Str[i].data(), ReadBufferSize, coordScreen, &cCharsWritten))
			SLog += StringFromFormat("WriteConsoleOutputCharacter error");
		if (!WriteConsoleOutputAttribute(hConsole, Attr[i].data(), ReadBufferSize, coordScreen, &cAttrWritten))
			SLog += StringFromFormat("WriteConsoleOutputAttribute error");

		BytesWritten += cAttrWritten;
		coordScreen = GetCoordinates(BytesWritten, LBufWidth);
	}	

	const int OldCursor = ConInfo.dwCursorPosition.Y * ConInfo.dwSize.X + ConInfo.dwCursorPosition.X;
	COORD Coo = GetCoordinates(OldCursor, LBufWidth);
	SetConsoleCursorPosition(hConsole, Coo);

	if (SLog.length() > 0) Log(LogTypes::LNOTICE, SLog.c_str());

	// Resize the window too
	if (Resize) MoveWindow(GetConsoleWindow(), Left,Top, (Width + 100),Height, true);
#endif
}

#if defined(_WIN32)
void ConsoleListener::Write(HANDLE hConsole_, LogTypes::LOG_LEVELS Level, const char *Text, WORD Color)
{
	if (!hConsole_) return;
	DWORD cCharsWritten;
	if (strlen(Text) > 10)
	{
		// Print timestamp in white
		SetConsoleTextAttribute(hConsole_, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
		WriteConsole(hConsole_, Text, 10, &cCharsWritten, NULL);
		Text += 10;
	}
	SetConsoleTextAttribute(hConsole_, Color);
	WriteConsole(hConsole_, Text, (DWORD)strlen(Text), &cCharsWritten, NULL);
	// Restore color
	SetConsoleTextAttribute(hConsole_, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}
#endif

void ConsoleListener::Log(LogTypes::LOG_LEVELS Level, const char *Text)
{
#if defined(_WIN32)
	/*
	const int MAX_BYTES = 1024*10;
	char Str[MAX_BYTES];
	va_list ArgPtr;
	int Cnt;
	va_start(ArgPtr, Text);
	Cnt = vsnprintf(Str, MAX_BYTES, Text, ArgPtr);
	va_end(ArgPtr);
	*/

	if (hHostConsole || hConsole)
	{
		WORD Color;

		switch (Level)
		{
		case NOTICE_LEVEL: // light green
			Color = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
			break;
		case ERROR_LEVEL: // light red
			Color = FOREGROUND_RED | FOREGROUND_INTENSITY;
			break;
		case WARNING_LEVEL: // light yellow
			Color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
			break;
		case INFO_LEVEL: // cyan
			Color = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
			break;
		case DEBUG_LEVEL: // gray
			Color = FOREGROUND_INTENSITY;
			break;
		default: // off-white
			Color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
			break;
		}
		Write(hHostConsole, Level, Text, Color);
		Write(hConsole, Level, Text, Color);
	}

	if (IsNonCmdConhost())
	{
#endif
		char ColorAttr[16] = "";
		char ResetAttr[16] = "";

		if (bUseColor)
		{
			strcpy(ResetAttr, "\033[0m");
			switch (Level)
			{
			case NOTICE_LEVEL: // light green
				strcpy(ColorAttr, "\033[92m");
				break;
			case ERROR_LEVEL: // light red
				strcpy(ColorAttr, "\033[91m");
				break;
			case WARNING_LEVEL: // light yellow
				strcpy(ColorAttr, "\033[93m");
				break;
			default:
				break;
			}
		}
		fprintf(stderr, "%s%s%s", ColorAttr, Text, ResetAttr);
		fflush(stderr);
#if defined(_WIN32)
	}
#endif
}
// Clear console screen
void ConsoleListener::ClearScreen(bool Cursor)
{ 
#if defined(_WIN32)
	COORD coordScreen = { 0, 0 }; 
	DWORD cCharsWritten; 
	CONSOLE_SCREEN_BUFFER_INFO csbi; 
	DWORD dwConSize; 

	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE); 

	GetConsoleScreenBufferInfo(hConsole, &csbi); 
	dwConSize = csbi.dwSize.X * csbi.dwSize.Y;
	// Write space to the entire console
	FillConsoleOutputCharacter(hConsole, TEXT(' '), dwConSize, coordScreen, &cCharsWritten); 
	GetConsoleScreenBufferInfo(hConsole, &csbi); 
	FillConsoleOutputAttribute(hConsole, csbi.wAttributes, dwConSize, coordScreen, &cCharsWritten);
	// Reset cursor
	if (Cursor) SetConsoleCursorPosition(hConsole, coordScreen); 
#endif
}


