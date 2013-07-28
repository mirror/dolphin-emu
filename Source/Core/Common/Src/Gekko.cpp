#ifdef GEKKO

#include "Gekko.h"

//-----------------------------------------------------------------------------------
//doreload - a flag that tells the program to quit looping and exit the program to the HBChannel.
//dooff - a flag that tells the program to quit looping and properly shutdown the system.
int doreload=0, dooff=0;

//Calling the function will end the while loop and properly exit the program to the HBChannel.
void reload(void) {
	doreload=1;
}

//Calling the function will end the while loop and properly shutdown the system.
// QUESTION: why calling the shutdown function direcly here halts the console?
void shutdown(void) {
	dooff=1;
}

//Draw a square on the screen (May draw rectangles as well, I am uncertain).
//*xfb - framebuffer
//*rmode - !unsure!
//w - Width of rectangle
//h - Height of rectangle
//fx - X coordinate to draw on the screen (0-w)
//fy - Y coordinate to draw on the screen (!unsure!-h)
//color - the color of the rectangle (Examples: COLOR_YELLOW, COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_BLACK, COLOR_WHITE)
void draw_rectangle(void *xfb, GXRModeObj *rmode, s32 w, s32 h, s32 x, s32 y, u32 color, bool fill) {
	u32 *fb;
	fb = (u32*)xfb;

	h *= VI_DISPLAY_PIX_SZ;
	y *= VI_DISPLAY_PIX_SZ;

	s32 x_max = rmode->fbWidth / VI_DISPLAY_PIX_SZ;

	if (x < 0)
		x = 0;
	if (x >= x_max)
		x = x_max;

	if (y < 0)
		y = 0;
	if (y >= rmode->xfbHeight)
		y = rmode->xfbHeight;

	if (w < 1)
		w = 1;
	if (w + x >= x_max)
		w = x_max - x;

	if (h < 1)
		h = 1;
	if (h + y >= rmode->xfbHeight)
		h = rmode->xfbHeight - y;

	s32 y_max = y + h;

	for (s32 py = y; py < y_max; py++) {
		for (s32 px = x; px < x + w; px++) {

		if (py < 0 || py >= rmode->xfbHeight
			|| px < 0 || px >= rmode->fbWidth / VI_DISPLAY_PIX_SZ)
			printf("buffer overflow coordinate: %d %d", px, py);

			if (fill)
				fb[rmode->fbWidth / VI_DISPLAY_PIX_SZ * py + px] = color;
			else
				if (
					py == y
					|| px == x
					|| py == y_max - 1
					|| px == x + w - 1
					)
					fb[rmode->fbWidth / VI_DISPLAY_PIX_SZ * py + px] = color;
		}
	}
}

void visual_stick_init(GXRModeObj *rmode, visual_stick_position &pos) {
	pos.stick_range = 256;
	pos.dot_size = 2;

	pos.box_scale = 4;
	pos.box_size = pos.stick_range / pos.box_scale;
	pos.box_margin = 10;

	pos.x_of = rmode->fbWidth / 2 - pos.box_size;
	pos.x_of2 = pos.x_of - pos.box_size - pos.box_margin;
	pos.y_of = rmode->xfbHeight / 2 - pos.box_size;
}

void visual_stick_box(GXRModeObj *rmode, void *screen_buffer, visual_stick_position pos, u32 x_of) {
	draw_rectangle(screen_buffer, rmode
		, (pos.stick_range) / pos.box_scale + pos.dot_size * 1.5
		, (pos.stick_range) / pos.box_scale + pos.dot_size * 1.5
		, x_of - pos.dot_size * 1.5
		, pos.y_of - pos.dot_size * 1.5
		, COLOR_WHITE, false);
}

void visual_stick(GXRModeObj *rmode, void *screen_buffer, s32 x, s32 y, visual_stick_position pos, u32 x_of, u32 color) {
	draw_rectangle(screen_buffer, rmode
		, pos.dot_size, pos.dot_size
		, (x + SCHAR_MAX) / pos.box_scale - pos.dot_size + x_of
		, (-y + SCHAR_MAX) / pos.box_scale - pos.dot_size + pos.y_of
		, color);
}

#endif