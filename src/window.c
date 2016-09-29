
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
#include "keys.h"
#include <memory.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <gdk/gdkx.h>

/* Enable this option if your window manager support window gravity. With this
   option, the CellWriter window will resize from the bottom using the
   window manager rather than manually. */
/* #define WINDOW_GRAVITY */

/* options.c */
void options_dialog_open(void);

/* recognize.c */
void update_enabled_samples(void);

/* cellwidget.c */
extern int training, cell_width, cell_height, cell_cols_pref;

GtkWidget *cell_widget_new(void);
void cell_widget_clear(void);
void cell_widget_render(void);
int cell_widget_insert(void);
void cell_widget_train(void);
void cell_widget_pack(void);
int cell_widget_update_colors(void);
void cell_widget_show_buffer(GtkWidget *button);
int cell_widget_scrollbar_width(void);
int cell_widget_get_height(void);

/* statusicon.c */
int status_icon_create(void);
int status_icon_embedded(void);

/* main.c */
extern int keyboard_only;

void save_profile(void);

/* keywidget.c */
void key_widget_resize(KeyWidget *key_widget);

/*
        Main window
*/

GtkWidget *window;
GtkTooltips *tooltips;
int window_force_x = -1, window_force_y = -1, training_block = 0,
    window_docked = WINDOW_UNDOCKED, window_force_docked = -1,
    window_button_labels = TRUE, window_force_show = FALSE,
    window_force_hide = FALSE, style_colors = TRUE, window_embedded = FALSE,
    window_struts = FALSE;

/* Tab XPM image */
static char *tab_xpm[] =
{
        "7 4 2 1",
        "         c None",
        ".        c #000000",
        ".......",
        " ..... ",
        "  ...  ",
        "   .   "
};

static KeyWidget *key_widget;
static GtkWidget *train_label_box, *train_label_frame, *train_label = NULL,
                 *bottom_box, *blocks_combo, *cell_widget,
                 *setup_button, *keys_button, *insert_button,
                 *clear_button, *train_button, *buffer_button;
static GdkRectangle window_frame = {-1, -1, 0, 0}, window_frame_saved;
static int screen_width = -1, screen_height = -1,
           window_shown = TRUE, history_valid = FALSE, keys_on = FALSE;

static void toggle_button_labels(int on)
{
        static int labels_off;

        if (labels_off && on) {
                gtk_button_set_label(GTK_BUTTON(train_button), "Train");
                gtk_button_set_label(GTK_BUTTON(setup_button), "Setup");
                gtk_button_set_label(GTK_BUTTON(clear_button), "Clear");
                gtk_button_set_label(GTK_BUTTON(insert_button), "Insert");
                gtk_button_set_label(GTK_BUTTON(keys_button), "Keys");
        } else if (!labels_off && !on) {
                gtk_button_set_label(GTK_BUTTON(train_button), "");
                gtk_button_set_label(GTK_BUTTON(setup_button), "");
                gtk_button_set_label(GTK_BUTTON(keys_button), "");
                gtk_button_set_label(GTK_BUTTON(clear_button), "");
                gtk_button_set_label(GTK_BUTTON(insert_button), "");
                gtk_button_set_label(GTK_BUTTON(keys_button), "");
        }
        labels_off = !on;
}

void window_pack(void)
{
        cell_widget_pack();
        toggle_button_labels(window_button_labels);
        if (training)
                gtk_widget_show(train_label_frame);
        key_widget_resize(key_widget);
}

void window_update_colors(void)
{
        int keys_changed;

        keys_changed = key_widget_update_colors();
        if (cell_widget_update_colors() || keys_changed)
                cell_widget_render();
        if (keys_changed)
                key_widget_render(key_widget);
}

static void update_struts(void)
/* Reserves screen space for the docked window.
   FIXME In Metacity it causes the window to be shoved outside of its own
         struts, which is especially devastating for top docking because this
         causes an infinite loop of events causing the struts to repeatedly
         scan down from the top of the screen. GOK and other applications
         somehow get around this but I can't figure out how. */
{
        static gulong struts[12];
        guint32 new2 = 0, new3 = 0, new9 = 0, new11 = 0;
        GdkAtom atom_strut, atom_strut_partial, cardinal;

        if (!window || !window->window || !window_struts)
                return;
        if (window_docked == WINDOW_DOCKED_TOP) {
                new2 = window_frame.y + window_frame.height;
                new9 = window_frame.width;
        } else if (window_docked == WINDOW_DOCKED_BOTTOM) {
                new3 = window_frame.height;
                new11 = window_frame.width;
        }
        if (new2 == struts[2] && new3 == struts[3] &&
            new9 == struts[9] && new11 == struts[11])
                return;
        trace("top=%d (%d) bottom=%d (%d)", new2, new9, new3, new11);
        struts[2] = new2;
        struts[3] = new3;
        struts[9] = new9;
        struts[11] = new11;
        atom_strut = gdk_atom_intern("_NET_WM_STRUT", FALSE),
        atom_strut_partial = gdk_atom_intern("_NET_WM_STRUT_PARTIAL", FALSE);
        cardinal = gdk_atom_intern("CARDINAL", FALSE);
        gdk_property_change(GDK_WINDOW(window->window), atom_strut, cardinal,
                            32, GDK_PROP_MODE_REPLACE, (guchar*)&struts, 4);
        gdk_property_change(GDK_WINDOW(window->window), atom_strut_partial,
                            cardinal, 32, GDK_PROP_MODE_REPLACE,
                            (guchar*)&struts, 12);
}

static void set_geometry_hints(void)
{
        GdkGeometry geometry;

        geometry.min_width = -1;
        geometry.min_height = -1;
        geometry.max_width = -1;
        geometry.max_height = -1;

        /* Use window geometry to force the window to be as large as the
           screen */
        if (window_docked)
                geometry.max_width = geometry.min_width = screen_width;

        gtk_window_set_geometry_hints(GTK_WINDOW(window), window, &geometry,
                                      GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
        trace("%dx%d", geometry.min_width, geometry.min_height);

#ifdef WINDOW_GRAVITY
        /* In some bright and sunny alternate universe when specifications are
           actually implemented as inteded, this function alone would cause the
           window frame to expand upwards without having to perform the ugly
           hack in window_configure(). */
        geometry.win_gravity = GDK_GRAVITY_SOUTH_WEST;
        gtk_window_set_geometry_hints(GTK_WINDOW(window), window, &geometry,
                                      GDK_HINT_WIN_GRAVITY);
#endif
}

static void docked_move_resize(void)
{
        GdkScreen *screen;
        int y = 0;

        if (!window_docked)
                return;
        screen = gtk_window_get_screen(GTK_WINDOW(window));
        if (window_docked == WINDOW_DOCKED_BOTTOM)
                y = gdk_screen_get_height(screen) - window_frame.height;
        set_geometry_hints();
        gtk_window_move(GTK_WINDOW(window), 0, y);
        cell_widget_pack();
        key_widget_resize(key_widget);
        trace("y=%d", y);
        if (window_struts)
                gdk_window_set_type_hint(GDK_WINDOW(window->window),
                                         GDK_WINDOW_TYPE_HINT_DOCK);
}

static gboolean window_configure(GtkWidget *widget, GdkEventConfigure *event)
/* Intelligently grow the window up and/or left if we are in the bottom or
   right corners of the screen respectively */
{
        GdkRectangle new_frame = {0, 0, 0, 0};
        GdkScreen *screen;
        int screen_w, screen_h, height_change, label_w;

        if (!window || !window->window)
                return FALSE;
        gtk_widget_get_display(window);

        /* Get screen and window information */
        screen = gtk_window_get_screen(GTK_WINDOW(window));
        screen_w = gdk_screen_get_width(screen);
        screen_h = gdk_screen_get_height(screen);
        gdk_window_get_frame_extents(window->window, &new_frame);

        /* We need to resize wrapped labels manually */
        label_w = window->allocation.width - 16;
        if (train_label && train_label->requisition.width != label_w)
                gtk_widget_set_size_request(train_label, label_w, -1);

        /* Docked windows have special placing requirements */
        height_change = new_frame.height - window_frame.height;
        if (window_docked) {
                window_frame = new_frame;
                if (screen_w != screen_width || screen_h != screen_height ||
                    (height_change && window_docked == WINDOW_DOCKED_BOTTOM)) {
                        screen_width = screen_w;
                        screen_height = screen_h;
                        trace("move-sizing bottom-docked window");
                        docked_move_resize();
                }
                update_struts();
                return FALSE;
        }
        screen_width = screen_w;
        screen_height = screen_h;

        /* Do nothing on the first configure */
        if (window_frame.height <= 1) {
                window_frame = new_frame;
                return FALSE;
        }

#ifndef WINDOW_GRAVITY
        /* Keep the window aligned to the bottom border */
        if (height_change && window_frame.y + window_frame.height / 2 >
                             gdk_screen_get_height(screen) / 2)
                window_frame.y -= height_change;
        else
                height_change = 0;

        /* Do not allow the window to go off-screen */
        if (window_frame.x + new_frame.width > screen_w)
                window_frame.x = screen_w - new_frame.width;
        if (window_frame.y + new_frame.height > screen_h)
                window_frame.y = screen_h - new_frame.height;
        if (window_frame.x < 0)
                window_frame.x = 0;
        if (window_frame.y < 0)
                window_frame.y = 0;

        /* Some window managers (Metacity) do not allow windows to resize
           larger than the screen and will move the window back within the
           screen bounds when this happens. We don't like this because it
           screws with our own correcting offset. Fortunately, both the move
           and the resize are bundled in one configure event so we can work
           around this by using our old x/y coordinates when the dimensions
           change. */
        if (height_change && (new_frame.x != window_frame.x ||
                              new_frame.y != window_frame.y)) {
                gtk_window_move(GTK_WINDOW(window),
                                window_frame.x, window_frame.y);
                window_frame.width = new_frame.width;
                window_frame.height = new_frame.height;
                trace("moving to (%d, %d)", window_frame.x, window_frame.y);
        } else
                window_frame = new_frame;
#endif

        return FALSE;
}

void window_set_docked(int mode)
{
        if (mode < WINDOW_UNDOCKED)
                mode = WINDOW_UNDOCKED;
        if (mode >= WINDOW_DOCKED_BOTTOM)
                mode = WINDOW_DOCKED_BOTTOM;
        if (mode && !window_docked)
                window_frame_saved = window_frame;
        window_docked = mode;
        gtk_window_set_decorated(GTK_WINDOW(window), !mode);
        set_geometry_hints();
        cell_widget_pack();
        key_widget_resize(key_widget);

        /* Set window docking hints.
           FIXME This allegedly solves docking problems with some window
                 managers but only seems to cause more problems for
                 Compiz and Metacity. */
        /*
        gtk_widget_hide(window);
        if (mode == WINDOW_UNDOCKED)
                gtk_window_set_type_hint(GTK_WINDOW(window),
                                         GDK_WINDOW_TYPE_HINT_NORMAL);
        else
                gtk_window_set_type_hint(GTK_WINDOW(window),
                                         GDK_WINDOW_TYPE_HINT_DOCK);
        gtk_widget_show(window);
        */

        /* Restore the old window position */
        if (!mode) {
                update_struts();
                window_frame = window_frame_saved;
                gtk_window_move(GTK_WINDOW(window), window_frame.x,
                                window_frame.y);
                trace("moving to (%d, %d)", window_frame.x, window_frame.y);
        }

        /* Move the window into docked position */
        else
                docked_move_resize();
}

void train_button_toggled(void)
{
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(train_button))) {
                cell_widget_train();
                gtk_widget_hide(clear_button);
                gtk_widget_hide(keys_button);
                gtk_widget_hide(insert_button);
                gtk_widget_hide(buffer_button);
                gtk_widget_show(blocks_combo);
                gtk_widget_show(train_label_frame);
        } else {
                cell_widget_clear();
                gtk_widget_hide(blocks_combo);
                gtk_widget_hide(train_label_frame);
                gtk_widget_show(clear_button);
                gtk_widget_show(keys_button);
                gtk_widget_show(insert_button);
                gtk_widget_show(buffer_button);

                /* Take the opportunity to save training data.
                   TODO: Only save if there is new training data. */
                save_profile();
        }
}

static void keys_button_toggled(void)
{
        keys_on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(keys_button));

        /* Hide widgets first */
        if (keys_on) {
                gtk_widget_hide(cell_widget);
                gtk_widget_set_sensitive(clear_button, FALSE);
                gtk_widget_set_sensitive(insert_button, FALSE);
                gtk_widget_set_sensitive(train_button, FALSE);
                gtk_widget_set_sensitive(buffer_button, FALSE);
        } else {
                gtk_widget_hide(key_widget->drawing_area);
                gtk_widget_set_sensitive(clear_button, TRUE);
                gtk_widget_set_sensitive(insert_button, TRUE);
                gtk_widget_set_sensitive(train_button, TRUE);
                if (history_valid)
                        gtk_widget_set_sensitive(buffer_button, TRUE);
        }

        /* Resize the window */
        docked_move_resize();

        /* Show widgets */
        if (keys_on)
                gtk_widget_show(key_widget->drawing_area);
        else
                gtk_widget_show(cell_widget);
}

static int block_combo_to_unicode(int block)
/* Find the Combo Box index's unicode block */
{
        int i, pos;

        for (i = 0, pos = 0; unicode_blocks[i].name; i++)
                if (unicode_blocks[i].enabled && ++pos > block)
                        break;
        return i;
}

static int block_unicode_to_combo(int block)
/* Find the Unicode block's combo box position */
{
        int i, pos;

        for (i = 0, pos = 0; i < block && unicode_blocks[i].name; i++)
                if (unicode_blocks[i].enabled)
                        pos++;
        return pos;
}

static void blocks_combo_changed(void)
{
        int pos;

        pos = gtk_combo_box_get_active(GTK_COMBO_BOX(blocks_combo));
        training_block = block_combo_to_unicode(pos);
        if (training)
                cell_widget_train();
}

static GtkWidget *create_blocks_combo(void)
{
        GtkWidget *event_box;
        UnicodeBlock *block;

        if (blocks_combo)
                gtk_widget_destroy(blocks_combo);
        blocks_combo = gtk_combo_box_new_text();
        block = unicode_blocks;
        while (block->name) {
                if (block->enabled)
                        gtk_combo_box_append_text(GTK_COMBO_BOX(blocks_combo),
                                                  block->name);
                block++;
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(blocks_combo),
                                 block_unicode_to_combo(training_block));
        gtk_combo_box_set_focus_on_click(GTK_COMBO_BOX(blocks_combo), FALSE);
        g_signal_connect(G_OBJECT(blocks_combo), "changed",
                         G_CALLBACK(blocks_combo_changed), NULL);

        /* Wrap ComboBox in an EventBox for tooltips */
        event_box = gtk_event_box_new();
        gtk_tooltips_set_tip(tooltips, event_box,
                             "Select Unicode block to train", NULL);
        gtk_container_add(GTK_CONTAINER(event_box), blocks_combo);

        return event_box;
}

void window_toggle(void)
{
        if (GTK_WIDGET_VISIBLE(window)) {
                gtk_widget_hide(window);
                window_shown = FALSE;

                /* User may have rendered themselves unable to interact with
                   the program widgets by pressing one of the modifier keys
                   that, for instance, puts the WM in move-window mode, so
                   if the window is closed we need to reset the held keys */
                key_widget_cleanup(key_widget);
        } else {
                gtk_widget_show(window);
                window_shown = TRUE;
        }
}

void window_show(void)
{
        if (!(GTK_WIDGET_VISIBLE(window)))
                window_toggle();
}

void window_hide(void)
{
        if (GTK_WIDGET_VISIBLE(window))
                window_toggle();
}

gboolean window_close(void)
{
        if (status_icon_embedded()) {
                gtk_widget_hide(window);
                window_shown = FALSE;
                key_widget_cleanup(key_widget);
                return TRUE;
        }
        g_debug("Status icon failed to embed, quitting.");
        return FALSE;
}

static void window_style_set(GtkWidget *w)
{
        GdkColor train_label_bg = RGB_TO_GDKCOLOR(255, 255, 200),
                 train_label_fg = RGB_TO_GDKCOLOR(0, 0, 0);

        /* The training label color is taken from tooltips */
        if (!train_label)
                return;
#if GTK_CHECK_VERSION(2, 10, 0)
        gtk_style_lookup_color(w->style, "tooltip_bg_color", &train_label_bg);
        gtk_style_lookup_color(w->style, "tooltip_fg_color", &train_label_fg);
#endif
        gtk_widget_modify_bg(train_label_frame, GTK_STATE_NORMAL,
                             &train_label_bg);
        gtk_widget_modify_bg(train_label_box, GTK_STATE_NORMAL,
                             &train_label_bg);
        gtk_widget_modify_fg(train_label, GTK_STATE_NORMAL,
                             &train_label_fg);
        gtk_widget_modify_fg(blocks_combo, GTK_STATE_NORMAL,
                             &train_label_fg);
}

static void button_set_image_xpm(GtkWidget *button, char **xpm)
/* Creates a button with an XPM icon */
{
        GdkPixmap *pixmap;
        GdkBitmap *mask;
        GtkWidget *image;

        pixmap = gdk_pixmap_colormap_create_from_xpm_d
                 (NULL, gdk_colormap_get_system(), &mask, NULL, xpm);
        image = gtk_image_new_from_pixmap(pixmap, mask);
        g_object_unref(pixmap);
        gtk_button_set_image(GTK_BUTTON(button), image);
}

static void insert_button_clicked(void)
{
        if (cell_widget_insert()) {
                history_valid = TRUE;
                gtk_widget_set_sensitive(buffer_button, TRUE);
        }
}

static void buffer_button_pressed(void)
{
        if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buffer_button))) {
                cell_widget_show_buffer(buffer_button);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buffer_button),
                                             TRUE);
        }
}

static void print_window_xid(GtkWidget *widget)
{
        g_print("%d\n", (unsigned int)GDK_WINDOW_XID(widget->window));
}

static gint status_icon_embedded_check() {
        if (!status_icon_embedded()) {
                g_debug("Status icon failed to embed, showing window.");
                window_shown = TRUE;
                gtk_widget_show(window);
        }
        return FALSE;
}

void window_create(void)
/* Create the main window and child widgets */
{
        GtkWidget *widget, *window_vbox, *image;
        GdkScreen *screen;

        /* Create the window or plug */
        window = !window_embedded ? gtk_window_new(GTK_WINDOW_TOPLEVEL) :
                                    gtk_plug_new(0);
        g_signal_connect(G_OBJECT(window), "delete-event",
                         G_CALLBACK(window_close), NULL);
        g_signal_connect(G_OBJECT(window), "destroy",
                         G_CALLBACK(gtk_main_quit), NULL);
        g_signal_connect(G_OBJECT(window), "style-set",
                         G_CALLBACK(window_style_set), NULL);
        g_signal_connect(G_OBJECT(window), "configure-event",
                         G_CALLBACK(window_configure), NULL);
        gtk_window_set_accept_focus(GTK_WINDOW(window), FALSE);
        gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

        /* Tooltips */
        tooltips = gtk_tooltips_new();
        gtk_tooltips_enable(tooltips);

        /* Root box */
        window_vbox = gtk_vbox_new(FALSE, 0);
        gtk_widget_show(window_vbox);

        /* Training info label frame */
        train_label_frame = gtk_frame_new(NULL);
        gtk_widget_set_no_show_all(train_label_frame, TRUE);
        gtk_frame_set_shadow_type(GTK_FRAME(train_label_frame), GTK_SHADOW_IN);
        gtk_container_set_border_width(GTK_CONTAINER(train_label_frame), 2);

        /* Training info label */
        train_label = gtk_label_new(NULL);
        gtk_label_set_line_wrap(GTK_LABEL(train_label), TRUE);
        gtk_label_set_justify(GTK_LABEL(train_label), GTK_JUSTIFY_FILL);
        gtk_label_set_markup(GTK_LABEL(train_label),
                             "<b>Training Mode:</b> Carefully draw each "
                             "character in its cell. Multiple "
                             "samples will be stored for each character. "
                             "If you make a mistake, reset by "
                             "pressing on the cell with the pen eraser.");
        gtk_widget_show(train_label);

        /* Training info label colored box */
        train_label_box = gtk_event_box_new();
        gtk_widget_show(train_label_box);
        gtk_container_add(GTK_CONTAINER(train_label_box), train_label);
        gtk_container_add(GTK_CONTAINER(train_label_frame), train_label_box);
        gtk_widget_show_all(train_label_frame);
        gtk_box_pack_start(GTK_BOX(window_vbox), train_label_frame,
                           FALSE, FALSE, 0);

        /* Cell widget */
        cell_widget = cell_widget_new();
        gtk_box_pack_start(GTK_BOX(window_vbox), cell_widget, TRUE, TRUE, 2);
        if (!keyboard_only)
                gtk_widget_show_all(cell_widget);

        /* Key widget */
        key_widget = key_widget_new_full();
        gtk_box_pack_start(GTK_BOX(window_vbox), key_widget->drawing_area,
                           TRUE, TRUE, 2);
        if (keyboard_only) {
                gtk_widget_show(key_widget->drawing_area);
                keys_on = TRUE;
        }

        /* Bottom box */
        bottom_box = gtk_hbox_new(FALSE, 0);

        /* Train button */
        train_button = gtk_toggle_button_new_with_label("Train");
        gtk_button_set_focus_on_click(GTK_BUTTON(train_button), FALSE);
        gtk_button_set_image(GTK_BUTTON(train_button),
                             gtk_image_new_from_stock(GTK_STOCK_MEDIA_RECORD,
                                                      GTK_ICON_SIZE_BUTTON));
        gtk_button_set_relief(GTK_BUTTON(train_button), GTK_RELIEF_NONE);
        gtk_box_pack_start(GTK_BOX(bottom_box), train_button, FALSE, FALSE, 0);
        g_signal_connect(G_OBJECT(train_button), "toggled",
                         G_CALLBACK(train_button_toggled), 0);
        gtk_tooltips_set_tip(tooltips, train_button, "Toggle training mode",
                             NULL);

        /* Setup button */
        setup_button = gtk_button_new_with_label("Setup");
        gtk_button_set_focus_on_click(GTK_BUTTON(setup_button), FALSE);
        gtk_button_set_image(GTK_BUTTON(setup_button),
                             gtk_image_new_from_stock(GTK_STOCK_PREFERENCES,
                                                      GTK_ICON_SIZE_BUTTON));
        gtk_button_set_relief(GTK_BUTTON(setup_button), GTK_RELIEF_NONE);
        gtk_box_pack_start(GTK_BOX(bottom_box), setup_button, FALSE, FALSE, 0);
        g_signal_connect(G_OBJECT(setup_button), "clicked",
                         G_CALLBACK(options_dialog_open), 0);
        gtk_tooltips_set_tip(tooltips, setup_button, "Edit program options",
                             NULL);

        /* Expanding box to keep things tidy */
        widget = gtk_vbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bottom_box), widget, TRUE, FALSE, 0);

        /* Training Unicode Block selector */
        widget = create_blocks_combo();
        gtk_box_pack_start(GTK_BOX(bottom_box), widget, FALSE, FALSE, 0);
        gtk_widget_set_no_show_all(blocks_combo, TRUE);

        /* Clear button */
        clear_button = gtk_button_new_with_label("Clear");
        gtk_button_set_focus_on_click(GTK_BUTTON(clear_button), FALSE);
        image = gtk_image_new_from_stock(GTK_STOCK_CLEAR, GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(clear_button), image);
        gtk_button_set_relief(GTK_BUTTON(clear_button), GTK_RELIEF_NONE);
        gtk_box_pack_start(GTK_BOX(bottom_box), clear_button, FALSE, FALSE, 0);
        g_signal_connect(G_OBJECT(clear_button), "clicked",
                         G_CALLBACK(cell_widget_clear), 0);
        gtk_tooltips_set_tip(tooltips, clear_button, "Clear current input",
                             NULL);

        /* Keys button */
        keys_button = gtk_toggle_button_new_with_label("Keys");
        gtk_button_set_focus_on_click(GTK_BUTTON(keys_button), FALSE);
        image = gtk_image_new_from_icon_name("keyboard", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(keys_button), image);
        gtk_button_set_relief(GTK_BUTTON(keys_button), GTK_RELIEF_NONE);
        gtk_box_pack_start(GTK_BOX(bottom_box), keys_button, FALSE, FALSE, 0);
        g_signal_connect(G_OBJECT(keys_button), "toggled",
                         G_CALLBACK(keys_button_toggled), 0);
        gtk_tooltips_set_tip(tooltips, keys_button,
                             "Switch between on-screen keyboard and "
                             "handwriting input", NULL);

        /* Insert button */
        insert_button = gtk_button_new_with_label("Enter");
        gtk_button_set_focus_on_click(GTK_BUTTON(insert_button), FALSE);
        gtk_button_set_image(GTK_BUTTON(insert_button),
                             gtk_image_new_from_stock(GTK_STOCK_OK,
                                                      GTK_ICON_SIZE_BUTTON));
        gtk_button_set_relief(GTK_BUTTON(insert_button), GTK_RELIEF_NONE);
        gtk_box_pack_start(GTK_BOX(bottom_box), insert_button, FALSE, FALSE, 0);
        g_signal_connect(G_OBJECT(insert_button), "clicked",
                         G_CALLBACK(insert_button_clicked), 0);
        gtk_tooltips_set_tip(tooltips, insert_button,
                             "Insert input or press Enter key", NULL);

        /* Back buffer button */
        buffer_button = gtk_toggle_button_new();
        gtk_button_set_focus_on_click(GTK_BUTTON(buffer_button), FALSE);
        button_set_image_xpm(buffer_button, tab_xpm);
        gtk_button_set_relief(GTK_BUTTON(buffer_button), GTK_RELIEF_NONE);
        gtk_box_pack_start(GTK_BOX(bottom_box), buffer_button, FALSE, FALSE, 0);
        g_signal_connect(G_OBJECT(buffer_button), "pressed",
                         G_CALLBACK(buffer_button_pressed), NULL);
        gtk_tooltips_set_tip(tooltips, buffer_button,
                             "Recall previously entered input", NULL);
        gtk_widget_set_sensitive(buffer_button, FALSE);

        /* Pack the regular bottom box */
        gtk_box_pack_start(GTK_BOX(window_vbox), bottom_box, FALSE, FALSE, 0);
        if (!keyboard_only)
                gtk_widget_show_all(bottom_box);

        /* Update button labels */
        toggle_button_labels(window_button_labels);

        /* Set window style */
        window_style_set(window);

        if (window_embedded) {

                /* Embedding in a screensaver won't let us popup new windows */
                gtk_widget_hide(buffer_button);
                gtk_widget_hide(train_button);
                gtk_widget_hide(setup_button);

                /* If we are embedded we need to print the plug's window XID */
                g_signal_connect_after(G_OBJECT(window), "realize",
                                       G_CALLBACK(print_window_xid), NULL);

                gtk_container_add(GTK_CONTAINER(window), window_vbox);
                gtk_widget_show(window);
                return;
        }

        /* Non-embedded window configuration */
        gtk_container_add(GTK_CONTAINER(window), window_vbox);
        gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
        gtk_window_set_type_hint(GTK_WINDOW(window),
                                 GDK_WINDOW_TYPE_HINT_UTILITY);
        gtk_window_set_title(GTK_WINDOW(window), PACKAGE_NAME);
        gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
        gtk_window_set_decorated(GTK_WINDOW(window), TRUE);
        gtk_window_stick(GTK_WINDOW(window));

        /* Coordinates passed on the command-line */
        if (window_force_x >= 0)
                window_frame.x = window_force_x;
        if (window_force_y >= 0)
                window_frame.y = window_force_y;

        /* Center window on initial startup */
        screen = gtk_window_get_screen(GTK_WINDOW(window));
        if (window_frame.x < 0)
                window_frame.x = gdk_screen_get_width(screen) / 2;
        if (window_frame.y < 0)
                window_frame.y = gdk_screen_get_height(screen) * 3 / 4;
        gtk_window_move(GTK_WINDOW(window), window_frame.x,
                        window_frame.y);

        /* Create status icon */
        status_icon_create();

        /* Set the window size */
        if (window_force_docked >= WINDOW_UNDOCKED)
                window_docked = window_force_docked;
        if (window_docked) {
                int mode;

                mode = window_docked;
                window_docked = WINDOW_UNDOCKED;
                window_set_docked(mode);
        }

        /* Show window */
        if (window_force_hide)
                window_shown = FALSE;
        else if (window_force_show)
                window_shown = TRUE;
        if (window_shown) {
                gtk_widget_show(window);
        } else {
                /* Check if the status icon is embedded after a timeout so that
                   it has a chance to embed itself into the tray. */
                gtk_timeout_add(10, status_icon_embedded_check, NULL);
        }
}

void window_sync(void)
/* Sync data with profile, do not change item order! */
{
        profile_write("window");

        /* Docking the window will mess up the desired natural frame */
        if (!profile_read_only && window_docked) {
                profile_sync_int(&window_frame_saved.x);
                profile_sync_int(&window_frame_saved.y);
        } else {
                profile_sync_int(&window_frame.x);
                profile_sync_int(&window_frame.y);
        }

        profile_sync_int(&training_block);
        profile_sync_int(&window_shown);
        profile_sync_int(&window_button_labels);
        profile_sync_int(&keyboard_size);
        profile_sync_int(&window_docked);
        profile_write("\n");
}

void window_cleanup(void)
{
        key_widget_cleanup(key_widget);
}

/*
        Unicode blocks
*/

/* This table is based on unicode-blocks.h from the gucharmap project */
UnicodeBlock unicode_blocks[] =
{
        { TRUE,  0x0000, 0x007F, "Basic Latin" },
        { TRUE,  0x0080, 0x00FF, "Latin-1 Supplement" },
        { FALSE, 0x0100, 0x017F, "Latin Extended-A" },
        { FALSE, 0x0180, 0x024F, "Latin Extended-B" },
        { FALSE, 0x0250, 0x02AF, "IPA Extensions" },
        { FALSE, 0x02B0, 0x02FF, "Spacing Modifier Letters" },
        { FALSE, 0x0300, 0x036F, "Combining Diacritical Marks" },
        { FALSE, 0x0370, 0x03FF, "Greek and Coptic" },
        { FALSE, 0x0400, 0x04FF, "Cyrillic" },
        { FALSE, 0x0500, 0x052F, "Cyrillic Supplement" },
        { FALSE, 0x0530, 0x058F, "Armenian" },
        { FALSE, 0x0590, 0x05FF, "Hebrew" },
        { FALSE, 0x0600, 0x06FF, "Arabic" },
        { FALSE, 0x0700, 0x074F, "Syriac" },
        { FALSE, 0x0750, 0x077F, "Arabic Supplement" },
        { FALSE, 0x0780, 0x07BF, "Thaana" },
        { FALSE, 0x07C0, 0x07FF, "N'Ko" },
        { FALSE, 0x0900, 0x097F, "Devanagari" },
        { FALSE, 0x0980, 0x09FF, "Bengali" },
        { FALSE, 0x0A00, 0x0A7F, "Gurmukhi" },
        { FALSE, 0x0A80, 0x0AFF, "Gujarati" },
        { FALSE, 0x0B00, 0x0B7F, "Oriya" },
        { FALSE, 0x0B80, 0x0BFF, "Tamil" },
        { FALSE, 0x0C00, 0x0C7F, "Telugu" },
        { FALSE, 0x0C80, 0x0CFF, "Kannada" },
        { FALSE, 0x0D00, 0x0D7F, "Malayalam" },
        { FALSE, 0x0D80, 0x0DFF, "Sinhala" },
        { FALSE, 0x0E00, 0x0E7F, "Thai" },
        { FALSE, 0x0E80, 0x0EFF, "Lao" },
        { FALSE, 0x0F00, 0x0FFF, "Tibetan" },
        { FALSE, 0x1000, 0x109F, "Myanmar" },
        { FALSE, 0x10A0, 0x10FF, "Georgian" },
        { FALSE, 0x1100, 0x11FF, "Hangul Jamo" },
        { FALSE, 0x1200, 0x137F, "Ethiopic" },
        { FALSE, 0x1380, 0x139F, "Ethiopic Supplement" },
        { FALSE, 0x13A0, 0x13FF, "Cherokee" },
        { FALSE, 0x1400, 0x167F, "Unified Canadian Aboriginal Syllabics" },
        { FALSE, 0x1680, 0x169F, "Ogham" },
        { FALSE, 0x16A0, 0x16FF, "Runic" },
        { FALSE, 0x1700, 0x171F, "Tagalog" },
        { FALSE, 0x1720, 0x173F, "Hanunoo" },
        { FALSE, 0x1740, 0x175F, "Buhid" },
        { FALSE, 0x1760, 0x177F, "Tagbanwa" },
        { FALSE, 0x1780, 0x17FF, "Khmer" },
        { FALSE, 0x1800, 0x18AF, "Mongolian" },
        { FALSE, 0x1900, 0x194F, "Limbu" },
        { FALSE, 0x1950, 0x197F, "Tai Le" },
        { FALSE, 0x1980, 0x19DF, "New Tai Lue" },
        { FALSE, 0x19E0, 0x19FF, "Khmer Symbols" },
        { FALSE, 0x1A00, 0x1A1F, "Buginese" },
        { FALSE, 0x1B00, 0x1B7F, "Balinese" },
        { FALSE, 0x1D00, 0x1D7F, "Phonetic Extensions" },
        { FALSE, 0x1D80, 0x1DBF, "Phonetic Extensions Supplement" },
        { FALSE, 0x1DC0, 0x1DFF, "Combining Diacritical Marks Supplement" },
        { FALSE, 0x1E00, 0x1EFF, "Latin Extended Additional" },
        { FALSE, 0x1F00, 0x1FFF, "Greek Extended" },
        { FALSE, 0x2000, 0x206F, "General Punctuation" },
        { FALSE, 0x2070, 0x209F, "Superscripts and Subscripts" },
        { FALSE, 0x20A0, 0x20CF, "Currency Symbols" },
        { FALSE, 0x20D0, 0x20FF, "Combining Diacritical Marks for Symbols" },
        { FALSE, 0x2100, 0x214F, "Letterlike Symbols" },
        { FALSE, 0x2150, 0x218F, "Number Forms" },
        { FALSE, 0x2190, 0x21FF, "Arrows" },
        { FALSE, 0x2200, 0x22FF, "Mathematical Operators" },
        { FALSE, 0x2300, 0x23FF, "Miscellaneous Technical" },
        { FALSE, 0x2400, 0x243F, "Control Pictures" },
        { FALSE, 0x2440, 0x245F, "Optical Character Recognition" },
        { FALSE, 0x2460, 0x24FF, "Enclosed Alphanumerics" },
        { FALSE, 0x2500, 0x257F, "Box Drawing" },
        { FALSE, 0x2580, 0x259F, "Block Elements" },
        { FALSE, 0x25A0, 0x25FF, "Geometric Shapes" },
        { FALSE, 0x2600, 0x26FF, "Miscellaneous Symbols" },
        { FALSE, 0x2700, 0x27BF, "Dingbats" },
        { FALSE, 0x27C0, 0x27EF, "Miscellaneous Mathematical Symbols-A" },
        { FALSE, 0x27F0, 0x27FF, "Supplemental Arrows-A" },
        { FALSE, 0x2800, 0x28FF, "Braille Patterns" },
        { FALSE, 0x2900, 0x297F, "Supplemental Arrows-B" },
        { FALSE, 0x2980, 0x29FF, "Miscellaneous Mathematical Symbols-B" },
        { FALSE, 0x2A00, 0x2AFF, "Supplemental Mathematical Operators" },
        { FALSE, 0x2B00, 0x2BFF, "Miscellaneous Symbols and Arrows" },
        { FALSE, 0x2C00, 0x2C5F, "Glagolitic" },
        { FALSE, 0x2C60, 0x2C7F, "Latin Extended-C" },
        { FALSE, 0x2C80, 0x2CFF, "Coptic" },
        { FALSE, 0x2D00, 0x2D2F, "Georgian Supplement" },
        { FALSE, 0x2D30, 0x2D7F, "Tifinagh" },
        { FALSE, 0x2D80, 0x2DDF, "Ethiopic Extended" },
        { FALSE, 0x2E00, 0x2E7F, "Supplemental Punctuation" },
        { FALSE, 0x2E80, 0x2EFF, "CJK Radicals Supplement" },
        { FALSE, 0x2F00, 0x2FDF, "Kangxi Radicals" },
        { FALSE, 0x2FF0, 0x2FFF, "Ideographic Description Characters" },
        { FALSE, 0x3000, 0x303F, "CJK Symbols and Punctuation" },
        { FALSE, 0x3040, 0x309F, "Hiragana" },
        { FALSE, 0x30A0, 0x30FF, "Katakana" },
        { FALSE, 0x3100, 0x312F, "Bopomofo" },
        { FALSE, 0x3130, 0x318F, "Hangul Compatibility Jamo" },
        { FALSE, 0x3190, 0x319F, "Kanbun" },
        { FALSE, 0x31A0, 0x31BF, "Bopomofo Extended" },
        { FALSE, 0x31C0, 0x31EF, "CJK Strokes" },
        { FALSE, 0x31F0, 0x31FF, "Katakana Phonetic Extensions" },
        { FALSE, 0x3200, 0x32FF, "Enclosed CJK Letters and Months" },
        { FALSE, 0x3300, 0x33FF, "CJK Compatibility" },
        { FALSE, 0x3400, 0x4DBF, "CJK Unified Ideographs Extension A" },
        { FALSE, 0x4DC0, 0x4DFF, "Yijing Hexagram Symbols" },
        { FALSE, 0x4E00, 0x9FFF, "CJK Unified Ideographs" },
        { FALSE, 0xA000, 0xA48F, "Yi Syllables" },
        { FALSE, 0xA490, 0xA4CF, "Yi Radicals" },
        { FALSE, 0xA700, 0xA71F, "Modifier Tone Letters" },
        { FALSE, 0xA720, 0xA7FF, "Latin Extended-D" },
        { FALSE, 0xA800, 0xA82F, "Syloti Nagri" },
        { FALSE, 0xA840, 0xA87F, "Phags-pa" },
        { FALSE, 0xAC00, 0xD7AF, "Hangul Syllables" },
        { FALSE, 0xD800, 0xDB7F, "High Surrogates" },
        { FALSE, 0xDB80, 0xDBFF, "High Private Use Surrogates" },
        { FALSE, 0xDC00, 0xDFFF, "Low Surrogates" },
        { FALSE, 0xE000, 0xF8FF, "Private Use Area" },
        { FALSE, 0xF900, 0xFAFF, "CJK Compatibility Ideographs" },
        { FALSE, 0xFB00, 0xFB4F, "Alphabetic Presentation Forms" },
        { FALSE, 0xFB50, 0xFDFF, "Arabic Presentation Forms-A" },
        { FALSE, 0xFE00, 0xFE0F, "Variation Selectors" },
        { FALSE, 0xFE10, 0xFE1F, "Vertical Forms" },
        { FALSE, 0xFE20, 0xFE2F, "Combining Half Marks" },
        { FALSE, 0xFE30, 0xFE4F, "CJK Compatibility Forms" },
        { FALSE, 0xFE50, 0xFE6F, "Small Form Variants" },
        { FALSE, 0xFE70, 0xFEFF, "Arabic Presentation Forms-B" },
        { FALSE, 0xFF00, 0xFFEF, "Halfwidth and Fullwidth Forms" },
        { FALSE, 0xFFF0, 0xFFFF, "Specials" },
        { FALSE, 0x10000, 0x1007F, "Linear B Syllabary" },
        { FALSE, 0x10080, 0x100FF, "Linear B Ideograms" },
        { FALSE, 0x10100, 0x1013F, "Aegean Numbers" },
        { FALSE, 0x10140, 0x1018F, "Ancient Greek Numbers" },
        { FALSE, 0x10300, 0x1032F, "Old Italic" },
        { FALSE, 0x10330, 0x1034F, "Gothic" },
        { FALSE, 0x10380, 0x1039F, "Ugaritic" },
        { FALSE, 0x103A0, 0x103DF, "Old Persian" },
        { FALSE, 0x10400, 0x1044F, "Deseret" },
        { FALSE, 0x10450, 0x1047F, "Shavian" },
        { FALSE, 0x10480, 0x104AF, "Osmanya" },
        { FALSE, 0x10800, 0x1083F, "Cypriot Syllabary" },
        { FALSE, 0x10900, 0x1091F, "Phoenician" },
        { FALSE, 0x10A00, 0x10A5F, "Kharoshthi" },
        { FALSE, 0x12000, 0x123FF, "Cuneiform" },
        { FALSE, 0x12400, 0x1247F, "Cuneiform Numbers and Punctuation" },
        { FALSE, 0x1D000, 0x1D0FF, "Byzantine Musical Symbols" },
        { FALSE, 0x1D100, 0x1D1FF, "Musical Symbols" },
        { FALSE, 0x1D200, 0x1D24F, "Ancient Greek Musical Notation" },
        { FALSE, 0x1D300, 0x1D35F, "Tai Xuan Jing Symbols" },
        { FALSE, 0x1D360, 0x1D37F, "Counting Rod Numerals" },
        { FALSE, 0x1D400, 0x1D7FF, "Mathematical Alphanumeric Symbols" },

        /* Cut the table here because the rest are non-printable characters */
        { FALSE, 0,      0,      NULL },
};

void blocks_sync(void)
{
        UnicodeBlock *block;

        profile_write("blocks");
        block = unicode_blocks;
        while (block->name) {
                profile_sync_short(&block->enabled);
                block++;
        }
        profile_write("\n");
}

void unicode_block_toggle(int block, int on)
{
        int pos, active, training_block_saved;

        if (block < 0 || unicode_blocks[block].enabled == on)
                return;
        unicode_blocks[block].enabled = on;
        active = gtk_combo_box_get_active(GTK_COMBO_BOX(blocks_combo));
        pos = block_unicode_to_combo(block);
        training_block_saved = training_block;
        if (!on)
                gtk_combo_box_remove_text(GTK_COMBO_BOX(blocks_combo), pos);
        else
                gtk_combo_box_insert_text(GTK_COMBO_BOX(blocks_combo), pos,
                                          unicode_blocks[block].name);
        update_enabled_samples();
        if ((!on && block <= training_block_saved) || active < 0)
                gtk_combo_box_set_active(GTK_COMBO_BOX(blocks_combo),
                                         active > 0 ? active - 1 : 0);

        /* Are we out of blocks? */
        if (gtk_combo_box_get_active(GTK_COMBO_BOX(blocks_combo)) < 0) {
                training_block = -1;
                cell_widget_train();
        }
}

/*
        Start-up message dialog
*/

#define WELCOME_MSG "You are either starting " PACKAGE_NAME " for the first " \
                    "time or have not yet created any training samples. " \
                    PACKAGE_NAME " requires accurate training samples of " \
                    "your characters before it can work. " \
                    "The program will now enter training mode. " \
                    "Carefully draw each character in its cell and then " \
                    "press the 'Train' button."

void startup_splash_show(void)
{
        GtkWidget *dialog;

        dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                        GTK_DIALOG_DESTROY_WITH_PARENT |
                                        GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                                        GTK_BUTTONS_OK,
                                        "Welcome to " PACKAGE_STRING "!");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                                 WELCOME_MSG);
        gtk_window_set_title(GTK_WINDOW(dialog),
                             "Welcome to " PACKAGE_NAME "!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        /* Press in the training button for the user */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(train_button), TRUE);
}
