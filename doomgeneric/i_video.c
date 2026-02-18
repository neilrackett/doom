// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff for X11, UNIX.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include "config.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_event.h"
#include "d_main.h"
#include "i_video.h"
#include "i_swap.h"
#include "i_system.h"
#include "z_zone.h"
#include "doomstat.h"
#include "hu_stuff.h"
#include "r_main.h"
#include "r_state.h"
#include "st_stuff.h"

#include "tables.h"
#include "doomkeys.h"

#include "doomgeneric.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>

#include <stdarg.h>

#include <sys/types.h>

//#define CMAP256

struct FB_BitField
{
	uint32_t offset;			/* beginning of bitfield	*/
	uint32_t length;			/* length of bitfield		*/
};

struct FB_ScreenInfo
{
	uint32_t xres;			/* visible resolution		*/
	uint32_t yres;
	uint32_t xres_virtual;		/* virtual resolution		*/
	uint32_t yres_virtual;

	uint32_t bits_per_pixel;		/* guess what			*/
	
							/* >1 = FOURCC			*/
	struct FB_BitField red;		/* bitfield in s_Fb mem if true color, */
	struct FB_BitField green;	/* else only length is significant */
	struct FB_BitField blue;
	struct FB_BitField transp;	/* transparency			*/
};

static struct FB_ScreenInfo s_Fb;
int fb_scaling = 1;
static int fb_scaling_forced = 0;
int usemouse = 0;


#ifdef CMAP256

boolean palette_changed;
struct color colors[256];

#else  // CMAP256

static struct color colors[256];


#endif  // CMAP256


void I_GetEvent(void);

// The screen buffer; this is modified to draw things to the screen

byte *I_VideoBuffer = NULL;
int screenwidth = DOOM_BASE_WIDTH;
int screenheight = DOOM_BASE_HEIGHT;

// If true, game is running as a screensaver

boolean screensaver_mode = false;

// Flag indicating whether the screen is currently visible:
// when the screen isnt visible, don't render the screen

boolean screenvisible;

// Mouse acceleration
//
// This emulates some of the behavior of DOS mouse drivers by increasing
// the speed when the mouse is moved fast.
//
// The mouse input values are input directly to the game, but when
// the values exceed the value of mouse_threshold, they are multiplied
// by mouse_acceleration to increase the speed.

float mouse_acceleration = 2.0;
int mouse_threshold = 10;

// Gamma correction level to use

int usegamma = 0;

typedef struct
{
	byte r;
	byte g;
	byte b;
} col_t;

// Palette converted to RGB565

static uint16_t rgb565_palette[256];

#ifdef __EMSCRIPTEN__
#define DG_OVERLAY_TRANSPARENT_INDEX 255
#endif

#ifdef __EMSCRIPTEN__
void DG_PollResize(void);
void DG_GetScreenBufferSize(int* width, int* height);
extern boolean setsizeneeded;
int dg_emscripten_view_width = DOOM_BASE_WIDTH;
int dg_emscripten_view_height = DOOM_BASE_HEIGHT;
static byte *dg_hud_overlay_buffer = NULL;
static boolean dg_hud_overlay_ready = false;
static byte *dg_message_overlay_buffer = NULL;
static boolean dg_message_overlay_ready = false;
static byte *dg_menu_overlay_buffer = NULL;
static boolean dg_menu_overlay_ready = false;
static int dg_overlay_capture_mode = DG_OVERLAY_CAPTURE_NONE;

int DG_GetOverlayCaptureMode(void)
{
    return dg_overlay_capture_mode;
}

static int DG_BeginOverlayCapture(byte **overlay_buffer, boolean *overlay_ready,
                                  int overlay_mode)
{
    size_t overlay_size = (size_t) SCREENWIDTH * (size_t) SCREENHEIGHT;

    if (*overlay_buffer == NULL)
    {
        *overlay_buffer = malloc(overlay_size);
    }

    if (*overlay_buffer == NULL)
    {
        *overlay_ready = false;
        return 0;
    }

    memset(*overlay_buffer, DG_OVERLAY_TRANSPARENT_INDEX, overlay_size);
    *overlay_ready = true;
    dg_overlay_capture_mode = overlay_mode;
    V_UseBuffer(*overlay_buffer);

    return 1;
}

static void DG_EndOverlayCapture(void)
{
    V_RestoreBuffer();
    dg_overlay_capture_mode = DG_OVERLAY_CAPTURE_NONE;
}

void DG_HudOverlayClear(void)
{
    dg_hud_overlay_ready = false;
}

int DG_HudOverlayBeginCapture(void)
{
    return DG_BeginOverlayCapture(&dg_hud_overlay_buffer, &dg_hud_overlay_ready,
                                  DG_OVERLAY_CAPTURE_HUD);
}

void DG_HudOverlayEndCapture(void)
{
    DG_EndOverlayCapture();
}

void DG_MessageOverlayClear(void)
{
    dg_message_overlay_ready = false;
}

int DG_MessageOverlayBeginCapture(void)
{
    return DG_BeginOverlayCapture(&dg_message_overlay_buffer, &dg_message_overlay_ready,
                                  DG_OVERLAY_CAPTURE_MESSAGE);
}

void DG_MessageOverlayEndCapture(void)
{
    DG_EndOverlayCapture();
}

void DG_MenuOverlayClear(void)
{
    dg_menu_overlay_ready = false;
}

int DG_MenuOverlayBeginCapture(void)
{
    return DG_BeginOverlayCapture(&dg_menu_overlay_buffer, &dg_menu_overlay_ready,
                                  DG_OVERLAY_CAPTURE_MENU);
}

void DG_MenuOverlayEndCapture(void)
{
    DG_EndOverlayCapture();
}

static int DG_FindOverlayBounds(byte *overlay,
                                int *out_x,
                                int *out_y,
                                int *out_w,
                                int *out_h)
{
    int x;
    int y;
    int min_x = SCREENWIDTH;
    int min_y = SCREENHEIGHT;
    int max_x = -1;
    int max_y = -1;

    for (y = 0; y < SCREENHEIGHT; ++y)
    {
        byte *row = overlay + y * SCREENWIDTH;

        for (x = 0; x < SCREENWIDTH; ++x)
        {
            if (row[x] != DG_OVERLAY_TRANSPARENT_INDEX)
            {
                if (x < min_x)
                {
                    min_x = x;
                }
                if (x > max_x)
                {
                    max_x = x;
                }
                if (y < min_y)
                {
                    min_y = y;
                }
                if (y > max_y)
                {
                    max_y = y;
                }
            }
        }
    }

    if (max_x < min_x || max_y < min_y)
    {
        return 0;
    }

    *out_x = min_x;
    *out_y = min_y;
    *out_w = max_x - min_x + 1;
    *out_h = max_y - min_y + 1;

    return 1;
}

static void I_UpdateFramebufferSize(void)
{
    int width = (int)s_Fb.xres;
    int height = (int)s_Fb.yres;

    DG_PollResize();
    DG_GetScreenBufferSize(&width, &height);

    if (width < DOOM_BASE_WIDTH)
    {
        width = DOOM_BASE_WIDTH;
    }
    if (height < DOOM_BASE_HEIGHT)
    {
        height = DOOM_BASE_HEIGHT;
    }
    if (width > DOOM_MAX_WIDTH)
    {
        width = DOOM_MAX_WIDTH;
    }
    if (height > DOOM_MAX_HEIGHT)
    {
        height = DOOM_MAX_HEIGHT;
    }

    if (width != screenwidth || height != screenheight)
    {
        size_t buffer_size = (size_t) width * (size_t) height;
        byte *resized = (byte *) realloc(I_VideoBuffer, buffer_size);

        if (resized != NULL)
        {
            I_VideoBuffer = resized;
            memset(I_VideoBuffer, 0, buffer_size);
            screenwidth = width;
            screenheight = height;
            setsizeneeded = true;

            if (dg_hud_overlay_buffer != NULL)
            {
                free(dg_hud_overlay_buffer);
                dg_hud_overlay_buffer = NULL;
            }
            dg_hud_overlay_ready = false;

            if (dg_message_overlay_buffer != NULL)
            {
                free(dg_message_overlay_buffer);
                dg_message_overlay_buffer = NULL;
            }
            dg_message_overlay_ready = false;

            if (dg_menu_overlay_buffer != NULL)
            {
                free(dg_menu_overlay_buffer);
                dg_menu_overlay_buffer = NULL;
            }
            dg_menu_overlay_ready = false;
        }
        else
        {
            // Keep previous internal size if host memory resize fails.
            width = screenwidth;
            height = screenheight;
        }
    }

    s_Fb.xres = width;
    s_Fb.yres = height;
    s_Fb.xres_virtual = width;
    s_Fb.yres_virtual = height;

    // Keep gameplay viewport matched to the software buffer size.
    dg_emscripten_view_width = screenwidth;
    dg_emscripten_view_height = screenheight;
}
#endif

static uint16_t I_MapPaletteColor16(uint8_t palette_index)
{
    struct color c = colors[palette_index];
    uint16_t p = ((c.r & 0xF8) << 8)
               | ((c.g & 0xFC) << 3)
               | (c.b >> 3);
#ifdef SYS_BIG_ENDIAN
    p = swapLE16(p);
#endif
    return p;
}

static uint32_t I_MapPaletteColor32(uint8_t palette_index)
{
    struct color c = colors[palette_index];
    uint32_t pix = (c.r << s_Fb.red.offset)
                 | (c.g << s_Fb.green.offset)
                 | (c.b << s_Fb.blue.offset);
#ifdef SYS_BIG_ENDIAN
    pix = swapLE32(pix);
#endif
    return pix;
}

void cmap_to_rgb565(uint16_t * out, uint8_t * in, int in_pixels)
{
    int i, j;
    struct color c;
    uint16_t r, g, b;

    for (i = 0; i < in_pixels; i++)
    {
        c = colors[*in]; 
        r = ((uint16_t)(c.r >> 3)) << 11;
        g = ((uint16_t)(c.g >> 2)) << 5;
        b = ((uint16_t)(c.b >> 3)) << 0;
        *out = (r | g | b);

        in++;
        for (j = 0; j < fb_scaling; j++) {
            out++;
        }
    }
}

void cmap_to_fb(uint8_t *out, uint8_t *in, int in_pixels)
{
    int i, k;
    struct color c;
    uint32_t pix;

    for (i = 0; i < in_pixels; i++)
    {
        c = colors[*in];  // R:8 G:8 B:8

        if (s_Fb.bits_per_pixel == 16)
        {
            // RGB565 packing
            uint16_t p = ((c.r & 0xF8) << 8) |
                         ((c.g & 0xFC) << 3) |
                         (c.b >> 3);

#ifdef SYS_BIG_ENDIAN
            p = swapLE16(p); // can't use SHORT() because this needs to stay unsigned
#endif
            for (k = 0; k < fb_scaling; k++) {
                *(uint16_t *)out = p;
                out += 2;
            }
        }
        else if (s_Fb.bits_per_pixel == 32)
        {
            // Assuming RGBA8888
            pix = (c.r << s_Fb.red.offset) |
                  (c.g << s_Fb.green.offset) |
                  (c.b << s_Fb.blue.offset);

#ifdef SYS_BIG_ENDIAN
            pix = swapLE32(pix);
#endif
            for (k = 0; k < fb_scaling; k++) {
                *(uint32_t *)out = pix;
                out += 4;
            }
        }
        else {
            // no clue how to convert this
            I_Error("No idea how to convert %d bpp pixels", s_Fb.bits_per_pixel);
        }

        in++;
    }
}

void I_InitGraphics (void)
{
    int i, gfxmodeparm;
    int fb_width;
    int fb_height;
    char *mode;

    fb_width = DOOMGENERIC_RESX;
    fb_height = DOOMGENERIC_RESY;
#ifdef __EMSCRIPTEN__
    DG_GetScreenBufferSize(&fb_width, &fb_height);
#endif
    if (fb_width < DOOM_BASE_WIDTH)
    {
        fb_width = DOOM_BASE_WIDTH;
    }
    if (fb_height < DOOM_BASE_HEIGHT)
    {
        fb_height = DOOM_BASE_HEIGHT;
    }
    if (fb_width > DOOM_MAX_WIDTH)
    {
        fb_width = DOOM_MAX_WIDTH;
    }
    if (fb_height > DOOM_MAX_HEIGHT)
    {
        fb_height = DOOM_MAX_HEIGHT;
    }

    screenwidth = fb_width;
    screenheight = fb_height;
#ifdef __EMSCRIPTEN__
    dg_emscripten_view_width = screenwidth;
    dg_emscripten_view_height = screenheight;
#endif

	memset(&s_Fb, 0, sizeof(struct FB_ScreenInfo));
	s_Fb.xres = fb_width;
	s_Fb.yres = fb_height;
	s_Fb.xres_virtual = s_Fb.xres;
	s_Fb.yres_virtual = s_Fb.yres;

#ifdef CMAP256

	s_Fb.bits_per_pixel = 8;

#else  // CMAP256

	gfxmodeparm = M_CheckParmWithArgs("-gfxmode", 1);

	if (gfxmodeparm) {
		mode = myargv[gfxmodeparm + 1];
	}
	else {
		// default to rgba8888 like the old behavior, for compatibility
		// maybe could warn here?
		mode = "rgba8888";
	}

	if (strcmp(mode, "rgba8888") == 0) {
		// default mode
		s_Fb.bits_per_pixel = 32;

		s_Fb.blue.length = 8;
		s_Fb.green.length = 8;
		s_Fb.red.length = 8;
		s_Fb.transp.length = 8;

		s_Fb.blue.offset = 0;
		s_Fb.green.offset = 8;
		s_Fb.red.offset = 16;
		s_Fb.transp.offset = 24;
	}

	else if (strcmp(mode, "rgb565") == 0) {
		s_Fb.bits_per_pixel = 16;

		s_Fb.blue.length = 5;
		s_Fb.green.length = 6;
		s_Fb.red.length = 5;
		s_Fb.transp.length = 0;

		s_Fb.blue.offset = 11;
		s_Fb.green.offset = 5;
		s_Fb.red.offset = 0;
		s_Fb.transp.offset = 16;
	}
	else
		I_Error("Unknown gfxmode value: %s\n", mode);


#endif  // CMAP256

    printf("I_InitGraphics: framebuffer: x_res: %d, y_res: %d, x_virtual: %d, y_virtual: %d, bpp: %d\n",
            s_Fb.xres, s_Fb.yres, s_Fb.xres_virtual, s_Fb.yres_virtual, s_Fb.bits_per_pixel);

    printf("I_InitGraphics: framebuffer: RGBA: %d%d%d%d, red_off: %d, green_off: %d, blue_off: %d, transp_off: %d\n",
            s_Fb.red.length, s_Fb.green.length, s_Fb.blue.length, s_Fb.transp.length, s_Fb.red.offset, s_Fb.green.offset, s_Fb.blue.offset, s_Fb.transp.offset);

    printf("I_InitGraphics: DOOM screen size: w x h: %d x %d\n", screenwidth, screenheight);


    i = M_CheckParmWithArgs("-scaling", 1);
    if (i > 0) {
        i = atoi(myargv[i + 1]);
        if (i < 1) {
            i = 1;
        }
        fb_scaling = i;
        fb_scaling_forced = 1;
        printf("I_InitGraphics: Fixed scaling factor: %d\n", fb_scaling);
    } else {
        fb_scaling_forced = 0;
        printf("I_InitGraphics: Auto-scaling enabled (aspect-preserving)\n");
    }


    /* Allocate screen to draw to */
	I_VideoBuffer = (byte*)malloc((size_t) screenwidth * (size_t) screenheight);  // For DOOM to draw on
    if (I_VideoBuffer == NULL)
    {
        I_Error("I_InitGraphics: failed to allocate screen buffer (%d x %d)",
                screenwidth, screenheight);
    }

	screenvisible = true;

    extern void I_InitInput(void);
    I_InitInput();
}

void I_ShutdownGraphics (void)
{
	free(I_VideoBuffer);
    I_VideoBuffer = NULL;

#ifdef __EMSCRIPTEN__
    if (dg_hud_overlay_buffer != NULL)
    {
        free(dg_hud_overlay_buffer);
        dg_hud_overlay_buffer = NULL;
    }
    dg_hud_overlay_ready = false;

    if (dg_message_overlay_buffer != NULL)
    {
        free(dg_message_overlay_buffer);
        dg_message_overlay_buffer = NULL;
    }
    dg_message_overlay_ready = false;

    if (dg_menu_overlay_buffer != NULL)
    {
        free(dg_menu_overlay_buffer);
        dg_menu_overlay_buffer = NULL;
    }
    dg_menu_overlay_ready = false;
#endif
}

void I_StartFrame (void)
{
#ifdef __EMSCRIPTEN__
    // Apply size changes before game rendering starts for this frame.
    I_UpdateFramebufferSize();
#endif
}

void I_StartTic (void)
{
	I_GetEvent();
}

void I_UpdateNoBlit (void)
{
}

//
// I_FinishUpdate
//

static void I_ComputeTargetSize(int* target_width, int* target_height)
{
#ifdef __EMSCRIPTEN__
    *target_width = (int)s_Fb.xres;
    *target_height = (int)s_Fb.yres;

    if (*target_width < 1)
    {
        *target_width = 1;
    }
    if (*target_height < 1)
    {
        *target_height = 1;
    }
#else
    if (fb_scaling_forced)
    {
        *target_width = SCREENWIDTH * fb_scaling;
        *target_height = SCREENHEIGHT * fb_scaling;
    }
    else if ((uint64_t)s_Fb.xres * SCREENHEIGHT <= (uint64_t)s_Fb.yres * SCREENWIDTH)
    {
        *target_width = (int)s_Fb.xres;
        *target_height = (int)(((uint64_t)(*target_width) * SCREENHEIGHT) / SCREENWIDTH);
    }
    else
    {
        *target_height = (int)s_Fb.yres;
        *target_width = (int)(((uint64_t)(*target_height) * SCREENWIDTH) / SCREENHEIGHT);
    }

    if (*target_width < 1)
    {
        *target_width = 1;
    }
    if (*target_height < 1)
    {
        *target_height = 1;
    }

    if (*target_width > (int)s_Fb.xres || *target_height > (int)s_Fb.yres)
    {
        if ((uint64_t)s_Fb.xres * SCREENHEIGHT <= (uint64_t)s_Fb.yres * SCREENWIDTH)
        {
            *target_width = (int)s_Fb.xres;
            *target_height = (int)(((uint64_t)(*target_width) * SCREENHEIGHT) / SCREENWIDTH);
        }
        else
        {
            *target_height = (int)s_Fb.yres;
            *target_width = (int)(((uint64_t)(*target_height) * SCREENWIDTH) / SCREENHEIGHT);
        }
    }
#endif
}

void I_FinishUpdate (void)
{
    int y;
    int target_width;
    int target_height;
    int x_offset;
    int y_offset;
    int scene_source_x;
    int scene_source_y;
    int scene_source_width;
    int scene_source_height;
    int bytes_per_pixel;
    byte *framebuffer;
#ifdef __EMSCRIPTEN__
    boolean draw_hud_overlay;
    boolean draw_message_overlay;
    boolean draw_menu_overlay;
#endif

    bytes_per_pixel = s_Fb.bits_per_pixel / 8;
    if (bytes_per_pixel <= 0)
    {
        return;
    }

    I_ComputeTargetSize(&target_width, &target_height);

#ifdef __EMSCRIPTEN__
    x_offset = 0;
    y_offset = 0;
#else
    x_offset = ((int)s_Fb.xres - target_width) / 2;
    y_offset = ((int)s_Fb.yres - target_height) / 2;
#endif

#ifdef __EMSCRIPTEN__
    draw_hud_overlay = (gamestate == GS_LEVEL) && dg_hud_overlay_ready;
    draw_message_overlay = (gamestate == GS_LEVEL) && dg_message_overlay_ready;
    draw_menu_overlay = dg_menu_overlay_ready;
    scene_source_x = 0;
    scene_source_y = 0;
    scene_source_width = SCREENWIDTH;
    scene_source_height = SCREENHEIGHT;
#else
    scene_source_x = 0;
    scene_source_y = 0;
    scene_source_width = SCREENWIDTH;
    scene_source_height = SCREENHEIGHT;
#endif

    framebuffer = (byte *)DG_ScreenBuffer;
    memset(framebuffer, 0, (size_t)s_Fb.xres * (size_t)s_Fb.yres * (size_t)bytes_per_pixel);

    for (y = 0; y < target_height; ++y)
    {
        int x;
        int src_y = scene_source_y + (y * scene_source_height) / target_height;
        byte *line_in = I_VideoBuffer + src_y * SCREENWIDTH + scene_source_x;
        byte *line_out = framebuffer
                       + (((size_t)(y + y_offset) * s_Fb.xres) + (size_t)x_offset) * bytes_per_pixel;

#ifdef CMAP256
        for (x = 0; x < target_width; ++x)
        {
            int src_x = (x * scene_source_width) / target_width;
            line_out[x] = line_in[src_x];
        }
#else
        if (s_Fb.bits_per_pixel == 16)
        {
            uint16_t *line_out16 = (uint16_t *)line_out;

            for (x = 0; x < target_width; ++x)
            {
                int src_x = (x * scene_source_width) / target_width;
                line_out16[x] = I_MapPaletteColor16(line_in[src_x]);
            }
        }
        else if (s_Fb.bits_per_pixel == 32)
        {
            uint32_t *line_out32 = (uint32_t *)line_out;

            for (x = 0; x < target_width; ++x)
            {
                int src_x = (x * scene_source_width) / target_width;
                line_out32[x] = I_MapPaletteColor32(line_in[src_x]);
            }
        }
        else
        {
            I_Error("No idea how to convert %d bpp pixels", s_Fb.bits_per_pixel);
        }
#endif
    }

#ifdef __EMSCRIPTEN__
    if (draw_message_overlay)
    {
        int y_msg;
        int message_dest_x;
        int message_dest_y = ST_HEIGHT + 4;
        int message_src_x = 0;
        int message_src_y = 0;
        int message_copy_width = 0;
        int message_copy_height = 0;

        if (!DG_FindOverlayBounds(dg_message_overlay_buffer,
                                  &message_src_x,
                                  &message_src_y,
                                  &message_copy_width,
                                  &message_copy_height))
        {
            message_copy_width = 0;
        }

        message_dest_x = (target_width - message_copy_width) / 2;

        if (message_dest_x < 0)
        {
            int delta = -message_dest_x;
            message_dest_x = 0;
            message_src_x += delta;
            message_copy_width -= delta;
        }
        if (message_dest_y < 0)
        {
            int delta = -message_dest_y;
            message_dest_y = 0;
            message_src_y += delta;
            message_copy_height -= delta;
        }

        if (message_dest_x + message_copy_width > target_width)
        {
            message_copy_width = target_width - message_dest_x;
        }
        if (message_dest_y + message_copy_height > target_height)
        {
            message_copy_height = target_height - message_dest_y;
        }

        if (message_copy_width > 0 && message_copy_height > 0)
        {
            for (y_msg = 0; y_msg < message_copy_height; ++y_msg)
            {
                byte *line_in = dg_message_overlay_buffer
                              + (message_src_y + y_msg) * SCREENWIDTH
                              + message_src_x;
                byte *line_out = framebuffer
                               + (((size_t)(message_dest_y + y_msg) * s_Fb.xres)
                               + (size_t)message_dest_x) * bytes_per_pixel;
                int x_msg;

#ifdef CMAP256
                for (x_msg = 0; x_msg < message_copy_width; ++x_msg)
                {
                    if (line_in[x_msg] != DG_OVERLAY_TRANSPARENT_INDEX)
                    {
                        line_out[x_msg] = line_in[x_msg];
                    }
                }
#else
                if (s_Fb.bits_per_pixel == 16)
                {
                    uint16_t *line_out16 = (uint16_t *)line_out;

                    for (x_msg = 0; x_msg < message_copy_width; ++x_msg)
                    {
                        if (line_in[x_msg] != DG_OVERLAY_TRANSPARENT_INDEX)
                        {
                            line_out16[x_msg] = I_MapPaletteColor16(line_in[x_msg]);
                        }
                    }
                }
                else if (s_Fb.bits_per_pixel == 32)
                {
                    uint32_t *line_out32 = (uint32_t *)line_out;

                    for (x_msg = 0; x_msg < message_copy_width; ++x_msg)
                    {
                        if (line_in[x_msg] != DG_OVERLAY_TRANSPARENT_INDEX)
                        {
                            line_out32[x_msg] = I_MapPaletteColor32(line_in[x_msg]);
                        }
                    }
                }
#endif
            }
        }
    }

    if (draw_hud_overlay)
    {
        int hud_dest_x;
        int hud_dest_y;
        int hud_src_x;
        int hud_copy_width;
        int hud_copy_height;

        hud_dest_x = (target_width - ST_WIDTH) / 2;
        hud_dest_y = 0;
        hud_src_x = 0;
        hud_copy_width = ST_WIDTH;
        hud_copy_height = ST_HEIGHT;

        if (hud_dest_x < 0)
        {
            hud_src_x = -hud_dest_x;
            hud_copy_width += hud_dest_x;
            hud_dest_x = 0;
        }

        if (hud_dest_y < 0)
        {
            int skip_rows = -hud_dest_y;
            hud_copy_height -= skip_rows;
            hud_dest_y = 0;
        }

        if (hud_dest_x + hud_copy_width > target_width)
        {
            hud_copy_width = target_width - hud_dest_x;
        }
        if (hud_dest_y + hud_copy_height > target_height)
        {
            hud_copy_height = target_height - hud_dest_y;
        }

        if (hud_copy_width > 0 && hud_copy_height > 0)
        {
            int y_hud;

            for (y_hud = 0; y_hud < hud_copy_height; ++y_hud)
            {
                int src_y = ST_Y + y_hud;
                byte *line_in = dg_hud_overlay_buffer + src_y * SCREENWIDTH + hud_src_x;
                byte *line_out = framebuffer
                               + (((size_t)(hud_dest_y + y_hud) * s_Fb.xres)
                               + (size_t)hud_dest_x) * bytes_per_pixel;
                int x_hud;

#ifdef CMAP256
                for (x_hud = 0; x_hud < hud_copy_width; ++x_hud)
                {
                    line_out[x_hud] = line_in[x_hud];
                }
#else
                if (s_Fb.bits_per_pixel == 16)
                {
                    uint16_t *line_out16 = (uint16_t *)line_out;

                    for (x_hud = 0; x_hud < hud_copy_width; ++x_hud)
                    {
                        line_out16[x_hud] = I_MapPaletteColor16(line_in[x_hud]);
                    }
                }
                else if (s_Fb.bits_per_pixel == 32)
                {
                    uint32_t *line_out32 = (uint32_t *)line_out;

                    for (x_hud = 0; x_hud < hud_copy_width; ++x_hud)
                    {
                        line_out32[x_hud] = I_MapPaletteColor32(line_in[x_hud]);
                    }
                }
#endif
            }
        }
    }

    if (draw_menu_overlay)
    {
        int y_menu;
        int menu_dest_x;
        int menu_dest_y;
        int menu_src_x;
        int menu_src_y;
        int menu_copy_width;
        int menu_copy_height;

        menu_dest_x = (target_width - SCREENWIDTH) / 2;
        menu_dest_y = (target_height - SCREENHEIGHT) / 2;
        menu_src_x = 0;
        menu_src_y = 0;
        menu_copy_width = SCREENWIDTH;
        menu_copy_height = SCREENHEIGHT;

        if (menu_dest_x < 0)
        {
            int delta = -menu_dest_x;
            menu_dest_x = 0;
            menu_src_x += delta;
            menu_copy_width -= delta;
        }
        if (menu_dest_y < 0)
        {
            int delta = -menu_dest_y;
            menu_dest_y = 0;
            menu_src_y += delta;
            menu_copy_height -= delta;
        }

        if (menu_dest_x + menu_copy_width > target_width)
        {
            menu_copy_width = target_width - menu_dest_x;
        }
        if (menu_dest_y + menu_copy_height > target_height)
        {
            menu_copy_height = target_height - menu_dest_y;
        }

        if (menu_copy_width > 0 && menu_copy_height > 0)
        {
            for (y_menu = 0; y_menu < menu_copy_height; ++y_menu)
            {
                byte *line_in = dg_menu_overlay_buffer
                              + (menu_src_y + y_menu) * SCREENWIDTH
                              + menu_src_x;
                byte *line_out = framebuffer
                               + (((size_t)(menu_dest_y + y_menu) * s_Fb.xres)
                               + (size_t)menu_dest_x) * bytes_per_pixel;
                int x_menu;

#ifdef CMAP256
                for (x_menu = 0; x_menu < menu_copy_width; ++x_menu)
                {
                    if (line_in[x_menu] != DG_OVERLAY_TRANSPARENT_INDEX)
                    {
                        line_out[x_menu] = line_in[x_menu];
                    }
                }
#else
                if (s_Fb.bits_per_pixel == 16)
                {
                    uint16_t *line_out16 = (uint16_t *)line_out;

                    for (x_menu = 0; x_menu < menu_copy_width; ++x_menu)
                    {
                        if (line_in[x_menu] != DG_OVERLAY_TRANSPARENT_INDEX)
                        {
                            line_out16[x_menu] = I_MapPaletteColor16(line_in[x_menu]);
                        }
                    }
                }
                else if (s_Fb.bits_per_pixel == 32)
                {
                    uint32_t *line_out32 = (uint32_t *)line_out;

                    for (x_menu = 0; x_menu < menu_copy_width; ++x_menu)
                    {
                        if (line_in[x_menu] != DG_OVERLAY_TRANSPARENT_INDEX)
                        {
                            line_out32[x_menu] = I_MapPaletteColor32(line_in[x_menu]);
                        }
                    }
                }
#endif
            }
        }
    }
#endif

	DG_DrawFrame();
}

//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
    memcpy (scr, I_VideoBuffer, (size_t) screenwidth * (size_t) screenheight);
}

//
// I_SetPalette
//
#define GFX_RGB565(r, g, b)			((((r & 0xF8) >> 3) << 11) | (((g & 0xFC) >> 2) << 5) | ((b & 0xF8) >> 3))
#define GFX_RGB565_R(color)			((0xF800 & color) >> 11)
#define GFX_RGB565_G(color)			((0x07E0 & color) >> 5)
#define GFX_RGB565_B(color)			(0x001F & color)

void I_SetPalette (byte* palette)
{
	int i;
	//col_t* c;

	//for (i = 0; i < 256; i++)
	//{
	//	c = (col_t*)palette;

	//	rgb565_palette[i] = GFX_RGB565(gammatable[usegamma][c->r],
	//								   gammatable[usegamma][c->g],
	//								   gammatable[usegamma][c->b]);

	//	palette += 3;
	//}
    

    /* performance boost:
     * map to the right pixel format over here! */

    for (i=0; i<256; ++i ) {
        colors[i].a = 0;
        colors[i].r = gammatable[usegamma][*palette++];
        colors[i].g = gammatable[usegamma][*palette++];
        colors[i].b = gammatable[usegamma][*palette++];
    }

#ifdef CMAP256

    palette_changed = true;

#endif  // CMAP256
}

// Given an RGB value, find the closest matching palette index.

int I_GetPaletteIndex (int r, int g, int b)
{
    int best, best_diff, diff;
    int i;
    col_t color;

    printf("I_GetPaletteIndex\n");

    best = 0;
    best_diff = INT_MAX;

    for (i = 0; i < 256; ++i)
    {
    	color.r = GFX_RGB565_R(rgb565_palette[i]);
    	color.g = GFX_RGB565_G(rgb565_palette[i]);
    	color.b = GFX_RGB565_B(rgb565_palette[i]);

        diff = (r - color.r) * (r - color.r)
             + (g - color.g) * (g - color.g)
             + (b - color.b) * (b - color.b);

        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }

        if (diff == 0)
        {
            break;
        }
    }

    return best;
}

void I_BeginRead (void)
{
}

void I_EndRead (void)
{
}

void I_SetWindowTitle (char *title)
{
	DG_SetWindowTitle(title);
}

void I_GraphicsCheckCommandLine (void)
{
}

void I_SetGrabMouseCallback (grabmouse_callback_t func)
{
}

void I_EnableLoadingDisk(void)
{
}

void I_BindVideoVariables (void)
{
}

void I_DisplayFPSDots (boolean dots_on)
{
}

void I_CheckIsScreensaver (void)
{
}
