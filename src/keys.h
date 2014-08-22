
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

/*
        Key events
*/

typedef struct {
        unsigned char keycode, shift;
        unsigned int keysym;
} KeyEvent;

extern int key_shifted, key_num_locked, key_caps_locked;

void key_event_new(KeyEvent *key_event, unsigned int keysym);
void key_event_free(KeyEvent *key_event);
void key_event_press(KeyEvent *key_event);
void key_event_press_force(KeyEvent *key_event);
void key_event_release(KeyEvent *key_event);
void key_event_release_force(KeyEvent *key_event);
void key_event_send_char(int unichar);
void key_event_send_enter(void);
void key_event_update_mappings(void);

/*
        Key widget
*/

/* Key flags */
#define KEY_ARROW               0x0001
#define KEY_TOGGLE_ON           0x0002
#define KEY_TOGGLE_OFF          0x0003
#define KEY_ICON_MASK           0x000f
#define KEY_STICKY              0x0010
#define KEY_SHIFT               0x0020
#define KEY_SHIFTABLE           0x0040
#define KEY_CAPS_LOCK           0x0080
#define KEY_ICON_SHIFT          0x0100
#define KEY_NUM_LOCK            0x0200
#define KEY_NUM_LOCKABLE        0x0400

typedef struct {
        char active;
        short flags;
        const char *string, *string_shift;
        unsigned int keysym, keysym_shift;
        int x, y, width, height, rotate;
        KeyEvent key_event;
} Key;

typedef struct {
        GtkWidget *drawing_area;
        GdkPixmap *pixmap;
        GdkGC *pixmap_gc;
        cairo_t *cairo;
        PangoContext *pango;
        PangoFontDescription *pango_font_desc;
        int slaved, len, max_len, x, y, width, height, active, x_range, y_range,
            min_height;
        Key keys[];
} KeyWidget;

extern int keyboard_size;

/* Create slaved or non-slaved keyboard */
KeyWidget *key_widget_new_small(GtkWidget *drawing_area);
KeyWidget *key_widget_new_full(void);

/* Functions for slaved keyboards only */
gboolean key_widget_button_press(GtkWidget *widget, GdkEventButton *event,
                                 KeyWidget *key_widget);
gboolean key_widget_button_release(GtkWidget *widget, GdkEventButton *event,
                                   KeyWidget *key_widget);
void key_widget_render(KeyWidget *key_widget);
void key_widget_configure(KeyWidget *key_widget, int x, int y,
                          int width, int height);

/* Functions to update keyboards */
int key_widget_update_colors(void);
void key_widget_cleanup(KeyWidget *key_widget);
