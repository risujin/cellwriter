
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

#include "config.h"
#include "common.h"
#include "recognize.h"
#include "keys.h"
#include <string.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <gdk/gdkx.h>

/*
        Keys
*/

GdkColor custom_key_color = RGB_TO_GDKCOLOR(103, 107, 120);
static GdkColor color_text, color_bg, color_keys_dark, color_keys,
                color_keys_on;

static void key_coord(KeyWidget *key_widget, int x, int y,
                      int *x_out, int *y_out)
{
        *x_out = (x * key_widget->width + key_widget->x_range / 2) /
                 key_widget->x_range + key_widget->x;
        *y_out = (y * key_widget->height + key_widget->y_range / 2) /
                 key_widget->y_range + key_widget->y;
}

static void key_coords(KeyWidget *key_widget, Key *key, int *x, int *y,
                       int *width, int *height)
{
        int x2, y2;

        key_coord(key_widget, key->x, key->y, x, y);
        key_coord(key_widget, key->x + key->width, key->y + key->height,
                  &x2, &y2);
        *width = x2 - *x;
        *height = y2 - *y;
}

static void render_icon(cairo_t *cairo, double x, double y, double size,
                        int icon, int rotate, GdkColor *color)
{
        cairo_save(cairo);
        cairo_new_path(cairo);
        cairo_translate(cairo, x + size / 2., y + size / 2.);
        cairo_scale(cairo, size, size);
        cairo_rotate(cairo, rotate * M_PI / -180);
        cairo_set_source_gdk_color(cairo, color, 1.);
        cairo_set_line_width(cairo, 1 / size);
        switch (icon) {
        default:
                cairo_identity_matrix(cairo);
                return;
        case KEY_ARROW:
                cairo_move_to(cairo,  0.50,  0.00);
                cairo_line_to(cairo,  0.00, -0.50);
                cairo_line_to(cairo,  0.00, -0.15);
                cairo_line_to(cairo, -0.50, -0.15);
                cairo_line_to(cairo, -0.50,  0.15);
                cairo_line_to(cairo,  0.00,  0.15);
                cairo_line_to(cairo,  0.00,  0.50);
                cairo_line_to(cairo,  0.50,  0.00);
                cairo_fill(cairo);
                break;
        case KEY_TOGGLE_ON:
                cairo_rectangle(cairo, -0.25, -0.15, 0.5, 0.5);
                cairo_fill(cairo);
                break;
        case KEY_TOGGLE_OFF:
                cairo_rectangle(cairo, -0.25, -0.15, 0.5, 0.5);
                cairo_stroke(cairo);
                break;
        }
        cairo_restore(cairo);
}

static int is_shifted(const Key *key)
/* Determine if the key is in a shifted state */
{
        int shifted = key_shifted;

        if (!(key->flags & KEY_SHIFTABLE))
                return FALSE;
        if (g_ascii_isalpha(key->string[0]) && key_caps_locked)
                shifted = !shifted;
        if (key->flags & KEY_NUM_LOCKABLE && !key_num_locked)
                shifted = !shifted;
        return shifted;
}

static void render_key(KeyWidget *key_widget, int n, int dirty)
{
        Key *key;
        PangoLayout *layout;
        PangoRectangle ink_ext, log_ext;
        cairo_pattern_t *pattern;
        double icon_size;
        int x, y, w, h;

        key = key_widget->keys + n;
        if (key->width < 1 || key->height < 1)
                return;
        key_coords(key_widget, key, &x, &y, &w, &h);

        /* Cairo clip region */
        cairo_reset_clip(key_widget->cairo);
        cairo_rectangle(key_widget->cairo, x, y, w, h);
        cairo_clip(key_widget->cairo);

        /* Render background */
        if (!key->active &&
            (((key->flags & KEY_CAPS_LOCK) && key_caps_locked) ||
             ((key->flags & KEY_NUM_LOCK) && key_num_locked))) {
                GdkColor new_bg;

                new_bg.red = (color_keys_on.red + color_keys.red) / 2;
                new_bg.green = (color_keys_on.green + color_keys.green) / 2;
                new_bg.blue = (color_keys_on.blue + color_keys.blue) / 2;
                gdk_gc_set_rgb_fg_color(key_widget->pixmap_gc, &new_bg);
        } else
                gdk_gc_set_rgb_fg_color(key_widget->pixmap_gc,
                                        key->active ? &color_keys_on :
                                                      &color_keys);
        gdk_draw_rectangle(key_widget->pixmap, key_widget->pixmap_gc, TRUE,
                           x, y, w, h);

        /* Draw text */
        log_ext.width = 0;
        icon_size = 0;
        if ((key->flags & KEY_ICON_MASK) &&
            (!(key->flags & KEY_ICON_SHIFT) || is_shifted(key)))
                icon_size = h * 3 / 7;
        if (key->string[0]) {
                const char *string = key->string;

                if (is_shifted(key))
                        string = key->string_shift;
                layout = pango_layout_new(key_widget->pango);
                cairo_move_to(key_widget->cairo, x, y + h / 2);
                pango_layout_set_text(layout, string, -1);
                pango_layout_set_font_description(layout,
                                                  key_widget->pango_font_desc);
                pango_layout_get_pixel_extents(layout, &ink_ext, &log_ext);
                cairo_rel_move_to(key_widget->cairo, w / 2 -
                                  log_ext.width / 2 + icon_size / 2 + 1,
                                  log_ext.height / -2 + (key->active ? 2 : 1));

                /* Draw text shadow */
                cairo_set_source_gdk_color(key_widget->cairo,
                                           &color_keys_dark, 1.);
                pango_cairo_show_layout(key_widget->cairo, layout);

                /* Draw the normal text */
                cairo_rel_move_to(key_widget->cairo, -1, -1);
                cairo_set_source_gdk_color(key_widget->cairo, &color_text, 1.);
                pango_cairo_show_layout(key_widget->cairo, layout);

                g_object_unref(layout);
        }

        /* Render icon */
        if (icon_size) {
                int icon_x, icon_y;

                icon_x = x + w / 2. - log_ext.width / 2. - icon_size / 2.;
                icon_y = y + h / 2. - icon_size / 2. + (key->active ? 1 : 0);
                render_icon(key_widget->cairo, icon_x + 1, icon_y + 1,
                            icon_size, key->flags & KEY_ICON_MASK,
                            key->rotate, &color_keys_dark);
                render_icon(key_widget->cairo, icon_x, icon_y, icon_size,
                            key->flags & KEY_ICON_MASK, key->rotate,
                            &color_text);
        }

        /* Render border */
        if (!key->active) {
                cairo_new_path(key_widget->cairo);
                cairo_set_line_width(key_widget->cairo, 1.);

                /* Top border */
                cairo_set_source_rgba(key_widget->cairo, 1., 1., 1., 0.5);
                cairo_move_to(key_widget->cairo, x + 0.5, y + 0.5);
                cairo_line_to(key_widget->cairo, x + w - 0.5, y + 0.5);
                cairo_stroke(key_widget->cairo);

                /* Left border */
                pattern = cairo_pattern_create_linear(x, y, x, y + h);
                cairo_pattern_add_color_stop_rgba(pattern, 0., 1., 1., 1., 0.5);
                cairo_pattern_add_color_stop_rgba(pattern, 1., 1., 1., 1., 0.);
                cairo_set_source(key_widget->cairo, pattern);
                cairo_move_to(key_widget->cairo, x + 0.5, y + 0.5);
                cairo_line_to(key_widget->cairo, x + 0.5, y + h - 0.5);
                cairo_stroke(key_widget->cairo);
                cairo_pattern_destroy(pattern);

                /* Right border */
                pattern = cairo_pattern_create_linear(x, y, x, y + h);
                cairo_pattern_add_color_stop_rgba(pattern, 0., 0., 0., 0., 0.);
                cairo_pattern_add_color_stop_rgba(pattern, 1., 0., 0., 0., 0.5);
                cairo_set_source(key_widget->cairo, pattern);
                cairo_move_to(key_widget->cairo, x + w - 0.5, y + 0.5);
                cairo_line_to(key_widget->cairo, x + w - 0.5, y + h - 0.5);
                cairo_stroke(key_widget->cairo);
                cairo_pattern_destroy(pattern);

                /* Bottom border */
                cairo_set_source_rgba(key_widget->cairo, 0., 0., 0., 0.5);
                cairo_move_to(key_widget->cairo, x + 0.5, y + h - 0.5);
                cairo_line_to(key_widget->cairo, x + w - 0.5, y + h - 0.5);
                cairo_stroke(key_widget->cairo);
        }

        /* Mark key area as dirty */
        if (dirty)
                gtk_widget_queue_draw_area(key_widget->drawing_area,
                                           x, y, w, h);
}

void key_widget_render(KeyWidget *key_widget)
{
        int i;

        if (!key_widget->pixmap_gc || !key_widget->pixmap)
                return;

        /* Render background */
        if (!key_widget->slaved) {
                gdk_gc_set_rgb_fg_color(key_widget->pixmap_gc, &color_bg);
                gdk_draw_rectangle(key_widget->pixmap, key_widget->pixmap_gc,
                                   TRUE, 0, 0,
                                   key_widget->drawing_area->allocation.width,
                                   key_widget->drawing_area->allocation.height);
        }

        /* Render keys */
        for (i = 0; i < key_widget->len; i++)
                render_key(key_widget, i, FALSE);

        /* Dirty the drawing area if we aren't slaved */
        if (!key_widget->slaved)
                gtk_widget_queue_draw(key_widget->drawing_area);
}

static Key *add_key(KeyWidget *key_widget, int keysym, const char *string,
                    int x, int y, int width, int height)
/* Adds a key to a key widget and resizes it if necessary */
{
        Key *key;

        if (key_widget->len >= key_widget->max_len) {
                g_warning("Not enough room on keyboard for key '%s'",
                          string);
                return NULL;
        }
        key = key_widget->keys + key_widget->len++;
        key->keysym = keysym;
        key->string = !string ? XKeysymToString(keysym) : string;
        key->x = x;
        key->y = y;
        key->width = width;
        key->height = height;
        if (key->x + key->width > key_widget->x_range)
                key_widget->x_range = key->x + key->width;
        if (key->y + key->height > key_widget->y_range)
                key_widget->y_range = key->y + key->height;
        if (key->height < key_widget->min_height || !key_widget->min_height)
                key_widget->min_height = key->height;

        return key;
}

static void set_flags(Key *key, int flags, int rotate)
{
        if (!key)
                return;
        key->flags = flags;
        key->rotate = rotate;
}

static void set_shifted(Key *key, unsigned int keysym, const char *string)
{
        if (!key)
                return;
        key->keysym_shift = keysym;
        key->string_shift = !string ? XKeysymToString(keysym) : string;
        key->flags |= KEY_SHIFTABLE;
}

int key_widget_update_colors(void)
{
        GdkColor old_keys, old_text;

        old_keys = color_keys;
        old_text = color_text;
        color_keys = custom_key_color;
        color_text.red = 255 * COLOR_SCALE;
        color_text.green = 255 * COLOR_SCALE;
        color_text.blue = 255 * COLOR_SCALE;
        color_bg = window->style->bg[0];
        if (style_colors) {
                color_keys = window->style->dark[3];
                //color_text = window->style->fg[3];
        }
        shade_gdk_color(&color_keys, &color_keys_dark, 0.67);
        shade_gdk_color(&color_keys, &color_keys_on, 0.75);
        return !gdk_colors_equal(&old_keys, &color_keys) ||
               !gdk_colors_equal(&old_text, &color_text);
}

void key_widget_configure(KeyWidget *key_widget, int x, int y,
                          int width, int height)
{
        key_widget->x = x;
        key_widget->y = y;
        key_widget->width = width;
        key_widget->height = height;
        key_widget_update_colors();
        pango_font_description_set_absolute_size(key_widget->pango_font_desc,
                                                 PANGO_SCALE *
                                                 key_widget->height *
                                                 key_widget->min_height /
                                                 key_widget->y_range / 2);
}

/*
        Widget
*/

int keyboard_size = 640;

static void style_set(GtkWidget *w, GtkStyle *previous_style,
                      KeyWidget *key_widget)
{
        if (key_widget_update_colors() && key_widget)
                key_widget_render(key_widget);
}

void key_widget_resize(KeyWidget *key_widget)
{
        int width, height;

        if (keyboard_size < KEYBOARD_SIZE_MIN)
                keyboard_size = KEYBOARD_SIZE_MIN;
        if (window_embedded) {
                width = key_widget->drawing_area->allocation.width;
                height = key_widget->drawing_area->allocation.height;
        } else if (window_docked) {
                GdkScreen *screen;

                screen = gdk_screen_get_default();
                width = gdk_screen_get_width(screen);
                height = width / 4;
        } else {
                width = keyboard_size;
                height = keyboard_size / 4;
        }
        if (key_widget->width == width && key_widget->height == height)
                return;
        key_widget->width = width;
        key_widget->height = height;
        gtk_widget_set_size_request(key_widget->drawing_area, width, height);
}

static gboolean configure_event(GtkWidget *widget, GdkEventConfigure *event,
                                KeyWidget *key_widget)
/* Create a new backing pixmap of the appropriate size */
{
        PangoFontMap *font_map;

        /* Backing pixmap */
        if (key_widget->pixmap) {
                int old_width, old_height;

                /* Do not update if the size has not changed */
                gdk_drawable_get_size(key_widget->pixmap,
                                      &old_width, &old_height);
                if (old_width == widget->allocation.width &&
                    old_height == widget->allocation.height)
                        return TRUE;

                g_object_unref(key_widget->pixmap);
        }
        if (widget->allocation.width <= 1)
                return TRUE;
        trace("%dx%d", widget->allocation.width, widget->allocation.height);
        key_widget->pixmap = gdk_pixmap_new(widget->window,
                                            widget->allocation.width,
                                            widget->allocation.height, -1);

        /* GDK graphics context */
        if (key_widget->pixmap_gc)
                g_object_unref(key_widget->pixmap_gc);
        key_widget->pixmap_gc = gdk_gc_new(GDK_DRAWABLE(key_widget->pixmap));

        /* Cairo context */
        if (key_widget->cairo)
                cairo_destroy(key_widget->cairo);
        key_widget->cairo = gdk_cairo_create(GDK_DRAWABLE(key_widget->pixmap));

        /* Pango context */
        if (key_widget->pango)
                g_object_unref(key_widget->pango);
        font_map = pango_cairo_font_map_new();
        key_widget->pango = pango_font_map_create_context(font_map);
        g_object_unref(font_map);

        /* Resize the widget */
        if (window_docked || window_embedded)
                key_widget_resize(key_widget);

        /* Font size is determined by smallest key */
        pango_font_description_set_absolute_size(key_widget->pango_font_desc,
                                                 PANGO_SCALE *
                                                 key_widget->height *
                                                 key_widget->min_height /
                                                 key_widget->y_range / 2);

        /* Enable leave/notify signals */
        if (key_widget->drawing_area->window) {
                GdkEventMask mask;

                mask = gdk_window_get_events(key_widget->drawing_area->window);
                gdk_window_set_events(key_widget->drawing_area->window, mask |
                                      GDK_ENTER_NOTIFY_MASK |
                                      GDK_LEAVE_NOTIFY_MASK);
        } else
                g_warning("Failed to get GdkWindow for KeyWidget");

        key_widget_update_colors();
        key_widget_render(key_widget);
        return TRUE;
}

static gboolean expose_event(GtkWidget *widget, GdkEventExpose *event,
                             KeyWidget *key_widget)
/* Redraw the drawing area from the backing pixmap */
{
        if (!key_widget->pixmap)
                return FALSE;
        gdk_draw_drawable(widget->window,
                          widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
                          key_widget->pixmap, event->area.x, event->area.y,
                          event->area.x, event->area.y, event->area.width,
                          event->area.height);
        return FALSE;
}

static int which_key(KeyWidget *key_widget, int ex, int ey)
{
        int i;

        for (i = 0; i < key_widget->len; i++) {
                Key *key = key_widget->keys + i;
                int x, y, w, h;

                key_coords(key_widget, key, &x, &y, &w, &h);
                if (ex >= x && ey >= y && ex < x + w && ey < y + h)
                        return i;
        }
        return -1;
}

static void update_shifted(KeyWidget *key_widget)
{
        int i;

        for (i = 0; i < key_widget->len; i++)
                if (key_widget->keys[i].flags & KEY_SHIFTABLE)
                        render_key(key_widget, i, TRUE);
}

static void press_sticky_keys(KeyWidget *key_widget, int on)
/* Find all active sticky keys and press or release their KeySyms */
{
        int i, old_shifted;

        old_shifted = key_shifted;
        for (i = 0; i < key_widget->len; i++) {
                Key *sticky_key = key_widget->keys + i;
                int keysym;

                if (!(sticky_key->flags & KEY_STICKY) || !sticky_key->active ||
                    ((sticky_key->flags & KEY_SHIFT) && on))
                        continue;
                keysym = sticky_key->keysym;
                if (is_shifted(sticky_key))
                        keysym = sticky_key->keysym_shift;
                if (!keysym)
                        continue;
                if (on) {
                        key_event_new(&sticky_key->key_event, keysym);
                        key_event_press(&sticky_key->key_event);
                } else {
                        key_event_new(&sticky_key->key_event, keysym);
                        key_event_release(&sticky_key->key_event);
                        key_event_free(&sticky_key->key_event);
                        sticky_key->active = FALSE;
                        render_key(key_widget, i, TRUE);
                }
        }
        if (old_shifted != key_shifted)
                update_shifted(key_widget);
}

static gboolean notify_event(GtkWidget *widget, GdkEventCrossing *event,
                             KeyWidget *key_widget)
/* Press or release modifier keys depending on pointer location */
{
        int i;

        for (i = 0; i < key_widget->len; i++) {
                Key *sticky_key = key_widget->keys + i;
                int keysym;

                if (!(sticky_key->flags & KEY_STICKY) || !sticky_key->active)
                        continue;
                keysym = sticky_key->keysym;
                if (is_shifted(sticky_key))
                        keysym = sticky_key->keysym_shift;
                if (!keysym)
                        continue;
                if (event->type == GDK_LEAVE_NOTIFY) {
                        key_event_new(&sticky_key->key_event, keysym);
                        key_event_press_force(&sticky_key->key_event);
                } else {
                        key_event_new(&sticky_key->key_event, keysym);
                        key_event_release_force(&sticky_key->key_event);
                        key_event_free(&sticky_key->key_event);
                }
        }
        return FALSE;
}

gboolean key_widget_button_press(GtkWidget *widget, GdkEventButton *event,
                                 KeyWidget *key_widget)
/* Mouse button is pressed over drawing area */
{
        Key *key;
        int index, keysym, old_shifted;

        /* Don't process double clicks */
        if (event->type != GDK_BUTTON_PRESS)
                return FALSE;

        /* Find the key */
        index = which_key(key_widget, event->x, event->y);
        if (index < 0)
                return FALSE;
        key = key_widget->keys + index;

        /* Toggle activated state */
        key->active = !key->active;
        if (key->active)
                key_widget->active = index;

        old_shifted = key_shifted;
        if (!(key->flags & KEY_STICKY) || (key->flags & KEY_SHIFT)) {

                /* Pressing a non-sticky key causes all sticky keys except
                   shifts to send their keysyms */
                if (!(key->flags & KEY_SHIFT))
                        press_sticky_keys(key_widget, TRUE);

                /* Prepare for sending key events */
                key_event_update_mappings();

                /* Press down the active key's keysym */
                keysym = key->keysym;
                if (is_shifted(key))
                        keysym = key->keysym_shift;
                if (key->active) {
                        key_event_new(&key->key_event, keysym);
                        key_event_press(&key->key_event);
                } else {
                        key_event_new(&key->key_event, keysym);
                        key_event_release(&key->key_event);
                        key_event_free(&key->key_event);
                }
        }

        /* Keep track of shifted state */
        if (old_shifted != key_shifted ||
            (key->flags & KEY_CAPS_LOCK) || (key->flags & KEY_NUM_LOCK))
                update_shifted(key_widget);

        render_key(key_widget, index, TRUE);
        return TRUE;
}

gboolean key_widget_button_release(GtkWidget *widget, GdkEventButton *event,
                                   KeyWidget *key_widget)
/* Mouse button is released over drawing area */
{
        Key *key;
        int old_shifted;

	/* Did we miss a keypress due to a server grab? */
	if (key_widget->active < 0) {
		event->type = GDK_BUTTON_PRESS;
		key_widget_button_press(widget, event, key_widget);
	}

        /* The last pressed key is the one we are releasing now */
        if (key_widget->active < 0 || key_widget->active > key_widget->len)
                return FALSE;
        key = key_widget->keys + key_widget->active;
        if (key->flags & KEY_STICKY || !key->active)
                return TRUE;
        key->active = FALSE;

        /* Send the keysym released event */
        old_shifted = key_shifted;
        key_event_release(&key->key_event);
        key_event_free(&key->key_event);

        /* Releasing a non-sticky key causes all sticky keys to be released */
        press_sticky_keys(key_widget, FALSE);

        /* Keep track of shifted state */
        if (old_shifted != key_shifted)
                update_shifted(key_widget);

        render_key(key_widget, key_widget->active, TRUE);
        key_widget->active = -1;
        return TRUE;
}

void key_widget_cleanup(KeyWidget *key_widget)
/* Turn off any active keys */
{
        int i, old_shifted;

        if (!key_widget)
                return;
        old_shifted = key_shifted;
        for (i = 0; i < key_widget->len; i++) {
                Key *key;

                key = key_widget->keys + i;
                if (!key->active)
                        continue;
                key->active = FALSE;
                key_event_release(&key->key_event);
                key_event_free(&key->key_event);
                render_key(key_widget, i, TRUE);
                g_debug("Released held key '%s'", key->string);
        }
        if (key_shifted != old_shifted)
                update_shifted(key_widget);
}

/*
        Widget
*/

static KeyWidget *key_widget_new(GtkWidget *drawing_area, int keys)
/* Unlike CellWidget, KeyWidget can be instantiated more than once */
{
        KeyWidget *key_widget;

        key_widget = g_malloc0(sizeof (*key_widget) + sizeof (Key) * keys);
        key_widget->active = -1;
        key_widget->drawing_area = drawing_area;
        key_widget->max_len = keys;

        /* Get background color */
        style_set(drawing_area, NULL, NULL);

        /* Create Pango font description
           FIXME font characteristics, not family */
        key_widget->pango_font_desc = pango_font_description_new();
        pango_font_description_set_family(key_widget->pango_font_desc, "Sans");
        pango_font_description_set_weight(key_widget->pango_font_desc,
                                          PANGO_WEIGHT_BOLD);

        return key_widget;
}

KeyWidget *key_widget_new_small(GtkWidget *drawing_area)
/* Creates a small on-screen keyboard slaved to another drawing area */
{
        KeyWidget *key_widget;
        Key *key;

        key_widget = key_widget_new(drawing_area, 16);
        key_widget->slaved = TRUE;

        /* 1st row */
        key = add_key(key_widget, XK_BackSpace, "BkSp", 1, 0, 2, 1);
        set_flags(key, KEY_ARROW, 180);
        add_key(key_widget, XK_Tab, "Tab", 0, 0, 1, 1);
        add_key(key_widget, XK_Delete, "Del", 3, 0, 1, 1);

        /* 2nd row */
        add_key(key_widget, XK_Home, "Hme", 0, 1, 1, 1);
        key = add_key(key_widget, XK_Up, "", 1, 1, 1, 1);
        set_flags(key, KEY_ARROW, 90);
        add_key(key_widget, XK_End, "End", 2, 1, 1, 1);
        add_key(key_widget, XK_Page_Up, "PUp", 3, 1, 1, 1);

        /* 3rd row */
        key = add_key(key_widget, XK_Left, "", 0, 2, 1, 1);
        set_flags(key, KEY_ARROW, 180);
        key = add_key(key_widget, XK_Down, "", 1, 2, 1, 1);
        set_flags(key, KEY_ARROW, 270);
        key = add_key(key_widget, XK_Right, "", 2, 2, 1, 1);
        set_flags(key, KEY_ARROW, 0);
        add_key(key_widget, XK_Page_Down, "PDn", 3, 2, 1, 1);

        /* Add some event hooks */
        g_signal_connect(G_OBJECT(drawing_area), "style-set",
                         G_CALLBACK(style_set), key_widget);

        return key_widget;
}

KeyWidget *key_widget_new_full(void)
/* Creates the QWERTY on-screen keyboard
   FIXME The Num Pad shifts are backwards, a shifted num lockable will be used
         when Num Lock is OFF. */
{
        GtkWidget *drawing_area;
        KeyWidget *key_widget;
        Key *key;

        /* Create drawing area */
        drawing_area = gtk_drawing_area_new();

        /* Create key widget */
        key_widget = key_widget_new(drawing_area, 104);
        key_widget_resize(key_widget);

        /* 1st row */
        add_key(key_widget, XK_Escape, "Esc", 0, 0, 3, 2);

        add_key(key_widget, XK_F1, "F1", 4, 0, 2, 2);
        add_key(key_widget, XK_F2, "F2", 6, 0, 2, 2);
        add_key(key_widget, XK_F3, "F3", 8, 0, 2, 2);
        add_key(key_widget, XK_F4, "F4", 10, 0, 2, 2);

        add_key(key_widget, XK_F5, "F5", 13, 0, 2, 2);
        add_key(key_widget, XK_F6, "F6", 15, 0, 2, 2);
        add_key(key_widget, XK_F7, "F7", 17, 0, 2, 2);
        add_key(key_widget, XK_F8, "F8", 19, 0, 2, 2);

        add_key(key_widget, XK_F9, "F9", 22, 0, 2, 2);
        add_key(key_widget, XK_F10, "F10", 24, 0, 2, 2);
        add_key(key_widget, XK_F11, "F11", 26, 0, 2, 2);
        add_key(key_widget, XK_F12, "F12", 28, 0, 2, 2);

        add_key(key_widget, XK_Print, "PSc", 31, 0, 2, 2);
        add_key(key_widget, XK_Scroll_Lock, "SLk", 33, 0, 2, 2);
        add_key(key_widget, XK_Pause, "Brk", 35, 0, 2, 2);

        /* 2nd row */
        key = add_key(key_widget, XK_grave, "`", 0, 3, 2, 2);
        set_shifted(key, XK_asciitilde, "~");
        key = add_key(key_widget, XK_1, "1", 2, 3, 2, 2);
        set_shifted(key, XK_exclam, "!");
        key = add_key(key_widget, XK_2, "2", 4, 3, 2, 2);
        set_shifted(key, XK_at, "@");
        key = add_key(key_widget, XK_3, "3", 6, 3, 2, 2);
        set_shifted(key, XK_numbersign, "#");
        key = add_key(key_widget, XK_4, "4", 8, 3, 2, 2);
        set_shifted(key, XK_dollar, "$");
        key = add_key(key_widget, XK_5, "5", 10, 3, 2, 2);
        set_shifted(key, XK_percent, "%");
        key = add_key(key_widget, XK_6, "6", 12, 3, 2, 2);
        set_shifted(key, XK_asciicircum, "^");
        key = add_key(key_widget, XK_7, "7", 14, 3, 2, 2);
        set_shifted(key, XK_ampersand, "&");
        key = add_key(key_widget, XK_8, "8", 16, 3, 2, 2);
        set_shifted(key, XK_asterisk, "*");
        key = add_key(key_widget, XK_9, "9", 18, 3, 2, 2);
        set_shifted(key, XK_parenleft, "(");
        key = add_key(key_widget, XK_0, "0", 20, 3, 2, 2);
        set_shifted(key, XK_parenright, ")");
        key = add_key(key_widget, XK_minus, "-", 22, 3, 2, 2);
        set_shifted(key, XK_underscore, "_");
        key = add_key(key_widget, XK_equal, "=", 24, 3, 2, 2);
        set_shifted(key, XK_plus, "+");
        key = add_key(key_widget, XK_BackSpace, "BkSp", 26, 3, 4, 2);
        set_flags(key, KEY_ARROW, 180);

        add_key(key_widget, XK_Insert, "Ins", 31, 3, 2, 2);
        add_key(key_widget, XK_Home, "Hm", 33, 3, 2, 2);
        add_key(key_widget, XK_Page_Up, "PU", 35, 3, 2, 2);

        key = add_key(key_widget, XK_Num_Lock, "NL", 38, 3, 2, 2);
        set_flags(key, KEY_NUM_LOCK, 0);
        add_key(key_widget, XK_KP_Divide, "/", 40, 3, 2, 2);
        add_key(key_widget, XK_KP_Multiply, "*", 42, 3, 2, 2);
        add_key(key_widget, XK_KP_Subtract, "-", 44, 3, 2, 2);

        /* 3rd row */
        add_key(key_widget, XK_Tab, "Tab", 0, 5, 3, 2);
        key = add_key(key_widget, XK_q, "q", 3, 5, 2, 2);
        set_shifted(key, XK_Q, "Q");
        key = add_key(key_widget, XK_w, "w", 5, 5, 2, 2);
        set_shifted(key, XK_W, "W");
        key = add_key(key_widget, XK_e, "e", 7, 5, 2, 2);
        set_shifted(key, XK_E, "E");
        key = add_key(key_widget, XK_r, "r", 9, 5, 2, 2);
        set_shifted(key, XK_R, "R");
        key = add_key(key_widget, XK_t, "t", 11, 5, 2, 2);
        set_shifted(key, XK_T, "T");
        key = add_key(key_widget, XK_y, "y", 13, 5, 2, 2);
        set_shifted(key, XK_Y, "Y");
        key = add_key(key_widget, XK_u, "u", 15, 5, 2, 2);
        set_shifted(key, XK_U, "U");
        key = add_key(key_widget, XK_i, "i", 17, 5, 2, 2);
        set_shifted(key, XK_I, "I");
        key = add_key(key_widget, XK_o, "o", 19, 5, 2, 2);
        set_shifted(key, XK_O, "O");
        key = add_key(key_widget, XK_p, "p", 21, 5, 2, 2);
        set_shifted(key, XK_P, "P");
        key = add_key(key_widget, XK_bracketleft, "[", 23, 5, 2, 2);
        set_shifted(key, XK_braceleft, "{");
        key = add_key(key_widget, XK_bracketright, "]", 25, 5, 2, 2);
        set_shifted(key, XK_braceright, "}");
        key = add_key(key_widget, XK_backslash, "\\", 27, 5, 3, 2);
        set_shifted(key, XK_bar, "|");

        add_key(key_widget, XK_Delete, "Del", 31, 5, 2, 2);
        add_key(key_widget, XK_End, "End", 33, 5, 2, 2);
        add_key(key_widget, XK_Page_Down, "PD", 35, 5, 2, 2);

        key = add_key(key_widget, XK_KP_7, "7", 38, 5, 2, 2);
        set_flags(key, KEY_NUM_LOCKABLE, 0);
        set_shifted(key, XK_KP_Home, "Hm");
        key = add_key(key_widget, XK_KP_8, "8", 40, 5, 2, 2);
        set_flags(key, KEY_NUM_LOCKABLE | KEY_ICON_SHIFT | KEY_ARROW, 90);
        set_shifted(key, XK_KP_Up, "");
        key = add_key(key_widget, XK_KP_9, "9", 42, 5, 2, 2);
        set_flags(key, KEY_NUM_LOCKABLE, 0);
        set_shifted(key, XK_KP_Page_Up, "PU");
        add_key(key_widget, XK_KP_Add, "+", 44, 5, 2, 4);

        /* 4th row */
        key = add_key(key_widget, XK_Caps_Lock, "CpLk", 0, 7, 4, 2);
        set_flags(key, KEY_CAPS_LOCK, 0);
        key = add_key(key_widget, XK_a, "a", 4, 7, 2, 2);
        set_shifted(key, XK_A, "A");
        key = add_key(key_widget, XK_s, "s", 6, 7, 2, 2);
        set_shifted(key, XK_S, "S");
        key = add_key(key_widget, XK_d, "d", 8, 7, 2, 2);
        set_shifted(key, XK_D, "D");
        key = add_key(key_widget, XK_f, "f", 10, 7, 2, 2);
        set_shifted(key, XK_F, "F");
        key = add_key(key_widget, XK_g, "g", 12, 7, 2, 2);
        set_shifted(key, XK_G, "G");
        key = add_key(key_widget, XK_h, "h", 14, 7, 2, 2);
        set_shifted(key, XK_H, "H");
        key = add_key(key_widget, XK_j, "j", 16, 7, 2, 2);
        set_shifted(key, XK_J, "J");
        key = add_key(key_widget, XK_k, "k", 18, 7, 2, 2);
        set_shifted(key, XK_K, "K");
        key = add_key(key_widget, XK_l, "l", 20, 7, 2, 2);
        set_shifted(key, XK_L, "L");
        key = add_key(key_widget, XK_semicolon, ";", 22, 7, 2, 2);
        set_shifted(key, XK_colon, ":");
        key = add_key(key_widget, XK_apostrophe, "'", 24, 7, 2, 2);
        set_shifted(key, XK_quotedbl, "\"");
        add_key(key_widget, XK_Return, "Enter", 26, 7, 4, 2);

        key = add_key(key_widget, XK_KP_4, "4", 38, 7, 2, 2);
        set_flags(key, KEY_NUM_LOCKABLE | KEY_ICON_SHIFT | KEY_ARROW, 180);
        set_shifted(key, XK_KP_Left, "");
        key = add_key(key_widget, XK_KP_5, "5", 40, 7, 2, 2);
        set_flags(key, KEY_NUM_LOCKABLE, 0);
        set_shifted(key, 0, "");
        key = add_key(key_widget, XK_KP_6, "6", 42, 7, 2, 2);
        set_flags(key, KEY_NUM_LOCKABLE | KEY_ICON_SHIFT | KEY_ARROW, 0);
        set_shifted(key, XK_KP_Right, "");

        /* 5th row */
        key = add_key(key_widget, XK_Shift_L, "Shift", 0, 9, 5, 2);
        set_flags(key, KEY_STICKY|KEY_SHIFT, 0);
        key = add_key(key_widget, XK_z, "z", 5, 9, 2, 2);
        set_shifted(key, XK_Z, "Z");
        key = add_key(key_widget, XK_x, "x", 7, 9, 2, 2);
        set_shifted(key, XK_X, "X");
        key = add_key(key_widget, XK_c, "c", 9, 9, 2, 2);
        set_shifted(key, XK_C, "C");
        key = add_key(key_widget, XK_v, "v", 11, 9, 2, 2);
        set_shifted(key, XK_V, "V");
        key = add_key(key_widget, XK_b, "b", 13, 9, 2, 2);
        set_shifted(key, XK_B, "B");
        key = add_key(key_widget, XK_n, "n", 15, 9, 2, 2);
        set_shifted(key, XK_N, "N");
        key = add_key(key_widget, XK_m, "m", 17, 9, 2, 2);
        set_shifted(key, XK_M, "M");
        key = add_key(key_widget, XK_comma, ",", 19, 9, 2, 2);
        set_shifted(key, XK_less, "<");
        key = add_key(key_widget, XK_period, ".", 21, 9, 2, 2);
        set_shifted(key, XK_greater, ">");
        key = add_key(key_widget, XK_slash, "/", 23, 9, 2, 2);
        set_shifted(key, XK_question, "?");
        key = add_key(key_widget, XK_Shift_R, "Shift", 25, 9, 5, 2);
        set_flags(key, KEY_STICKY|KEY_SHIFT, 0);

        key = add_key(key_widget, XK_Up, "", 33, 9, 2, 2);
        set_flags(key, KEY_ARROW, 90);

        key = add_key(key_widget, XK_KP_1, "1", 38, 9, 2, 2);
        set_flags(key, KEY_NUM_LOCKABLE, 0);
        set_shifted(key, XK_KP_End, "End");
        key = add_key(key_widget, XK_KP_2, "2", 40, 9, 2, 2);
        set_flags(key, KEY_NUM_LOCKABLE | KEY_ICON_SHIFT | KEY_ARROW, 270);
        set_shifted(key, XK_KP_Down, "");
        key = add_key(key_widget, XK_KP_3, "3", 42, 9, 2, 2);
        set_flags(key, KEY_NUM_LOCKABLE, 0);
        set_shifted(key, XK_KP_Page_Down, "PD");
        add_key(key_widget, XK_KP_Enter, "Ent", 44, 9, 2, 4);

        /* 6th row */
        key = add_key(key_widget, XK_Control_L, "Ctrl", 0, 11, 4, 2);
        set_flags(key, KEY_STICKY, 0);
        key = add_key(key_widget, XK_Super_L, "Su", 4, 11, 2, 2);
        set_flags(key, KEY_STICKY, 0);
        key = add_key(key_widget, XK_Alt_L, "Alt", 6, 11, 2, 2);
        set_flags(key, KEY_STICKY, 0);
        add_key(key_widget, XK_space, "", 8, 11, 14, 2);
        key = add_key(key_widget, XK_Alt_R, "Alt", 22, 11, 2, 2);
        set_flags(key, KEY_STICKY, 0);
        add_key(key_widget, XK_Menu, "Mn", 24, 11, 2, 2);
        key = add_key(key_widget, XK_Control_R, "Ctrl", 26, 11, 4, 2);
        set_flags(key, KEY_STICKY, 0);

        key = add_key(key_widget, XK_Left, "", 31, 11, 2, 2);
        set_flags(key, KEY_ARROW, 180);
        key = add_key(key_widget, XK_Down, "", 33, 11, 2, 2);
        set_flags(key, KEY_ARROW, 270);
        key = add_key(key_widget, XK_Right, "", 35, 11, 2, 2);
        set_flags(key, KEY_ARROW, 0);

        key = add_key(key_widget, XK_KP_0, "0", 38, 11, 4, 2);
        set_flags(key, KEY_NUM_LOCKABLE, 0);
        set_shifted(key, XK_KP_Insert, "Ins");
        key = add_key(key_widget, XK_KP_Decimal, ".", 42, 11, 2, 2);
        set_flags(key, KEY_NUM_LOCKABLE, 0);
        set_shifted(key, XK_KP_Delete, "Del");

        /* Setup drawing area events */
        g_signal_connect(G_OBJECT(drawing_area), "expose_event",
                         G_CALLBACK(expose_event), key_widget);
        g_signal_connect(G_OBJECT(drawing_area), "configure_event",
                         G_CALLBACK(configure_event), key_widget);
        g_signal_connect(G_OBJECT(drawing_area), "button_press_event",
                         G_CALLBACK(key_widget_button_press), key_widget);
        g_signal_connect(G_OBJECT(drawing_area), "button_release_event",
                         G_CALLBACK(key_widget_button_release), key_widget);
        g_signal_connect(G_OBJECT(drawing_area), "style_set",
                         G_CALLBACK(style_set), key_widget);
        g_signal_connect(G_OBJECT(drawing_area), "enter_notify_event",
                         G_CALLBACK(notify_event), key_widget);
        g_signal_connect(G_OBJECT(drawing_area), "leave_notify_event",
                         G_CALLBACK(notify_event), key_widget);
        gtk_widget_set_events(drawing_area, GDK_EXPOSURE_MASK |
                                            GDK_BUTTON_PRESS_MASK |
                                            GDK_BUTTON_RELEASE_MASK);

        return key_widget;
}
