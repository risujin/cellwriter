
/*

cellwriter -- a character recognition input method
Copyright (C) 2007 Michael Levin <risujin@gmail.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/

#include <gtk/gtk.h>
#include <math.h>

/*
        Limits
*/

#define HISTORY_MAX 8
#define KEYBOARD_SIZE_MIN 480

/*
        Single instance protection
*/

typedef void (*SingleInstanceFunc)(const char *msg);

int single_instance_init(SingleInstanceFunc callback, const char *str);
void single_instance_cleanup(void);

/*
        Unicode blocks
*/

typedef struct {
        short enabled;
        const int start, end;
        const char *name;
} UnicodeBlock;

extern UnicodeBlock unicode_blocks[];

/*
        Profile
*/

extern int profile_line, profile_read_only;

const char *profile_read(void);
int profile_write(const char *str);
int profile_sync_int(int *var);
int profile_sync_short(short *var);

/*
        Window
*/

enum {
        WINDOW_UNDOCKED = 0,
        WINDOW_DOCKED_TOP,
        WINDOW_DOCKED_BOTTOM,
};

extern GtkWidget *window;
extern GtkTooltips *tooltips;
extern int window_force_show, window_force_hide, window_force_x, window_force_y,
           window_force_docked, window_struts,
           window_embedded, window_button_labels, window_show_info,
           window_docked, style_colors;

void window_create(void);
void window_sync(void);
void window_cleanup(void);
void window_show(void);
void window_hide(void);
void window_toggle(void);
void window_pack(void);
void window_update_colors(void);
void window_set_docked(int mode);
void unicode_block_toggle(int block, int on);
void blocks_sync(void);
void startup_splash_show(void);

/*
        GTK/GDK/Glib specific
*/

/* Multiply to convert RGB to GDK color */
#define COLOR_SCALE 256

/* Constants may not have been defined if GLib is not included */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((void*)0)
#endif

/* A macro used to initialize GdkColor with RGB values */
#define RGB_TO_GDKCOLOR(r, g, b) {0, (r) * 256, (g) * 256, (b) * 256 }

static inline void cairo_set_source_gdk_color(cairo_t *cairo,
                                              const GdkColor *color,
                                              double alpha)
/* Set the cairo source color from a GdkColor */
{
        cairo_set_source_rgba(cairo, color->red / 65535.,
                                     color->green / 65535.,
                                     color->blue / 65535., alpha);
}

static inline void cairo_pattern_add_gdk_color_stop(cairo_pattern_t *pattern,
                                                    double offset,
                                                    GdkColor *color,
                                                    double alpha)
/* Add a GdkColor color stop to a cairo pattern */
{
        cairo_pattern_add_color_stop_rgba(pattern, offset,
                                          color->red / 65535.,
                                          color->green / 65535.,
                                          color->blue / 65535., alpha);
}

static inline int gdk_colors_equal(GdkColor *a, GdkColor *b)
/* Check if two GdkColor structures are equal */
{
        return a->red == b->red && a->green == b->green && a->blue == b->blue;
}

void highlight_gdk_color(const GdkColor *base, GdkColor *out, double value);
void scale_gdk_color(const GdkColor *base, GdkColor *out, double value);
void shade_gdk_color(const GdkColor *base, GdkColor *out, double value);
void gdk_color_to_hsl(const GdkColor *src,
                      double *hue, double *sat, double *lit);
void hsl_to_gdk_color(GdkColor *src, double hue, double sat, double lit);

/*
        Error logging and variable argument parsing
*/

/* Function traces */
#define LOG_LEVEL_TRACE (G_LOG_LEVEL_DEBUG << 1)
#define trace(...) trace_full(__FILE__, __FUNCTION__, __VA_ARGS__)

/* Log detail level */
extern int log_level;

#ifdef _EFISTDARG_H_
char *nvav(int *plen, const char *format, va_list va);
#endif
char *nva(int *length, const char *format, ...);
char *va(const char *format, ...);
void log_errno(const char *message);
void log_print(const char *format, ...);
void trace_full(const char *file, const char *func, const char *fmt, ...);

/*
        Angles
*/

/* Size of the ANGLE data type in bytes */
#define ANGLE_SIZE 2

#if (ANGLE_SIZE == 4)

/* High-precision angle type */
typedef int ANGLE;
#define ANGLE_PI 2147483648

#elif (ANGLE_SIZE == 2)

/* Medium-precision angle type */
typedef short ANGLE;
#define ANGLE_PI 32768

#else

/* Low-precision angle type */
typedef signed char ANGLE;
#define ANGLE_PI 128

#endif

/*
        2D Vector
*/

typedef struct Vec2 {
	float x, y;
} Vec2;

static inline void vec2_set(Vec2 *dest, float x, float y)
{
	dest->x = x;
	dest->y = y;
}
#define vec2_from_coords vec2_set

static inline void vec2_copy(Vec2 *dest, const Vec2 *src)
{
	dest->x = src->x;
	dest->y = src->y;
}

static inline void vec2_sub(Vec2 *dest, const Vec2 *a, const Vec2 *b)
{
	dest->x = a->x - b->x;
	dest->y = a->y - b->y;
}

static inline void vec2_sum(Vec2 *dest, const Vec2 *a, const Vec2 *b)
{
	dest->x = a->x + b->x;
	dest->y = a->y + b->y;
}

static inline float vec2_dot(const Vec2 *a, const Vec2 *b)
{
	return a->x * b->x + a->y * b->y;
}

static inline float vec2_cross(const Vec2 *a, const Vec2 *b)
{
	return a->y * b->x - b->y * a->x;
}

static inline void vec2_scale(Vec2 *dest, const Vec2 *src, float scale)
{
	dest->x = src->x * scale;
	dest->y = src->y * scale;
}

static inline void vec2_avg(Vec2 *dest, const Vec2 *a, const Vec2 *b,
			    float scale)
{
	dest->x = a->x + (b->x - a->x) * scale;
	dest->y = a->y + (b->y - a->y) * scale;
}

static inline float vec2_square(const Vec2 *src)
{
        return src->x * src->x + src->y * src->y;
}

static inline float vec2_mag(const Vec2 *src)
{
	return sqrt(src->x * src->x + src->y * src->y);
}

static inline ANGLE vec2_angle(const Vec2 *src)
{
	return (ANGLE)(atan2f(src->y, src->x) * ANGLE_PI / M_PI + 0.5f);
}

static inline float vec2_norm(Vec2 *dest, const Vec2 *a)
{
	float mag = vec2_mag(a);
	dest->x = a->x / mag;
	dest->y = a->y / mag;
	return mag;
}

static inline void vec2_proj(Vec2 *dest, const Vec2 *a, const Vec2 *b)
{
	float dist = vec2_dot(a, b), mag = vec2_mag(b), mag2 = mag * mag;
	dest->x = dist * b->x / mag2;
	dest->y = dist * b->y / mag2;
}

static inline void vec2_from_angle(Vec2 *dest, ANGLE angle, float mag)
{
	dest->y = sinf(angle * M_PI / ANGLE_PI) * mag;
	dest->x = cosf(angle * M_PI / ANGLE_PI) * mag;
}
