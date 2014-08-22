
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
#ifdef USING_LIBEGG
#include "libegg/eggtrayicon.h"
#endif

/* options.c */
void options_dialog_open(void);

/* window.c */
extern GtkWidget *window;

void window_toggle(void);

/*
        Status icon menu
*/

int status_menu_left_click = FALSE;

static GtkWidget *status_menu, *status_menu_show = NULL;
static GObject *status_icon = NULL;

#ifdef USING_LIBEGG
static void position_menu_libegg(GtkMenu *menu, int *x, int *y,
                                 gboolean *push_in, gpointer user_data)
/* Positions the menu relative to the libegg tray icon */
{
        GdkScreen *screen;
        GtkWidget *tray_icon = GTK_WIDGET(status_icon);
        GtkRequisition req;
        gint menu_xpos, menu_ypos;

        gtk_widget_size_request(GTK_WIDGET(menu), &req);
        gdk_window_get_origin(tray_icon->window, &menu_xpos, &menu_ypos);
        menu_xpos += tray_icon->allocation.x;
        menu_ypos += tray_icon->allocation.y;
        screen = gtk_widget_get_screen(tray_icon);
        if (menu_ypos > gdk_screen_get_height(screen) / 2)
                menu_ypos -= req.height;
        else
                menu_ypos += tray_icon->allocation.height;
        *x = menu_xpos;
        *y = menu_ypos;
        *push_in = TRUE;
}
#define POSITION_MENU_FUNC position_menu_libegg
#else
#define POSITION_MENU_FUNC gtk_status_icon_position_menu
#endif

static void status_menu_popup(GObject *status, guint button,
                              guint activate_time)
{
        GtkWidget *widget, *image;

        if (status_menu)
                gtk_widget_destroy(status_menu);
        status_menu = gtk_menu_new();

        /* Menu -> Show/Hide */
        if (GTK_WIDGET_VISIBLE(window))
                status_menu_show = gtk_menu_item_new_with_label("Hide");
        else
                status_menu_show = gtk_menu_item_new_with_label("Show");
        g_signal_connect(G_OBJECT(status_menu_show), "activate",
                         G_CALLBACK(window_toggle), NULL);
        gtk_menu_attach(GTK_MENU(status_menu), status_menu_show, 0, 1, 0, 1);

        /* Menu -> Setup */
        widget = gtk_image_menu_item_new_with_label("Setup");
        image = gtk_image_new();
        gtk_image_set_from_stock(GTK_IMAGE(image), GTK_STOCK_PREFERENCES,
                                 GTK_ICON_SIZE_MENU);
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(widget), image);
        g_signal_connect(G_OBJECT(widget), "activate",
                         G_CALLBACK(options_dialog_open), 0);
        gtk_menu_attach(GTK_MENU(status_menu), widget, 0, 1, 1, 2);

        /* Menu -> Separator */
        widget = gtk_separator_menu_item_new();
        gtk_menu_attach(GTK_MENU(status_menu), widget, 0, 1, 2, 3);

        /* Menu -> Close */
        widget = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, NULL);
        g_signal_connect(G_OBJECT(widget), "activate",
                         G_CALLBACK(gtk_main_quit), NULL);
        gtk_menu_attach(GTK_MENU(status_menu), widget, 0, 1, 3, 4);

        /* Popup the menu */
        gtk_widget_show_all(status_menu);
        gtk_menu_popup(GTK_MENU(status_menu), NULL, NULL,
                       POSITION_MENU_FUNC, status_icon,
                       button, activate_time);
}

#ifdef USING_LIBEGG

/*
        Status icon with LibEgg
*/

/* This section is based largely on gtkdocklet-x11.c from the Pidgin project.
   Use of libegg is deprecated from GTK 2.10 onwards so the libegg tray icon
   will only be compiled for GTK 2.8 and earlier. */

static GtkWidget *status_image;

static gboolean button_press_event(GtkWidget *widget, GdkEventButton *event)
{
        /* Don't process double clicks */
        if (event->type != GDK_BUTTON_PRESS)
                return TRUE;

        /* Toggle window with left click */
        if (event->button == 1) {
                if (status_menu_left_click)
                        status_menu_popup(status_icon, event->button,
                                          event->time);
                else
                        window_toggle();
        }

        /* Show popup menu with right click */
        else if (event->button == 3)
                status_menu_popup(status_icon, event->button, event->time);

        return TRUE;
}

static void status_icon_size_allocate(GtkWidget *widget, GtkAllocation *alloc)
/* Sets the status icon image size to match the tray icon allocation size */
{
        gtk_image_set_pixel_size(GTK_IMAGE(status_image), alloc->height);
}

void status_icon_create(void)
/* Create the system tray icon and associated menu */
{
        GtkWidget *box;
        char *icon_path;

        /* Use the libegg tray icon to create a status icon in the tray */
        status_icon = G_OBJECT(egg_tray_icon_new(PACKAGE_NAME));
        box = gtk_event_box_new();

        /* Create the system tray icon */
        icon_path = g_build_filename(DATADIR, ICON_PATH PACKAGE ".svg", NULL);
        status_image = gtk_image_new_from_icon_name(PACKAGE,
                                                   GTK_ICON_SIZE_SMALL_TOOLBAR);
        gtk_container_add(GTK_CONTAINER(box), status_image);
        gtk_container_add(GTK_CONTAINER(status_icon), box);
        gtk_widget_show_all(GTK_WIDGET(status_icon));
        g_signal_connect(G_OBJECT(box), "button-press-event",
                         G_CALLBACK(button_press_event), NULL);
        g_signal_connect(G_OBJECT(status_icon), "size-allocate",
                         G_CALLBACK(status_icon_size_allocate), NULL);
}

int status_icon_embedded(void)
{
        /* FIXME Doesn't actually test if the icon is embedded because this
                 function is called before the icon has had a chance to
                 embed! */
        return status_icon != NULL;
}

#else /* USING_LIBEGG */

/*
        Status icon with GtkStatusIcon
*/

static void status_icon_activate(void)
{
        if (status_menu_left_click)
                status_menu_popup(status_icon, 1, gtk_get_current_event_time());
        else
                window_toggle();
}

void status_icon_create(void)
/* Create the system tray icon and associated menu */
{
        char *icon_path;
        GError *error = NULL;
        GdkPixbuf *pixbuf;

        /* Create the system tray icon */
        icon_path = g_build_filename(DATADIR, ICON_PATH PACKAGE ".svg", NULL);
        if (!(pixbuf = gdk_pixbuf_new_from_file(icon_path, &error))) {
                status_icon = NULL;
                g_warning("Failed to load status icon '%s': %s",
                          icon_path, error->message);
                return;
        }
        status_icon = G_OBJECT(gtk_status_icon_new_from_pixbuf(pixbuf));
        g_object_unref(pixbuf);
        g_signal_connect(status_icon, "activate",
                         G_CALLBACK(status_icon_activate), NULL);
        g_signal_connect(status_icon, "popup-menu",
                         G_CALLBACK(status_menu_popup), NULL);
        gtk_status_icon_set_visible(GTK_STATUS_ICON(status_icon), TRUE);
}

int status_icon_embedded(void)
{
        return status_icon != NULL && 
               gtk_status_icon_is_embedded(GTK_STATUS_ICON(status_icon));
}

#endif /* !USING_LIBEGG */
