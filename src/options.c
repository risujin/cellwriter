
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
#include <stdlib.h>
#include <string.h>

/* preprocess.c */
int ignore_stroke_dir, ignore_stroke_num;

/* cellwidget.c */
extern int cell_width, cell_height, cell_cols_pref, cell_rows_pref,
           train_on_input, right_to_left, keyboard_enabled, xinput_enabled;
extern GdkColor custom_active_color, custom_inactive_color,
                custom_ink_color, custom_select_color;

void cell_widget_render(void);
void cell_widget_set_cursor(int recreate);
void cell_widget_enable_xinput(int on);

/* keywidget.c */
extern GdkColor custom_key_color;
extern int keyboard_size;

void key_widget_update_colors(void);

/* statusicon.c */
extern int status_menu_left_click;

/*
        Profile options
*/

static void color_sync(GdkColor *color)
{
        profile_sync_short((short*)&color->red);
        profile_sync_short((short*)&color->green);
        profile_sync_short((short*)&color->blue);
}

void options_sync(void)
/* Read or write options. Order here is important for compatibility. */
{
        profile_write("options");
        profile_sync_int(&cell_width);
        profile_sync_int(&cell_height);
        profile_sync_int(&cell_cols_pref);
        profile_sync_int(&cell_rows_pref);
        color_sync(&custom_active_color);
        color_sync(&custom_inactive_color);
        color_sync(&custom_select_color);
        color_sync(&custom_ink_color);
        profile_sync_int(&train_on_input);
        profile_sync_int(&ignore_stroke_dir);
        profile_sync_int(&ignore_stroke_num);
        profile_sync_int(&wordfreq_enable);
        profile_sync_int(&right_to_left);
        color_sync(&custom_key_color);
        profile_sync_int(&keyboard_enabled);
        profile_sync_int(&xinput_enabled);
        profile_sync_int(&style_colors);
        profile_sync_int(&status_menu_left_click);
        profile_write("\n");
}

/*
        Unicode blocks list
*/

static void unicode_block_toggled(GtkCellRendererToggle *renderer, gchar *path,
                                  GtkListStore *blocks_store)
{
        GtkTreePath *tree_path;
        GtkTreeIter iter;
        GValue value;
        gboolean enabled;
        int index;

        /* Get the block this checkbox references */
        tree_path = gtk_tree_path_new_from_string(path);
        gtk_tree_model_get_iter(GTK_TREE_MODEL(blocks_store), &iter, tree_path);
        index = gtk_tree_path_get_indices(tree_path)[0];
        gtk_tree_path_free(tree_path);

        /* Toggle its value */
        memset(&value, 0, sizeof (value));
        gtk_tree_model_get_value(GTK_TREE_MODEL(blocks_store), &iter, 0,
                                 &value);
        enabled = !g_value_get_boolean(&value);
        gtk_list_store_set(blocks_store, &iter, 0, enabled, -1);
        unicode_block_toggle(index, enabled);
}

static GtkWidget *create_blocks_list(void)
{
        GtkWidget *view, *scrolled;
        GtkTreeIter iter;
        GtkTreeViewColumn *column;
        GtkListStore *blocks_store;
        GtkCellRenderer *renderer;
        UnicodeBlock *block;

        /* Tree view */
        blocks_store = gtk_list_store_new(2, G_TYPE_BOOLEAN, G_TYPE_STRING);
        view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(blocks_store));
        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
        gtk_tooltips_set_tip(tooltips, view,
                             "Controls which blocks are enabled for "
                             "recognition and appear in the training mode "
                             "combo box.", NULL);

        /* Column */
        column = gtk_tree_view_column_new();
        gtk_tree_view_insert_column(GTK_TREE_VIEW(view), column, 0);
        renderer = gtk_cell_renderer_toggle_new();
        g_signal_connect(G_OBJECT(renderer), "toggled",
                         G_CALLBACK(unicode_block_toggled), blocks_store);
        gtk_tree_view_column_pack_start(column, renderer, FALSE);
        gtk_tree_view_column_add_attribute(column, renderer, "active", 0);
        renderer = gtk_cell_renderer_text_new();
        gtk_tree_view_column_pack_start(column, renderer, TRUE);
        gtk_tree_view_column_add_attribute(column, renderer, "text", 1);

        /* Fill blocks list */
        block = unicode_blocks;
        while (block->name) {
                gtk_list_store_append(blocks_store, &iter);
                gtk_list_store_set(blocks_store, &iter, 0, block->enabled,
                                   1, block->name, -1);
                block++;
        }

        /* Scrolled window */
        scrolled = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
                                            GTK_SHADOW_ETCHED_IN);
        gtk_container_add(GTK_CONTAINER(scrolled), view);

        return scrolled;
}

/*
        Options dialog
*/

#define CELL_WIDTH_MIN  24
#define CELL_HEIGHT_MIN 48
#define CELL_HEIGHT_MAX 96

static GtkWidget *options_dialog = NULL, *cell_width_spin, *cell_height_spin,
                 *color_table;

static void close_dialog(void)
{
        gtk_widget_hide(options_dialog);
}

static void color_set(GtkColorButton *button, GdkColor *color)
{
        gtk_color_button_get_color(button, color);
        window_update_colors();
}

static void ink_color_set(void)
{
        cell_widget_set_cursor(TRUE);
}

static void xinput_enabled_toggled(void)
{
        cell_widget_enable_xinput(xinput_enabled);
}

static void spin_value_changed_int(GtkSpinButton *button, int *value)
{
        *value = (int)gtk_spin_button_get_value(button);
}

static void spin_value_changed_int_repack(GtkSpinButton *button, int *value)
{
        spin_value_changed_int(button, value);
        window_pack();
}

static void check_button_toggled(GtkToggleButton *button, int *value)
{
        *value = gtk_toggle_button_get_active(button);
}

static void check_button_toggled_repack(GtkToggleButton *button, int *value)
{
        check_button_toggled(button, value);
        window_pack();
}

static GtkWidget *label_new_markup(const char *s)
{
        GtkWidget *w;

        w = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(w), s);
        gtk_misc_set_alignment(GTK_MISC(w), 0, 0.5);
        return w;
}

static GtkWidget *spacer_new(int width, int height)
{
        GtkWidget *w;

        w = gtk_hbox_new(FALSE, 0);
        gtk_widget_set_size_request(w, width, height);
        return w;
}

static GtkWidget *spin_button_new_int(int min, int max, int *variable,
                                      int repack)
{
        GtkWidget *w;

        w = gtk_spin_button_new_with_range(min, max, 1.);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), *variable);
        g_signal_connect(G_OBJECT(w), "value-changed",
                         repack ? G_CALLBACK(spin_value_changed_int_repack) :
                                  G_CALLBACK(spin_value_changed_int), variable);
        return w;
}

static GtkWidget *check_button_new(const char *label, int *variable, int repack)
{
        GtkWidget *w;

        w = gtk_check_button_new_with_label(label);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), *variable);
        g_signal_connect(G_OBJECT(w), "toggled",
                         repack ? G_CALLBACK(check_button_toggled_repack) :
                                  G_CALLBACK(check_button_toggled), variable);
        return w;
}

static void cell_height_value_changed(void)
{
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(cell_width_spin),
                                  CELL_WIDTH_MIN, cell_height);
}

static void cell_width_value_changed(void)
{
        int min;

        min = CELL_HEIGHT_MIN > cell_width ? CELL_HEIGHT_MIN : cell_width;
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(cell_height_spin),
                                  min, CELL_HEIGHT_MAX);
}

static void style_colors_changed(void)
{
#if GTK_CHECK_VERSION(2, 10, 0)
        gtk_widget_set_sensitive(color_table, !style_colors);
        window_update_colors();
#endif
}

#if GTK_CHECK_VERSION(2, 14, 0)
static void help_clicked(void)
{
        GError *error = NULL;

        gtk_show_uri(gdk_screen_get_default(), CELLWRITER_URL, GDK_CURRENT_TIME,
                     &error);
        if (error)
                g_warning("Failed to launch help: %s", error->message);
}
#endif

static GtkWidget *create_color_table(void)
{
        GtkWidget *table;
        int i, entries;

        struct {
                const char *string;
                GdkColor *color;
                int reset_cursor;
        } colors[] = {
                { "<b>Custom colors:</b>", NULL, FALSE },
                { "Used cell:", &custom_active_color, FALSE },
                { "Blank cell:", &custom_inactive_color, FALSE },
                { "Highlight:", &custom_select_color, FALSE },
                { "Text and ink:", &custom_ink_color, TRUE  },
                { "Key face:", &custom_key_color, FALSE },
        };

        entries = (int)(sizeof (colors) / sizeof (*colors));
        table = gtk_table_new(entries, 2, TRUE);
        for (i = 0; i < entries; i++) {
                GtkWidget *w, *hbox;

                /* Headers */
                if (!colors[i].color)
                        w = label_new_markup(colors[i].string);

                /* Color label */
                else {
                        hbox = gtk_hbox_new(FALSE, 0);
                        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1),
                                           FALSE, FALSE, 0);
                        w = label_new_markup(colors[i].string);
                        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
                        gtk_misc_set_alignment(GTK_MISC(w), 0, 0.5);
                        w = hbox;
                }

                gtk_table_attach(GTK_TABLE(table), w, 0, 1, i, i + 1,
                                 GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 0);
                if (!colors[i].color)
                        continue;

                /* Attach color selection button */
                w = gtk_color_button_new_with_color(colors[i].color);
                g_signal_connect(G_OBJECT(w), "color-set",
                                 G_CALLBACK(color_set), colors[i].color);
                gtk_table_attach(GTK_TABLE(table), w, 1, 2, i, i + 1,
                                 GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 0);

                /* Some colors reset the cursor */
                if (colors[i].reset_cursor)
                        g_signal_connect(G_OBJECT(w), "color-set",
                                         G_CALLBACK(ink_color_set), NULL);
        }
        return table;
}

static void window_docking_changed(GtkComboBox *combo)
{
        int mode;

        mode = gtk_combo_box_get_active(combo);
        window_set_docked(mode);
}

static void create_dialog(void)
{
        GtkWidget *vbox, *hbox, *vbox2, *notebook, *w;

        if (options_dialog)
                return;
        vbox = gtk_vbox_new(FALSE, 0);

        /* Buttons box */
        hbox = gtk_hbutton_box_new();
        gtk_box_pack_end(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);
        gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_END);

#if GTK_CHECK_VERSION(2, 14, 0)
        /* Help button */
        gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_EDGE);
        w = gtk_button_new_from_stock(GTK_STOCK_HELP);
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        g_signal_connect(G_OBJECT(w), "clicked",
                         G_CALLBACK(help_clicked), NULL);
        gtk_tooltips_set_tip(tooltips, w, "Launch program website", NULL);
#endif

        /* Close button */
        w = gtk_button_new_with_label("Close");
        gtk_button_set_image(GTK_BUTTON(w),
                             gtk_image_new_from_stock(GTK_STOCK_CLOSE,
                                                      GTK_ICON_SIZE_BUTTON));
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        g_signal_connect(G_OBJECT(w), "clicked",
                         G_CALLBACK(close_dialog), NULL);

        gtk_box_pack_end(GTK_BOX(vbox), spacer_new(-1, 8), FALSE, TRUE, 0);

        /* Create notebook */
        notebook = gtk_notebook_new();
        gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

        /* View page */
        vbox2 = gtk_vbox_new(FALSE, 0);
        gtk_container_set_border_width(GTK_CONTAINER(vbox2), 8);
        w = gtk_label_new("Interface");
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox2, w);

        /* View -> Dimensions */
        w = label_new_markup("<b>Dimensions</b>");
        gtk_box_pack_start(GTK_BOX(vbox2), w, FALSE, FALSE, 0);

        /* View -> Dimensions -> Cell size */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = label_new_markup("Cells: ");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        cell_width_spin = spin_button_new_int(CELL_WIDTH_MIN, cell_height,
                                              &cell_width, TRUE);
        g_signal_connect(G_OBJECT(cell_width_spin), "value-changed",
                         G_CALLBACK(cell_width_value_changed), NULL);
        gtk_box_pack_start(GTK_BOX(hbox), cell_width_spin, FALSE, FALSE, 0);
        w = label_new_markup(" by ");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        cell_height_spin = spin_button_new_int(cell_width, CELL_HEIGHT_MAX,
                                               &cell_height, TRUE);
        cell_width_value_changed();
        g_signal_connect(G_OBJECT(cell_height_spin), "value-changed",
                         G_CALLBACK(cell_height_value_changed), NULL);
        gtk_box_pack_start(GTK_BOX(hbox), cell_height_spin, FALSE, FALSE, 0);
        w = label_new_markup(" pixels");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);

        /* View -> Dimensions -> Grid */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = label_new_markup("Grid: ");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = spin_button_new_int(6, 48, &cell_cols_pref, TRUE);
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = label_new_markup(" by ");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = spin_button_new_int(1, 8, &cell_rows_pref, TRUE);
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = label_new_markup(" cells");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);

        /* View -> Dimensions -> Keyboard size */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = label_new_markup("Keyboard: ");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = spin_button_new_int(KEYBOARD_SIZE_MIN, 1400, &keyboard_size, TRUE);
        gtk_spin_button_set_increments(GTK_SPIN_BUTTON(w), 16, 4);
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = label_new_markup(" pixels wide");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);

        /* View -> Window */
        gtk_box_pack_start(GTK_BOX(vbox2), spacer_new(-1, 8), FALSE, FALSE, 0);
        w = label_new_markup("<b>Window</b>");
        gtk_box_pack_start(GTK_BOX(vbox2), w, FALSE, FALSE, 0);

        /* View -> Window -> Button labels */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = check_button_new("Show button labels", &window_button_labels, TRUE);
        gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);

        /* View -> Window -> On-screen keyboard */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = check_button_new("Show on-screen keyboard",
                             &keyboard_enabled, TRUE);
        gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);

        /* View -> Window -> Enable */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = check_button_new("Enable extended input events",
                             &xinput_enabled, FALSE);
        g_signal_connect(G_OBJECT(w), "toggled",
                         G_CALLBACK(xinput_enabled_toggled), NULL);
        gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);
        gtk_tooltips_set_tip(tooltips, w,
                             "If you cannot write in the cells or the ink "
                             "does not appear where it should, you can try "
                             "disabling extended input events. Note that this "
                             "will disable the pen eraser.", NULL);

        /* View -> Window -> Docking */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = label_new_markup("Window docking: ");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = gtk_combo_box_new_text();
        gtk_combo_box_append_text(GTK_COMBO_BOX(w), "Disabled");
        gtk_combo_box_append_text(GTK_COMBO_BOX(w), "Top");
        gtk_combo_box_append_text(GTK_COMBO_BOX(w), "Bottom");
        gtk_combo_box_set_active(GTK_COMBO_BOX(w), window_docked);
        g_signal_connect(G_OBJECT(w), "changed",
                         G_CALLBACK(window_docking_changed), NULL);
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);

        /* View -> Status icon */
        gtk_box_pack_start(GTK_BOX(vbox2), spacer_new(-1, 8), FALSE, FALSE, 0);
        w = label_new_markup("<b>Status icon</b>");
        gtk_box_pack_start(GTK_BOX(vbox2), w, FALSE, FALSE, 0);

        /* View -> Status icon -> Enable */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = check_button_new("Open menu on left click",
                             &status_menu_left_click, FALSE);
        gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);

        /* Colors page */
        vbox2 = gtk_vbox_new(FALSE, 0);
        w = gtk_label_new("Colors");
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox2, w);
        gtk_container_set_border_width(GTK_CONTAINER(vbox2), 8);

        /* Colors -> Use style */
        w = check_button_new("Use default theme colors", &style_colors, FALSE);
        g_signal_connect(G_OBJECT(w), "toggled",
                         G_CALLBACK(style_colors_changed), NULL);
        gtk_box_pack_start(GTK_BOX(vbox2), w, FALSE, FALSE, 0);

        /* Colors -> Custom colors */
        gtk_box_pack_start(GTK_BOX(vbox2), spacer_new(-1, 8), FALSE, FALSE, 0);
        color_table = create_color_table();
        gtk_box_pack_start(GTK_BOX(vbox2), color_table, FALSE, FALSE, 0);
        style_colors_changed();

        /* Unicode page */
        vbox2 = gtk_vbox_new(FALSE, 0);
        w = gtk_label_new("Languages");
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox2, w);
        gtk_container_set_border_width(GTK_CONTAINER(vbox2), 8);

        /* Unicode -> Displayed blocks */
        w = label_new_markup("<b>Enabled Unicode blocks</b>");
        gtk_box_pack_start(GTK_BOX(vbox2), w, FALSE, FALSE, 0);
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(-1, 4), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);

        /* Unicode -> Blocks list */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = create_blocks_list();
        gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, TRUE, TRUE, 0);

        /* Recognition -> Duplicate glyphs */
        gtk_box_pack_start(GTK_BOX(vbox2), spacer_new(-1, 8), FALSE, FALSE, 0);
        w = label_new_markup("<b>Language options</b>");
        gtk_box_pack_start(GTK_BOX(vbox2), w, FALSE, FALSE, 0);

        /* Unicode -> Disable Latin letters */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = check_button_new("Disable Basic Latin letters",
                             &no_latin_alpha, TRUE);
        gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);
        gtk_tooltips_set_tip(tooltips, w,
                             "If you have trained both the Basic Latin block "
                             "and a block with characters similar to Latin "
                             "letters (for instance, Cyrillic) you can disable "
                             "the Basic Latin letters in order to use only "
                             "numbers and symbols from Basic Latin.", NULL);

        /* Unicode -> Right-to-left */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = check_button_new("Enable right-to-left mode",
                             &right_to_left, TRUE);
        gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);
        gtk_tooltips_set_tip(tooltips, w,
                             PACKAGE_NAME " will expect you to write from "
                             "the rightmost cell to the left and will pad "
                             "cells and create new lines accordingly.", NULL);

        /* Recognition page */
        vbox2 = gtk_vbox_new(FALSE, 0);
        gtk_container_set_border_width(GTK_CONTAINER(vbox2), 8);
        w = gtk_label_new("Recognition");
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox2, w);

        /* Recognition -> Samples */
        w = label_new_markup("<b>Training samples</b>");
        gtk_box_pack_start(GTK_BOX(vbox2), w, FALSE, FALSE, 0);

        /* Recognition -> Samples -> Train on input */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = check_button_new("Train on input when entering",
                             &train_on_input, FALSE);
        gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);
        gtk_tooltips_set_tip(tooltips, w,
                             "When enabled, input characters will be used as "
                             "training samples when 'Enter' is pressed. This "
                             "is a good way to quickly build up many samples, "
                             "but can generate poor samples if your writing "
                             "gets sloppy.", NULL);

        /* Recognition -> Samples -> Maximum */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = label_new_markup("Samples per character: ");
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        w = spin_button_new_int(2, SAMPLES_MAX, &samples_max, FALSE);
        gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);
        gtk_tooltips_set_tip(tooltips, w,
                             "The maximum number of training samples kept per "
                             "character. Lower this value if recognition is "
                             "too slow or the program uses too much memory.",
                             NULL);

        /* Recognition -> Word context */
        gtk_box_pack_start(GTK_BOX(vbox2), spacer_new(-1, 8), FALSE, FALSE, 0);
        w = label_new_markup("<b>Word context</b>");
        gtk_box_pack_start(GTK_BOX(vbox2), w, FALSE, FALSE, 0);

        /* Recognition -> Word context -> English */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = check_button_new("Enable English word context",
                             &wordfreq_enable, FALSE);
        gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);
        gtk_tooltips_set_tip(tooltips, w,
                             "Use a dictionary of the most frequent English "
                             "words to assist recognition. Also aids in "
                             "consistent recognition of numbers and "
                             "capitalization.", NULL);

        /* Recognition -> Preprocessor */
        gtk_box_pack_start(GTK_BOX(vbox2), spacer_new(-1, 8), FALSE, FALSE, 0);
        w = label_new_markup("<b>Preprocessor</b>");
        gtk_box_pack_start(GTK_BOX(vbox2), w, FALSE, FALSE, 0);

        /* Recognition -> Preprocessor -> Ignore stroke direction */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = check_button_new("Ignore stroke direction",
                             &ignore_stroke_dir, FALSE);
        gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);
        gtk_tooltips_set_tip(tooltips, w,
                             "Match input strokes with training sample strokes "
                             "that were drawn in the opposite direction. "
                             "Disabling this can boost recognition speed.",
                             NULL);

        /* Recognition -> Preprocessor -> Ignore stroke number */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), spacer_new(16, -1), FALSE, FALSE, 0);
        w = check_button_new("Match differing stroke numbers",
                             &ignore_stroke_num, FALSE);
        gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);
        gtk_tooltips_set_tip(tooltips, w,
                             "Match inputs to training samples that do not "
                             "have the same number of strokes. Disabling this "
                             "can boost recognition speed.", NULL);

        /* Create dialog window */
        options_dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        g_signal_connect(G_OBJECT(options_dialog), "delete_event",
                         G_CALLBACK(gtk_widget_hide_on_delete), NULL);
        gtk_window_set_destroy_with_parent(GTK_WINDOW(options_dialog), TRUE);
        gtk_window_set_resizable(GTK_WINDOW(options_dialog), TRUE);
        gtk_window_set_title(GTK_WINDOW(options_dialog), "CellWriter Setup");
        gtk_container_set_border_width(GTK_CONTAINER(options_dialog), 8);
        gtk_container_add(GTK_CONTAINER(options_dialog), vbox);
        if (!window_embedded)
                gtk_window_set_transient_for(GTK_WINDOW(options_dialog),
                                             GTK_WINDOW(window));
}

void options_dialog_open(void)
{
        create_dialog();
        gtk_widget_show_all(options_dialog);
}
