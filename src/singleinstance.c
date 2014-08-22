
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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/*
        Single-instance checks
*/

static GIOChannel *io_channel;
static SingleInstanceFunc on_dupe;
static char *path;

void single_instance_cleanup(void)
{
        if (io_channel) {
                g_io_channel_unref(io_channel);
                io_channel = NULL;
        }
        if (path && unlink(path) == -1)
                log_errno("Failed to unlink program FIFO");
}

static gboolean on_fifo_input(GIOChannel *src, GIOCondition cond, gpointer data)
{
        GError *error;
        ssize_t len;
        gsize bytes_read;
        char buf[2];

        if (!io_channel || !on_dupe)
                return FALSE;
        error = NULL;
        len = g_io_channel_read_chars(io_channel, buf, 1, &bytes_read, &error);

        /* Error reading for some reason */
        if (error) {
                single_instance_cleanup();
                return FALSE;
        }

        /* Handle the one-byte message */
        buf[1] = 0;
        if (len > 0)
                on_dupe(buf);
        return TRUE;
}

int single_instance_init(SingleInstanceFunc func, const char *str)
{
        int fifo;

        on_dupe = func;
        path = g_build_filename(g_get_home_dir(), "." PACKAGE, "fifo", NULL);

        /* If we can open the program FIFO in write-only mode then we must
           have a reader process already running. We send it a one-byte
           message and quit. */
        if ((fifo = open(path, O_WRONLY | O_NONBLOCK)) > 0) {
                size_t written = write(fifo, str, 1);
                if (!written) {
                  g_debug("Failed to write to program FIFO\n");
                }
                close(fifo);
                return TRUE;
        }

        /* The FIFO can be left over from a previous instance if the program
           crashes or is killed */
        if (g_file_test(path, G_FILE_TEST_EXISTS)) {
                g_debug("Program FIFO exists but is not opened on "
                        "read-only side, deleting\n");
                single_instance_cleanup();
        }

        /* Otherwise, create a read-only FIFO */
        if (mkfifo(path, S_IRUSR | S_IWUSR)) {
                log_errno("Failed to create program FIFO");
                return FALSE;
        }
        if ((fifo = open(path, O_RDWR | O_NONBLOCK)) == -1) {
                log_errno("Failed to open FIFO for reading");
                return FALSE;
        }

        /* Open and watch the fifo to become readable */
	io_channel = g_io_channel_unix_new(fifo);
	g_io_add_watch(io_channel, G_IO_IN, on_fifo_input, NULL);
        return FALSE;
}
