// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#endif

#include "ConfigManager.h"

#include "Thread.h"

#include "CLI.h"

int main(int argc, char* argv[])
{
#ifdef __APPLE__
	[NSApplication sharedApplication];
	[NSApp activateIgnoringOtherApps: YES];
	[NSApp finishLaunching];
#endif
	int c, help = 0;
	std::string exec = "";

	struct option longopts[] = {
		{ "benchmark",	no_argument,	NULL,	'B' },
		{ "exec",	optional_argument,	NULL,	'e' },
		{ "help",	no_argument,	NULL,	'h' },
		{ "version",	no_argument,	NULL,	53 },
		{ "video",	required_argument,	NULL,	'V' },
		{ NULL,		0,		NULL,	0 }
	};

	SConfig::Init();

	while ((c = getopt_long(argc, argv, "Be::h?V:", longopts, 0)) != -1) {
		switch (c) {
		case 'B':
			SConfig::GetInstance().m_LocalCoreStartupParameter.bBenchmark = true;
			SConfig::GetInstance().m_Framelimit = 0;
			break;
		case 'e':
			if (optarg) exec = std::string(optarg);
			break;
		case -1:
		case 0:
		case 'h':
		case '?':
			help = 1;
			break;
		case 53:
			fprintf(stderr, "%s\n", scm_rev_str);
			return 1;
		case 'V':
			SConfig::GetInstance().m_LocalCoreStartupParameter.m_strVideoBackend = std::string(optarg);
			break;
		 default:
            printf ("?? getopt returned character code 0%o ??\n", c);
		}
	}

	if (help || argc == 1) {
		fprintf(stderr, "%s\n\n", scm_rev_str);
		fprintf(stderr, "A multi-platform Gamecube/Wii emulator\n\n");
		fprintf(stderr, "Usage: %s [-b] [-h] [-v <video backend>] [-e[<file>]]\n", argv[0]);
		fprintf(stderr, "  -b, --bench	Benchmark mode\n");
		fprintf(stderr, "  -e, --exec	Load the specified file\n");
		fprintf(stderr, "  -h, --help	Show this help message\n");
		fprintf(stderr, "  -v, --exec	Use the specified video backend\n");
		fprintf(stderr, "  --version	Print version and exit\n");
		return 1;
	}

	CLI::Entry(exec, true);

	return 0;
}


// ------------
// Talk to interface

void Host_NotifyMapLoaded() {}

void Host_RefreshDSPDebuggerWindow() {}

void Host_ShowJitResults(unsigned int address){}

bool Host_IsCLI() { return CLI::isCLI; }

void Host_Message(int Id) {	CLI::Message(Id); }

void* Host_GetRenderHandle() { return CLI::GetRenderHandle(); }

void* Host_GetInstance() { return NULL; }

void Host_UpdateTitle(const char* title){};

void Host_UpdateLogDisplay(){}

void Host_UpdateDisasmDialog(){}

void Host_UpdateMainFrame() { CLI::UpdateMainFrame(); }

void Host_UpdateBreakPointView(){}

bool Host_GetKeyState(int keycode) { return false; }

void Host_GetRenderWindowSize(int& x, int& y, int& width, int& height)
{
	CLI::GetRenderWindowSize(x, y, width, height);
}

void Host_RequestRenderWindowSize(int width, int height) {}

void Host_SetStartupDebuggingParameters()
{
    CLI::SetStartupDebuggingParameters();
}

bool Host_RendererHasFocus() { return CLI::rendererHasFocus; }

void Host_ConnectWiimote(int wm_idx, bool connect) {}

void Host_SetWaitCursor(bool enable){}

void Host_UpdateStatusBar(const char* _pText, int Filed){}

void Host_SysMessage(const char *fmt, ...)
{
	va_list list;
	char msg[512];

	va_start(list, fmt);
	vsprintf(msg, fmt, list);
	va_end(list);

	size_t len = strlen(msg);
	if (msg[len - 1] != '\n') {
		msg[len - 1] = '\n';
		msg[len] = '\0';
	}
	fprintf(stderr, "%s", msg);
}

void Host_SetWiiMoteConnectionState(int _State) {}
