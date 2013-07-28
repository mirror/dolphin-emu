// This file is from wiibrew.org: http://wiibrew.org/wiki/How_to_use_the_Wiimote

//code by WinterMute
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string.h>
#include <malloc.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <wiiuse/wpad.h>

#include "Gekko.h"
#include "StringUtil.h"

using namespace std;

static GXRModeObj *rmode = NULL;

visual_stick_position s_pos;

int evctr = 0;
void countevs(int chan, const WPADData *data) {
	evctr++;
}

void print_wiimote_connection_status(int wiimote_connection_status) {
	switch(wiimote_connection_status) {
		case WPAD_ERR_NO_CONTROLLER:
			std::cout<<"  Wiimote not connected\n";
			break;
		case WPAD_ERR_NOT_READY:
			std::cout<<"  Wiimote not ready\n";
			break;
		case WPAD_ERR_NONE:
			std::cout<<"  Wiimote ready\n";
			break;
		default:
			std::cout<<"  Unknown Wimote state "<<wiimote_connection_status<<"\n";
	}
}

void print_wiimote_buttons(WPADData *wd) {
	std::cout<<"  Buttons down:\n   ";
	if(wd->btns_h & WPAD_BUTTON_A) std::cout<<"A ";
	if(wd->btns_h & WPAD_BUTTON_B) std::cout<<"B ";
	if(wd->btns_h & WPAD_BUTTON_1) std::cout<<"1 ";
	if(wd->btns_h & WPAD_BUTTON_2) std::cout<<"2 ";
	if(wd->btns_h & WPAD_BUTTON_MINUS) std::cout<<"MINUS ";
	if(wd->btns_h & WPAD_BUTTON_HOME) std::cout<<"HOME ";
	if(wd->btns_h & WPAD_BUTTON_PLUS) std::cout<<"PLUS ";
	std::cout<<"\n   ";
	if(wd->btns_h & WPAD_BUTTON_LEFT) std::cout<<"LEFT ";
	if(wd->btns_h & WPAD_BUTTON_RIGHT) std::cout<<"RIGHT ";
	if(wd->btns_h & WPAD_BUTTON_UP) std::cout<<"UP ";
	if(wd->btns_h & WPAD_BUTTON_DOWN) std::cout<<"DOWN ";
	std::cout<<"\n";
}

void print_and_draw_wiimote_data(void *screen_buffer) {
	// wiimote to screen coordinates
	fdot_t scale;
	scale.x = rmode->fbWidth / 2.0 / 1024.0;
	scale.y = rmode->xfbHeight / 2.0 / 768.0;
	fdot_t pos;
	pos.x = 0;
	pos.y = 0;

	//Makes the var wd point to the data on the wiimote
	WPADData *wd = WPAD_Data(0);
	std::cout<<"  Data->Err: "<<wd->err<<"\n";
	std::cout<<"  IR Dots:\n";
	int i;
	for(i=0; i<4; i++) {
		if(wd->ir.dot[i].visible) {
			std::cout<<"   "<<wd->ir.dot[i].rx<<", "<<wd->ir.dot[i].ry<<"\n";
			draw_rectangle(screen_buffer, rmode, 8, 8
				, s32(double(wd->ir.dot[i].rx) * scale.x)
				, s32(double(wd->ir.dot[i].ry) * scale.y)
				, COLOR_YELLOW);
		} else {
			std::cout<<"   None\n";
		}
	}
	//ir.valid - TRUE is the wiimote is pointing at the screen, else it is false
	if(wd->ir.valid) {
		float theta = wd->ir.angle / 180.0 * M_PI;

		//ir.x/ir.y - The x/y coordinates that the wiimote is pointing to, relative to the screen.
		//ir.angle - how far (in degrees) the wiimote is twisted (based on ir)
		std::cout << StringFromFormat(
				"  Cursor: %.0f, %.0f\n"
				, wd->ir.x
				, wd->ir.y
				);

		std::cout<<"    @ "<<wd->ir.angle<<" deg\n";

		draw_rectangle(screen_buffer, rmode, 8, 8
			, s32(double(wd->ir.x) * scale.x)
			, s32(double(wd->ir.y) * scale.y)
			, COLOR_RED);
		draw_rectangle(screen_buffer, rmode, 8, 8
			, s32(double(wd->ir.x + 10*sinf(theta)) * scale.x)
			, s32(double(wd->ir.y - 10*cosf(theta)) * scale.y)
			, COLOR_BLUE);
	} else {
		std::cout<<"  No Cursor\n\n";
	}
	if(wd->ir.raw_valid) {
		//ir.z - How far away the wiimote is from the screen in meters
		std::cout<<"  Distance: "<<wd->ir.z<<"m\n";
		//orient.yaw - The left/right angle of the wiimote to the screen
		std::cout<<"  Yaw: "<<wd->orient.yaw<<" deg\n";
	} else {
		std::cout<<"\n\n";
	}

	// sensor bar
	scale.x = (rmode->fbWidth / 2.0) / 4.0;
	scale.y = (rmode->xfbHeight / 2.0) / 4.0;
	pos.x = rmode->fbWidth / 2.0 / 16.0;
	pos.y = rmode->xfbHeight / 2.0 / 32.0;

	if(wd->ir.raw_valid) {
		cout << "Sensor bar dots\n";

		for(i=0; i<2; i++) {

			cout << StringFromFormat(
				"  %.2f, %.2f\n"
				, ((wd->ir.sensorbar.rot_dots[i].x + 2) * scale.x) + pos.x
				, ((wd->ir.sensorbar.rot_dots[i].y + 2) * scale.y) + pos.y

				);

			draw_rectangle(screen_buffer, rmode, 8, 8
				, s32(((wd->ir.sensorbar.rot_dots[i].x + 2) * scale.x) + pos.x)
				, s32(((wd->ir.sensorbar.rot_dots[i].y + 2) * scale.y) + pos.y)
				, COLOR_GREEN);
		}
	}

	std::cout<<"  Accel:\n";
	//accel.x/accel.y/accel.z - analog values for the accelleration of the wiimote
	//(Note: Gravity pulls downwards, so even if the wiimote is not moving,
	//one(or more) axis will have a reading as if it is moving "upwards")
	std::cout<<"   XYZ: "<<wd->accel.x<<","<<wd->accel.y<<","<<wd->accel.z<<"\n";
	//orient.pitch - how far the wiimote is "tilted" in degrees
	std::cout<<"   Pitch: "<<wd->orient.pitch<<"\n";
	//orient.roll - how far the wiimote is "twisted" in degrees (uses accelerometer)
	std::cout<<"   Roll:  "<<wd->orient.roll<<"\n";

	print_wiimote_buttons(wd);

	if(wd->btns_h & WPAD_BUTTON_1) doreload=1;

	if (wd->exp.type == EXP_NUNCHUK)
	{
		joystick_t js = wd->exp.nunchuk.js;

		for (int i = 0; i < 2; i++) {
			double p = (double)*(&js.pos.x + i);
			ubyte max = *(&js.max.x + i);
			ubyte min = *(&js.min.x + i);
			ubyte center = *(&js.center.x + i);

			if (p < center)
				p = center - double(abs(p - center)) * (double(abs(0 - center)) / double(abs(min - center)));
			else if (p > center)
				p = double(abs(p - center)) * (double(abs(UCHAR_MAX - center)) / double(abs(max - center))) + center;
			else
				p = center;

			*(&js.pos.x + i) = u8(Common::trim8(p));
		}

		cout << StringFromFormat(
			"Nunchuk\n"
			"  Stick: %3u %3u [%u , %u , %u], %3u %3u [%u , %u , %u]\n"
				, wd->exp.nunchuk.js.pos.x
				, js.pos.x
				, wd->exp.nunchuk.js.min.x
				, wd->exp.nunchuk.js.center.x
				, wd->exp.nunchuk.js.max.x

				, wd->exp.nunchuk.js.pos.y
				, js.pos.y
				, wd->exp.nunchuk.js.min.y
				, wd->exp.nunchuk.js.center.x
				, wd->exp.nunchuk.js.max.y
				);

		visual_stick_box(rmode, screen_buffer, s_pos, s_pos.x_of);
		visual_stick(rmode, screen_buffer
			, s32(wd->exp.nunchuk.js.pos.x - 128)
			, s32(wd->exp.nunchuk.js.pos.y - 128)
			, s_pos, s_pos.x_of, COLOR_GRAY);
		visual_stick(rmode, screen_buffer
			, s32(js.pos.x - 128)
			, s32(js.pos.y - 128)
			, s_pos, s_pos.x_of, COLOR_RED);
	}
}

int main(int argc, char **argv) {
	void *xfb[2];
	u32 type;
	int fbi = 0;

	VIDEO_Init();
	PAD_Init();
	WPAD_Init();

	rmode = VIDEO_GetPreferredMode(NULL);

	visual_stick_init(rmode, s_pos);

	// double buffering, prevents flickering (is it needed for LCD TV? i don't have one to test)
	xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	SYS_SetResetCallback(reload);
	SYS_SetPowerCallback(shutdown);

	WPAD_SetDataFormat(0, WPAD_FMT_BTNS_ACC_IR);
	WPAD_SetVRes(0, rmode->fbWidth, rmode->xfbHeight);

	while(!doreload && !dooff) {
		CON_Init(xfb[fbi],0,0,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
		//VIDEO_ClearFrameBuffer(rmode,xfb[fbi],COLOR_BLACK);
		std::cout<<"\n\n\n";
		WPAD_ReadPending(WPAD_CHAN_ALL, countevs);
		int wiimote_connection_status = WPAD_Probe(0, &type);
		print_wiimote_connection_status(wiimote_connection_status);

		std::cout<<"  Event count: "<<evctr<<"\n";
		if(wiimote_connection_status == WPAD_ERR_NONE) {
			print_and_draw_wiimote_data(xfb[fbi]);
		}
		VIDEO_SetNextFramebuffer(xfb[fbi]);
		VIDEO_Flush();
		VIDEO_WaitVSync();
		fbi ^= 1;
	}
	if(doreload) return 0;
	if(dooff) SYS_ResetSystem(SYS_SHUTDOWN,0,0);

	return 0;
}
