
/*

cellwriter -- a character recognition input method
Copyright (C) 2007 Michael Levin <risujin@risujin.org>

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

#include <math.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <glib.h>
#include <glib/gunicode.h>

/* This program is capable of running various tests useful for the
   development of CellWriter. It does NOT test whether CellWriter can or did
   install succesfully. */

/*
        Test functions
*/

KeyCode shift_key;

void print_modmap(void)
{
        XModifierKeymap *xmk;
        int i, j;

        xmk = XGetModifierMapping(GDK_DISPLAY());
        for (i = 0; i < 8; i++) {
                g_print("Modifier %d:", i);
                for (j = 0; j < xmk->max_keypermod; j++)
                        g_print(" %3d",
                                xmk->modifiermap[i * xmk->max_keypermod + j]);
                g_print("\n");
        }
}

void print_keymap(void)
{
        KeySym *keysym;
        int i, j, min, max, numcodes;

        XDisplayKeycodes(GDK_DISPLAY(), &min, &max);
        keysym = XGetKeyboardMapping(GDK_DISPLAY(), min, max - min, &numcodes);
        for (i = 0; i <= max - min; i++) {
                g_print("KeyCode %3d:", min + i);
                for (j = 0; j < numcodes; j++)
                        g_print(" %8x", (unsigned int)keysym[i * numcodes + j]);
                g_print("\n");
        }
        XFree(keysym);
}

void print_colormap(void)
{
        GdkColormap *cm = NULL;
        int i;

        cm = gdk_colormap_get_system();
        if (!cm || !cm->colors) {
                g_print("NULL colormap!");
                return;
        }
        g_print("%d colors in system map:\n", cm->size);
        for (i = 0; i < cm->size; i++)
                g_print("%02d: (%2d, %2d, %2d)\n", i, cm->colors[i].red,
                        cm->colors[i].green, cm->colors[i].blue);
}

void print_gdkcolor_5(GdkColor array[5])
{
        int i;
        char *type[] = { "      normal",
                         "      active",
                         "    prelight",
                         "    selected",
                         " insensitive" };

        for (i = 0; i < 5; i++)
                g_print("%s: (%3d, %3d, %3d)\n",
                        type[i],
                        array[i].red * 255 / 65535,
                        array[i].green * 255 / 65535,
                        array[i].blue * 255 / 65535);
}

void print_style_colors(void)
{
        GtkWidget *widget;
        GtkStyle *style;

        widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_widget_show(widget);
        style = widget->style;
        g_print("\nfg --\n");
        print_gdkcolor_5(widget->style->fg);
        g_print("\nbg --\n");
        print_gdkcolor_5(widget->style->bg);
        g_print("\nlight --\n");
        print_gdkcolor_5(widget->style->light);
        g_print("\ndark --\n");
        print_gdkcolor_5(widget->style->dark);
        g_print("\nmid --\n");
        print_gdkcolor_5(widget->style->mid);
        g_print("\ntext --\n");
        print_gdkcolor_5(widget->style->text);
        g_print("\nbase --\n");
        print_gdkcolor_5(widget->style->base);
        gtk_widget_destroy(widget);
}

void print_dirs(void)
{
        g_print("        Home: %s\n", g_get_home_dir());
        /*g_print("   User data: %s\n", g_get_user_data_dir());
        g_print(" User config: %s\n", g_get_user_config_dir());
        g_print("  User cache: %s\n", g_get_user_cache_dir());*/
        g_print("         Tmp: %s\n", g_get_tmp_dir());
        g_print("     Current: %s\n", g_get_current_dir());
}

#define RD_TRIALS 10000

void random_difference(void)
{
        int i, r1, r2, rd, diff;

        srand(time(NULL));
        for (i = 0, diff = 0; i < RD_TRIALS; i++) {
                r1 = rand();
                r2 = rand();
                rd = r1 - r2;
                if (rd < 0)
                        rd = -rd;
                diff += rd;
        }
        g_print("Random difference: %g\n",
                ((double)diff) / RD_TRIALS / RAND_MAX);
}

/*
        Test mechanism
*/

#define TESTS (sizeof (tests) / sizeof (*tests))

struct {
        const char *name;
        void (*func)(void);
} tests[] = {
        { "modmap",       print_modmap       },
        { "keymap",       print_keymap       },
        { "colormap",     print_colormap     },
        { "dirs",         print_dirs         },
        { "style_colors", print_style_colors },
        { "rand_diff",    random_difference  },
};

int main(int argc, char *argv[])
{
        int i;

        gtk_init(&argc, &argv);

        /* Display test info */
        if (argc < 2) {
                g_print("%d available tests --", TESTS);
                for (i = 0; i < TESTS; i++)
                        g_print(" %s", tests[i].name);
                g_print("\n");
                return 0;
        }

        /* Run tests */
        for (i = 1; i < argc; i++) {
                int j;

                for (j = 0; j < TESTS; j++)
                        if (!strcasecmp(tests[j].name, argv[i])) {
                                g_print("Running test '%s' --\n",
                                        tests[j].name);
                                tests[j].func();
                                g_print("\n");
                                break;
                        }
                if (j == TESTS)
                        g_print("Test '%s' not found!\n", argv[i]);
        }

        return 0;
}
