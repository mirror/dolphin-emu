#include <gccore.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <ogcsys.h>

#include <iostream>
#include <debug.h>
#include <math.h>

#include "Gekko.h"
#include "StringUtil.h"

using namespace std;

u32 first_frame = 1;
GXRModeObj *rmode;
vu16 oldstate;
vu16 keystate;
vu16 keydown;
vu16 keyup;
PADStatus pad[4];

visual_stick_position s_pos;

void Initialise();

string stick(int a, string title) {
	return StringFromFormat("pad[%d] %s: Main[ %d, %d ] Sub[ %d, %d ]\n"
				, a
				, title.c_str()
				, pad[a].stickX
				, pad[a].stickY
				, pad[a].substickX
				, pad[a].substickY
				);
}

void print_and_draw(void *screen_buffer)
{
	VIDEO_WaitVSync();

	PAD_Read(pad);

	for(int a = 0; a < 4;a ++)
	{
		if(pad[a].err & PAD_ERR_NO_CONTROLLER)
		{
			std::cout<<"pad["<<a<<"] Not Connected\n";
			continue;
		}

		visual_stick_box(rmode, screen_buffer, s_pos, s_pos.x_of2);
		visual_stick_box(rmode, screen_buffer, s_pos, s_pos.x_of);

		visual_stick(rmode, screen_buffer, pad[a].stickX, pad[a].stickY, s_pos, s_pos.x_of2, COLOR_GRAY);
		visual_stick(rmode, screen_buffer, pad[a].substickX, pad[a].substickY, s_pos, s_pos.x_of, COLOR_GRAY);
		std::cout << stick(a, "Sticks");

		PAD_Clamp(pad);
		std::cout << stick(a, "Sticks clamped");
		visual_stick(rmode, screen_buffer, pad[a].stickX, pad[a].stickY, s_pos, s_pos.x_of2, COLOR_RED);
		visual_stick(rmode, screen_buffer, pad[a].substickX, pad[a].substickY, s_pos, s_pos.x_of, COLOR_RED);

		std::cout<<"pad["<<a<<"] Analog Triggers: Left "<<(int)pad[a].triggerL<<" Right "<<(int)pad[a].triggerR<<"\n";
		std::cout<<"pad["<<a<<"] Buttons: "<<
			(pad[a].button & PAD_BUTTON_START? "Start " : "")<<
			(pad[a].button & PAD_BUTTON_A ? "A " : "")<<
			(pad[a].button & PAD_BUTTON_B ? "B " : "")<<
			(pad[a].button & PAD_BUTTON_X ? "X " : "")<<
			(pad[a].button & PAD_BUTTON_Y ? "Y " : "")<<
			(pad[a].button & PAD_TRIGGER_Z? "Z " : "")<<
			(pad[a].button & PAD_TRIGGER_L? "L " : "")<<
			(pad[a].button & PAD_TRIGGER_R? "R " : "")<<std::endl;
		std::cout<<"pad["<<a<<"] DPad: "<<
			(pad[a].button & PAD_BUTTON_UP ? "Up " : "")<<
			(pad[a].button & PAD_BUTTON_DOWN ? "Down " : "")<<
			(pad[a].button & PAD_BUTTON_LEFT ? "Left " : "")<<
			(pad[a].button & PAD_BUTTON_RIGHT ? "Right " : "")<<std::endl;
	}
}

int main()
{
	void *xfb[2];
	int fbi = 0;

	// Initialise the video system
	VIDEO_Init();

	// This function initialises the attached controllers
	PAD_Init();

	// Obtain the preferred video mode from the system
	// This will correspond to the settings in the Wii menu
	rmode = VIDEO_GetPreferredMode(NULL);

	visual_stick_init(rmode, s_pos);

	// Allocate memory for the display in the uncached region
	xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

	/*
	// Initialise the console, required for printf
	console_init(xfb,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);

	// Set up the video registers with the chosen mode
	VIDEO_Configure(rmode);

	// Tell the video hardware where our display memory is
	VIDEO_SetNextFramebuffer(xfb);

	// Make the display visible
	VIDEO_SetBlack(FALSE);

	// Flush the video register changes to the hardware
	VIDEO_Flush();

	// Wait for Video setup to complete
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
	*/

	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();

	if (rmode->viTVMode&VI_NON_INTERLACE)
		VIDEO_WaitVSync();

	SYS_SetResetCallback(reload);
	SYS_SetPowerCallback(shutdown);

	while(!doreload && !dooff) {
		CON_Init(xfb[fbi] ,0,0, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth*VI_DISPLAY_PIX_SZ);
		//VIDEO_ClearFrameBuffer(rmode,xfb[fbi],COLOR_BLACK);
		std::cout<<"\n\n\n";

		print_and_draw(xfb[fbi]);
		VIDEO_SetNextFramebuffer(xfb[fbi]);
		VIDEO_Flush();
		VIDEO_WaitVSync();
		fbi ^= 1;
	}
	if(doreload) return 0;
	if(dooff) SYS_ResetSystem(SYS_SHUTDOWN,0,0);

	return 0;
}
