#ifndef _GEKKO_H_
#define _GEKKO_H_

#include <iostream>
#include <cstdio>
#include <limits.h>

#include <ogcsys.h>
#include <gccore.h>

extern int doreload, dooff;

void reload(void);
void shutdown(void);

void draw_rectangle(void *xfb, GXRModeObj *rmode, s32 w, s32 h, s32 fx, s32 fy, u32 color, bool fill = true);

// visual stick position
struct visual_stick_position {
	s32 stick_range;
	s32 box_scale;
	s32 box_size;
	s32 box_margin;
	s32 dot_size;
	s32 x_of;
	s32 x_of2;
	s32 y_of;
};

void visual_stick_init(GXRModeObj *rmode, visual_stick_position &pos);
void visual_stick_box(GXRModeObj *rmode, void *screen_buffer, visual_stick_position pos, u32 x_of);
void visual_stick(GXRModeObj *rmode, void *screen_buffer, s32 x, s32 y, visual_stick_position pos, u32 x_of, u32 color);

#endif