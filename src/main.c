
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
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>

/* recognize.c */
extern int strength_sum;

void recognize_init(void);
void recognize_sync(void);
void samples_write(void);
void sample_read(void);
void update_enabled_samples(void);
int samples_loaded(void);

/* cellwidget.c */
extern int training, corrections, rewrites, characters, inputs;

void cell_widget_cleanup(void);

/* options.c */
void options_sync(void);

/* keyevent.c */
extern int key_recycles, key_overwrites, key_disable_overwrite;

int key_event_init(void);
void key_event_cleanup(void);
void bad_keycodes_write(void);
void bad_keycodes_read(void);

/*
        Variable argument parsing
*/

char *nvav(int *plen, const char *fmt, va_list va)
{
	static char buffer[2][16000];
	static int which;
	int len;

	which = !which;
	len = g_vsnprintf(buffer[which], sizeof(buffer[which]), fmt, va);
	if (plen)
		*plen = len;
	return buffer[which];
}

char *nva(int *plen, const char *fmt, ...)
{
	va_list va;
	char *string;

	va_start(va, fmt);
	string = nvav(plen, fmt, va);
	va_end(va);
	return string;
}

char *va(const char *fmt, ...)
{
	va_list va;
	char *string;

	va_start(va, fmt);
	string = nvav(NULL, fmt, va);
	va_end(va);
	return string;
}

/*
        GDK colors
*/

static int check_color_range(int value)
{
        if (value < 0)
                value = 0;
        if (value > 65535)
                value = 65535;
        return value;
}

void scale_gdk_color(const GdkColor *base, GdkColor *out, double value)
{
        out->red = check_color_range(base->red * value);
        out->green = check_color_range(base->green * value);
        out->blue = check_color_range(base->blue * value);
}

void gdk_color_to_hsl(const GdkColor *src,
                      double *hue, double *sat, double *lit)
/* Source: http://en.wikipedia.org/wiki/HSV_color_space
           #Conversion_from_RGB_to_HSL_or_HSV */
{
        double max = src->red, min = src->red;

        /* Find largest and smallest channel */
        if (src->green > max)
                max = src->green;
        if (src->green < min)
                min = src->green;
        if (src->blue > max)
                max = src->blue;
        if (src->blue < min)
                min = src->blue;

        /* Hue depends on max/min */
        if (max == min)
                *hue = 0;
        else if (max == src->red) {
                *hue = (src->green - src->blue) / (max - min) / 6.;
                if (*hue < 0.)
                        *hue += 1.;
        } else if (max == src->green)
                *hue = ((src->blue - src->red) / (max - min) + 2.) / 6.;
        else if (max == src->blue)
                *hue = ((src->red - src->green) / (max - min) + 4.) / 6.;

        /* Lightness */
        *lit = (max + min) / 2 / 65535;

        /* Saturation depends on lightness */
        if (max == min)
                *sat = 0.;
        else if (*lit <= 0.5)
                *sat = (max - min) / (max + min);
        else
                *sat = (max - min) / (65535 * 2 - (max + min));
}

void hsl_to_gdk_color(GdkColor *src, double hue, double sat, double lit)
/* Source: http://en.wikipedia.org/wiki/HSV_color_space
           #Conversion_from_RGB_to_HSL_or_HSV */
{
        double q, p, t[3];
        int i;

        /* Clamp ranges */
        if (hue < 0)
                hue -= (int)hue - 1.;
        if (hue > 1)
                hue -= (int)hue;
        if (sat < 0)
                sat = 0;
        if (sat > 1)
                sat = 1;
        if (lit < 0)
                lit = 0;
        if (lit > 1)
                lit = 1;

        /* Special case for gray */
        if (sat == 0.) {
                src->red = lit * 65535;
                src->green = lit * 65535;
                src->blue = lit * 65535;
                return;
        }

        q = (lit < 0.5) ? lit * (1 + sat) : lit + sat - (lit * sat);
        p = 2 * lit - q;
        t[0] = hue + 1 / 3.;
        t[1] = hue;
        t[2] = hue - 1 / 3.;
        for (i = 0; i < 3; i++) {
                if (t[i] < 0.)
                        t[i] += 1.;
                if (t[i] > 1.)
                        t[i] -= 1.;
                if (t[i] >= 2 / 3.)
                        t[i] = p;
                else if (t[i] >= 0.5)
                        t[i] = p + ((q - p) * 6 * (2 / 3. - t[i]));
                else if (t[i] >= 1 / 6.)
                        t[i] = q;
                else
                        t[i] = p + ((q - p) * 6 * t[i]);
        }
        src->red = t[0] * 65535;
        src->green = t[1] * 65535;
        src->blue = t[2] * 65535;
}

void shade_gdk_color(const GdkColor *base, GdkColor *out, double value)
{
        double hue, sat, lit;

        gdk_color_to_hsl(base, &hue, &sat, &lit);
        sat *= value;
        lit += value - 1.;
        hsl_to_gdk_color(out, hue, sat, lit);
}

void highlight_gdk_color(const GdkColor *base, GdkColor *out, double value)
/* Shades brighter or darker depending on the luminance of the base color */
{
        double lum = (0.3 * base->red + 0.59 * base->green +
                      0.11 * base->blue) / 65535;

        value = lum < 0.5 ? 1. + value : 1. - value;
        shade_gdk_color(base, out, value);
}

/*
        Profile
*/

/* Profile format version */
#define PROFILE_VERSION 0

/* The profile filename not including the path or postfix */
#define PROFILE_FILENAME "profile"

/* Added to the end of the profile backup filename */
#define BACKUP_POSTFIX ".backup"

int profile_read_only, keyboard_only = FALSE;

static GIOChannel *channel;
static char profile_buf[4096], *profile_end = NULL, profile_swap,
            *force_profile = NULL;
static int force_read_only;

static int is_space(int ch)
{
        return ch == ' ' || ch == '\t' || ch == '\r';
}

static int profile_open_channel(const char *type, const char *path)
/* Tries to open a profile channel, returns TRUE if it succeeds */
{
        GError *error = NULL;

        if (!g_file_test(path, G_FILE_TEST_IS_REGULAR) &&
            g_file_test(path, G_FILE_TEST_EXISTS)) {
                g_warning("Failed to open %s profile '%s': Not a regular file",
                          type, path);
                return FALSE;
        }
        channel = g_io_channel_new_file(path, profile_read_only ? "r" : "w",
                                        &error);
        if (!error) {
                g_debug("Opened %s profile '%s' for %s",
                        type, path, profile_read_only ? "reading" : "writing");
                return TRUE;
        }
        g_warning("Failed to open %s profile '%s' for %s: %s",
                  type, path, profile_read_only ? "reading" : "writing",
                  error->message);
        g_error_free(error);
        return FALSE;
}

static void create_user_dir(void)
/* Make sure the user directory exists */
{
        char *path;

        path = g_build_filename(g_get_home_dir(), "." PACKAGE, NULL);
        if (g_mkdir_with_parents(path, 0755))
                g_warning("Failed to create user directory '%s'", path);
        g_free(path);
}

static int profile_open_read(void)
/* Open the profile file for reading. Returns TRUE if the profile was opened
   successfully. */
{
        char *path;

        profile_read_only = TRUE;

        /* Try opening a command-line specified profile first */
        if (force_profile) {
                if (profile_open_channel("command-line specified",
                                         force_profile))
                        return TRUE;

                /* Try opening a backup of the command-line specified profile */
                path = g_build_filename(force_profile, BACKUP_POSTFIX, NULL);
                if (profile_open_channel("backup of command-line specified",
                                         path)) {
                        g_free(path);
                        return TRUE;
                }
        }

        /* Open user's profile */
        path = g_build_filename(g_get_home_dir(), "." PACKAGE,
                                PROFILE_FILENAME, NULL);
        if (profile_open_channel("user's", path)) {
                g_free(path);
                return TRUE;
        }
        g_free(path);

        /* Open user's backup profile */
        path = g_build_filename(g_get_home_dir(), "." PACKAGE,
                                PROFILE_FILENAME BACKUP_POSTFIX, NULL);
        if (profile_open_channel("user's backup", path)) {
                g_free(path);
                return TRUE;
        }
        g_free(path);

        /* Open system profile */
        path = g_build_filename(PKGDATADIR, PROFILE_FILENAME, NULL);
        if (profile_open_channel("system", path)) {
                g_free(path);
                return TRUE;
        }
        g_free(path);

        return FALSE;
}

static int move_file(char *from, char *to)
/* First tries to rename the file to the new location. The standard library
   rename() cannot move across filesystems so we need a backup that can do that.
   This function will copy a file, byte-by-byte if necessary. */
{
        GError *error = NULL;
        GIOChannel *src_channel, *dest_channel;
        gchar buffer[4096];
        int errno;

        /* First try to rename the file to the destination */
        errno = rename(from, to);
        if (!errno)
                return TRUE;
        log_errno("rename() failed to move the file");

        /* Open source file for reading */
        src_channel = g_io_channel_new_file(from, "r", &error);
        if (error) {
                g_warning("move_file() failed to open src '%s': %s",
                          from, error->message);
                return FALSE;
        }

        /* Open destination file for writing */
        dest_channel = g_io_channel_new_file(to, "w", &error);
        if (error) {
                g_warning("move_file() failed to open dest '%s': %s",
                          to, error->message);
                g_io_channel_unref(src_channel);
                return FALSE;
        }

        /* Copy data in blocks */
        for (;;) {
                gsize bytes_read, bytes_written;

                /* Read a block in */
                if (g_io_channel_read_chars(src_channel, buffer,
                                            sizeof (buffer), &bytes_read,
                                            &error) != G_IO_STATUS_NORMAL)
                        break;

                /* Write the block out */
                g_io_channel_write_chars(dest_channel, buffer, bytes_read,
                                         &bytes_written, &error);
                if (bytes_written < bytes_read || error) {
                        g_warning("move_file() error writing to '%s': %s",
                                  to, error->message);
                        g_io_channel_unref(src_channel);
                        g_io_channel_unref(dest_channel);
                        return FALSE;
                }
        }

        /* Close channels */
        g_io_channel_unref(src_channel);
        g_io_channel_unref(dest_channel);

        g_debug("move_file() copied '%s' to '%s'", from, to);

        /* Should be safe to delete the old file now */
        if (remove(from))
                log_errno("move_file() failed to delete src");

        return TRUE;
}

static int profile_open_write(void)
/* Open a temporary profile file for writing. Returns TRUE if the profile was
   opened successfully. */
{
        GError *error;
        char *path;
        char *backup_path;

        if (force_read_only) {
                g_debug("Not saving profile, opened in read-only mode");
                return FALSE;
        }
        profile_read_only = FALSE;

        /* Use command-line specified profile path first then the user's
           home directory profile */
        path = force_profile;
        if (!path)
                path = g_build_filename(g_get_home_dir(),
                                        "." PACKAGE, PROFILE_FILENAME, NULL);

        /* Backup the current profile, if it exists */
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR) &&
            g_file_test(path, G_FILE_TEST_EXISTS)) {
                backup_path = g_strconcat(path, BACKUP_POSTFIX, NULL);
                if (!move_file(path, backup_path))
                        g_warning("Failed to backup the profile");
                else
                        g_debug("Backed up profile '%s' to '%s'", path,
                                backup_path);
                g_free(backup_path);
        } else {
          g_debug("No profile found, not backing up");
        }

        /* Open user's profile */
        if (profile_open_channel("user's", path)) {
                g_free(path);
                return TRUE;
        }
        g_free(path);
        g_warning("Failed to open profile for writing: %s", error->message);
        return FALSE;
}

static int profile_close(void)
/* Close the currently open profile */
{
        if (!channel)
                return FALSE;
        g_io_channel_unref(channel);
        return TRUE;
}

const char *profile_read(void)
/* Read a token from the open profile */
{
        GError *error = NULL;
        char *token;

        if (!channel)
                return "";
        if (!profile_end)
                profile_end = profile_buf;
        *profile_end = profile_swap;

seek_profile_end:

        /* Get the next token from the buffer */
        for (; is_space(*profile_end); profile_end++);
        token = profile_end;
        for (; *profile_end && !is_space(*profile_end) && *profile_end != '\n';
             profile_end++);

        /* If we run out of buffer space, read a new chunk */
        if (!*profile_end) {
                unsigned int token_size;
                gsize bytes_read;

                /* If we are out of space and we are not on the first or
                   the last byte, then we have run out of things to read */
                if (profile_end > profile_buf &&
                    profile_end < profile_buf + sizeof (profile_buf) - 1) {
                        profile_swap = 0;
                        return "";
                }

                /* Move what we have of the token to the start of the buffer,
                   fill the rest of the buffer with new data and start reading
                   from the beginning */
                token_size = profile_end - token;
                if (token_size >= sizeof (profile_buf) - 1) {
                        g_warning("Oversize token in profile");
                        return "";
                }
                memmove(profile_buf, token, token_size);
                g_io_channel_read_chars(channel, profile_buf + token_size,
                                        sizeof (profile_buf) - token_size - 1,
                                        &bytes_read, &error);
                if (error) {
                        g_warning("Read error: %s", error->message);
                        return "";
                }
                if (bytes_read < 1) {
                        profile_swap = 0;
                        return "";
                }
                profile_end = profile_buf;
                profile_buf[token_size + bytes_read] = 0;
                goto seek_profile_end;
        }

        profile_swap = *profile_end;
        *profile_end = 0;
        return token;
}

int profile_read_next(void)
/* Skip to the next line
   FIXME should skip multiple blank lines */
{
        const char *s;

        do {
                s = profile_read();
        } while (s[0]);
        if (profile_swap == '\n') {
                profile_swap = ' ';
                return TRUE;
        }
        return FALSE;
}

int profile_write(const char *str)
/* Write a string to the open profile */
{
        GError *error = NULL;
        gsize bytes_written;

        if (profile_read_only || !str)
                return 0;
        if (!channel)
                return 1;
        g_io_channel_write_chars(channel, str, strlen(str), &bytes_written,
                                 &error);
        if (error) {
                g_warning("Write error: %s", error->message);
                return 1;
        }
        return 0;
}

int profile_sync_int(int *var)
/* Read or write an integer variable depending on the profile mode */
{
        if (profile_read_only) {
                const char *s;
                int n;

                s = profile_read();
                if (s[0]) {
                        n = atoi(s);
                        if (n || (s[0] == '0' && !s[1])) {
                                *var = n;
                                return 0;
                        }
                }
        } else
                return profile_write(va(" %d", *var));
        return 1;
}

int profile_sync_short(short *var)
/* Read or write a short integer variable depending on the profile mode */
{
        int value = *var;

        if (profile_sync_int(&value))
                return 1;
        if (!profile_read_only)
                return 0;
        *var = (short)value;
        return 0;
}

void version_read(void)
{
        int version;

        version = atoi(profile_read());
        if (version != 0)
                g_warning("Loading a profile with incompatible version %d "
                          "(expected %d)", version, PROFILE_VERSION);
}

/*
        Main and signal handling
*/

#define NUM_PROFILE_CMDS (sizeof (profile_cmds) / sizeof (*profile_cmds))

int profile_line, log_level = 4;

static char *log_filename = NULL;
static FILE *log_file = NULL;
static int ignore_fifo;

/* Profile commands table */
static struct {
        const char *name;
        void (*read_func)(void);
        void (*write_func)(void);
} profile_cmds[] = {
        { "version",      version_read,      NULL               },
        { "window",       window_sync,       window_sync        },
        { "options",      options_sync,      options_sync       },
        { "recognize",    recognize_sync,    recognize_sync     },
        { "blocks",       blocks_sync,       blocks_sync        },
        { "bad_keycodes", bad_keycodes_read, bad_keycodes_write },
        { "sample",       sample_read,       samples_write      },
};

/* Command line arguments */
static GOptionEntry command_line_opts[] = {
        { "log-level", 0, 0, G_OPTION_ARG_INT, &log_level,
          "Log threshold (0=silent, 7=debug)", "4" },
        { "log-file", 0, 0, G_OPTION_ARG_STRING, &log_filename,
          "Log file to use instead of stdout", PACKAGE ".log" },
        { "xid", 0, 0, G_OPTION_ARG_NONE, &window_embedded,
          "Embed the main window (XEMBED)", NULL },
        { "show-window", 0, 0, G_OPTION_ARG_NONE, &window_force_show,
          "Show the main window", NULL },
        { "hide-window", 0, 0, G_OPTION_ARG_NONE, &window_force_hide,
          "Don't show the main window", NULL },
        { "window-x", 0, 0, G_OPTION_ARG_INT, &window_force_x,
          "Horizontal window position", "512" },
        { "window-y", 0, 0, G_OPTION_ARG_INT, &window_force_y,
          "Vertical window position", "768" },
        { "dock-window", 0, 0, G_OPTION_ARG_INT, &window_force_docked,
          "Docking (0=off, 1=top, 2=bottom)", "0" },
        { "window-struts", 0, 0, G_OPTION_ARG_NONE, &window_struts,
          "Reserve space when docking, see manpage", NULL },
        { "keyboard-only", 0, 0, G_OPTION_ARG_NONE, &keyboard_only,
          "Show on-screen keyboard only", NULL },
        { "profile", 0, 0, G_OPTION_ARG_STRING, &force_profile,
          "Full path to profile file to load", "profile" },
        { "read-only", 0, 0, G_OPTION_ARG_NONE, &force_read_only,
          "Do not save changes to the profile", NULL },
        { "disable-overwrite", 0, 0, G_OPTION_ARG_NONE, &key_disable_overwrite,
          "Do not modify the keymap", NULL },
        { "ignore-fifo", 0, 0, G_OPTION_ARG_NONE, &ignore_fifo,
          "Allow starting a second instance", NULL },

        /* Sentinel */
        { NULL, 0, 0, 0, NULL, NULL, NULL }
};

/* If any of these things happen, try to save and exit cleanly -- gdb is not
   affected by any of these signals being caught */
static int catch_signals[] = {
	SIGSEGV,
	SIGHUP,
	SIGINT,
	SIGTERM,
	SIGQUIT,
	SIGALRM,
        -1
};

/* Save the profile */
void save_profile(void) {
        if (!window_embedded && profile_open_write()) {
                unsigned int i;

                profile_write(va("version %d\n", PROFILE_VERSION));
                for (i = 0; i < NUM_PROFILE_CMDS; i++)
                        if (profile_cmds[i].write_func)
                                profile_cmds[i].write_func();
                if (profile_close())
                        g_debug("Profile saved");
        }
}

void cleanup(void)
{
        static int finished;

        /* Run once */
        if (finished) {
                g_debug("Cleanup already called");
                return;
        }
        finished = TRUE;
        g_message("Cleaning up");

        /* Explicit cleanup */
        cell_widget_cleanup();
        window_cleanup();
        key_event_cleanup();
        if (!window_embedded)
                single_instance_cleanup();
        save_profile();

        /* Close log file */
        if (log_file)
                fclose(log_file);
}

static void catch_sigterm(int sig)
/* Terminated by shutdown */
{
        g_warning("Caught signal %d, cleaning up", sig);
        cleanup();
        exit(1);
}

static void hook_signals(void)
/* Setup signal catchers */
{
        sigset_t sigset;
        int *ps;

        sigemptyset(&sigset);
        ps = catch_signals;
        while (*ps != -1) {
                signal(*ps, catch_sigterm);
                sigaddset(&sigset, *(ps++));
        }
        if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) == -1)
                log_errno("Failed to set signal blocking mask");
}

void log_print(const char *format, ...)
/* Print to the log file or stderr */
{
        FILE *file;
        va_list va;

        file = log_file;
        if (!file) {
                if (window_embedded)
                        return;
                file = stderr;
        }
        va_start(va, format);
        vfprintf(file, format, va);
        va_end(va);
}

void log_func(const gchar *domain, GLogLevelFlags level, const gchar *message)
{
        const char *prefix = "", *postfix = "\n", *pmsg;

        if (level > log_level)
                goto skip_print;

        /* Do not print empty messages */
        for (pmsg = message; *pmsg <= ' '; pmsg++)
                if (!*pmsg)
                        goto skip_print;

        /* Format the message */
        switch (level & G_LOG_LEVEL_MASK) {
        case G_LOG_LEVEL_DEBUG:
                prefix = "| ";
                break;
        case G_LOG_LEVEL_INFO:
        case G_LOG_LEVEL_MESSAGE:
                if (log_level > G_LOG_LEVEL_INFO) {
                        prefix = "\n";
                        postfix = ":\n";
                }
                break;
        case G_LOG_LEVEL_WARNING:
                if (log_level > G_LOG_LEVEL_INFO)
                        prefix = "* ";
                else if (log_level > G_LOG_LEVEL_WARNING)
                        prefix = "WARNING: ";
                else
                        prefix = PACKAGE ": ";
                break;
        case G_LOG_LEVEL_CRITICAL:
        case G_LOG_LEVEL_ERROR:
                if (log_level > G_LOG_LEVEL_INFO)
                        prefix = "* ERROR! ";
                else if (log_level > G_LOG_LEVEL_WARNING)
                        prefix = "ERROR: ";
                else
                        prefix = PACKAGE ": ERROR! ";
                break;
        default:
                break;
        }
        if (domain)
                log_print("%s[%s] %s%s", prefix, domain, message, postfix);
        else
                log_print("%s%s%s", prefix, message, postfix);

skip_print:
        if (level & G_LOG_FLAG_FATAL || level & G_LOG_LEVEL_ERROR)
                abort();
}

void trace_full(const char *file, const char *func, const char *format, ...)
{
        char buf[256];
        va_list va;

        if (LOG_LEVEL_TRACE > log_level)
                return;
        va_start(va, format);
        vsnprintf(buf, sizeof(buf), format, va);
        va_end(va);
        log_print(": %s:%s() %s\n", file, func, buf);
}

void log_errno(const char *string)
{
        log_func(NULL, G_LOG_LEVEL_WARNING,
                 va("%s: %s", string, strerror(errno)));
}

static void second_instance(char *str)
{
        g_debug("Received '%s' from fifo", str);
        if (str[0] == '0' || str[0] == 'H' || str[0] == 'h')
                window_hide();
        else if (str[0] == '1' || str[0] == 'S' || str[0] == 's')
                window_show();
        else if (str[0] == '2' || str[0] == 'T' || str[0] == 't')
                window_toggle();
}

int main(int argc, char *argv[])
{
        GError *error;
        const char *token;

        /* Initialize GTK+ */
        error = NULL;
        if (!gtk_init_with_args(&argc, &argv,
                                "- Grid-entry handwriting input panel "
                                "(version " PACKAGE_VERSION ")",
                                command_line_opts, NULL, &error))
                return 0;

        /* Setup log handler */
        log_level = 1 << log_level;
        g_log_set_handler(NULL, -1, (GLogFunc)log_func, NULL);

        /* Try to open the log-file */
        if (log_filename) {
                log_file = fopen(log_filename, "w");
                if (!log_file)
                        g_warning("Failed to open log-file '%s'", log_filename);
        }

        /* See if the program is already running */
        g_message("Starting " PACKAGE_STRING);
        create_user_dir();
        if (!window_embedded && !ignore_fifo) {
                const char *msg;

                msg = "2";
                if (window_force_show)
                        msg = "1";
                else if (window_force_hide)
                        msg = "0";
                if (single_instance_init((SingleInstanceFunc)second_instance,
                                         msg)) {
                        gdk_notify_startup_complete();
                        g_message(PACKAGE_NAME " already running, quitting");
                        return 0;
                }
        }

        /* Component initilization */
        if (key_event_init()) {
                GtkWidget *dialog;

                dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_OK,
                                                "Xtest extension not "
                                                "supported");
                gtk_window_set_title(GTK_WINDOW(dialog), "Initilization Error");
                gtk_message_dialog_format_secondary_text(
                        GTK_MESSAGE_DIALOG(dialog),
                        "Your Xserver does not support the Xtest extension. "
                        PACKAGE_NAME " cannot generate keystrokes without it.");
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
        }
        recognize_init();

        /* Read profile */
        if (profile_open_read()) {
                profile_line = 1;
                g_message("Parsing profile");
                do {
                        unsigned int i;

                        token = profile_read();
                        if (!token[0]) {
                                if (profile_read_next())
                                        continue;
                                break;
                        }
                        for (i = 0; i < NUM_PROFILE_CMDS; i++)
                                if (!g_ascii_strcasecmp(profile_cmds[i].name,
                                                        token)) {
                                        if (profile_cmds[i].read_func)
                                                profile_cmds[i].read_func();
                                        break;
                                }
                        if (i == NUM_PROFILE_CMDS)
                                g_warning("Unrecognized profile command '%s'",
                                          token);
                        profile_line++;
                } while (profile_read_next());
                profile_close();
                g_debug("Parsed %d commands", profile_line - 1);
        }

        /* After loading samples and block enabled/disabled information,
           update the samples */
        update_enabled_samples();

        /* Ensure that if there is a crash, data is saved properly */
        hook_signals();
        atexit(cleanup);

        /* Initialize the interface */
        g_message("Running interface");
        window_create();

        /* Startup notification is sent when the first window shows but in if
           the window was closed during last start, it won't show at all so
           we need to do this manually */
        gdk_notify_startup_complete();

        /* Run the interface */
        if (!samples_loaded() && !keyboard_only)
                startup_splash_show();
        gtk_main();
        cleanup();

        /* Session statistics */
        if (characters && inputs && log_level >= G_LOG_LEVEL_DEBUG) {
                g_message("Session statistics --");
                g_debug("Average strength: %d%%", strength_sum / inputs);
                g_debug("Rewrites: %d out of %d inputs (%d%%)",
                        rewrites, inputs, rewrites * 100 / inputs);
                g_debug("Corrections: %d out of %d characters (%d%%)",
                        corrections, characters,
                        corrections * 100 / characters);
                g_debug("KeyCodes overwrites: %d out of %d uses (%d%%)",
                        key_overwrites, key_overwrites + key_recycles,
                        key_recycles + key_overwrites ? key_overwrites * 100 /
                        (key_recycles + key_overwrites) : 0);
        }

        return 0;
}
