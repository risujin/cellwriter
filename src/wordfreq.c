
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

/* cellwidget.c */
const char *cell_widget_word(void);

/*
        Word frequency engine
*/

#ifndef DISABLE_WORDFREQ

/* TODO needs to be internationalized (wide char)
   TODO user-made words list
   TODO choose a list via GUI
   FIXME the frequency list contains "n't" etc as separate endings, this
         needs to be taken into consideration */

/* The number of word frequency entries to load */
#define WORDFREQS 15000

typedef struct {
        char string[24];
        int count;
} WordFreq;

int wordfreq_enable = TRUE;

static WordFreq wordfreqs[WORDFREQS + 1];
static int wordfreqs_len, wordfreqs_count;

void load_wordfreq(void)
/* Read in the word frequency file. The file format is: word\tcount\n */
{
        GIOChannel *channel;
        GError *error = NULL;
        char buf[64], *path;
        gsize bytes_read = 1;
        int i;

        wordfreqs[0].string[0] = 0;

        /* Try to open the user's word frequency file */
        path = g_build_filename(g_get_home_dir(), "." PACKAGE, "wordfreq",
                                NULL);
        channel = g_io_channel_new_file(path, "r", &error);
        if (error) {
                g_debug("User does not have a word frequency file, "
                        "loading system file");
                channel = NULL;
        }
        error = NULL;
        g_free(path);

        /* Open the word frequency file */
        if (!channel) {
                path = g_build_filename(PKGDATADIR, "wordfreq", NULL);
                channel = g_io_channel_new_file(path, "r", &error);
                if (error) {
                        g_warning("Failed to open system word frequency file "
                                  "'%s' for reading: %s", path, error->message);
                        g_free(path);
                        return;
                }
                g_free(path);
        }

        /* Read in every entry */
        g_debug("Parsing word frequency list");
        wordfreqs_count = 0;
        for (i = 0; bytes_read > 0 && i < WORDFREQS; i++) {
                char *pbuf;
                int swap, len;

                /* Read a line */
                pbuf = buf - 1;
                do {
                        g_io_channel_read_chars(channel, ++pbuf, 1,
                                                &bytes_read, &error);
                } while (bytes_read > 0 && *pbuf != '\n' &&
                         pbuf < buf + sizeof (buf));
                *pbuf = 0;

                /* Parse the word */
                pbuf = buf;
                while (*pbuf && *pbuf != '\t' && *pbuf != ' ')
                        pbuf++;
                if (buf == pbuf) {
                        i--;
                        continue;
                }
                swap = *pbuf;
                *pbuf = 0;
                len = pbuf - buf;
                if (len >= (int)sizeof (wordfreqs[i].string))
                        len = sizeof (wordfreqs[i].string) - 1;
                memcpy(wordfreqs[i].string, buf, len);
                wordfreqs[i].string[len] = 0;

                /* Parse the count */
                *pbuf = swap;
                while (*pbuf == ' ' || *pbuf == '\t')
                        pbuf++;
                wordfreqs_count += wordfreqs[i].count = log(atoi(pbuf));
        }
        wordfreqs[i].string[0] = 0;
        wordfreqs_len = i;
        g_io_channel_unref(channel);
        g_debug("%d words parsed", i);

        return;
}

void engine_wordfreq(void)
{
        Sample *sample;
        const char *pre, *post;
        int i, pre_len, post_len, chars[128];

        if (!wordfreq_enable)
                return;
        pre = cell_widget_word();
        pre_len = strlen(pre);
        post = pre + pre_len + 1;
        post_len = strlen(post);
        if (!pre_len && !post_len)
                return;
        memset(chars, 0, sizeof (chars));

        /* Numbers follow numbers */
        if (g_ascii_isdigit(pre[pre_len - 1])) {
                for (i = 0; i <= 9; i++)
                        chars['0' + i] = 1;
                goto apply_table;
        }

        /* Search the databases for matches (FIXME sort/index) */
        for (i = 0; i < wordfreqs_len; i++)
                if ((!pre_len ||
                     !g_ascii_strncasecmp(pre, wordfreqs[i].string, pre_len)) &&
                    (!post_len ||
                     !g_ascii_strncasecmp(post, wordfreqs[i].string + pre_len +
                                          1, post_len))) {
                        int ch = wordfreqs[i].string[pre_len],
                            ch_lower = ch, ch_upper = 0;

                        if (ch < 32 || ch >= 127)
                                continue;

                        /* Suggest proper case */
                        if (g_ascii_isalpha(ch)) {
                                ch_lower = g_ascii_tolower(ch);
                                ch_upper = g_ascii_toupper(ch);
                                if (pre_len > 1) {
                                        if (g_ascii_islower(pre[pre_len - 1]))
                                                ch_upper = 0;
                                        else
                                        if (g_ascii_isupper(pre[pre_len - 1]) &&
                                            g_ascii_isupper(pre[pre_len - 2]))
                                                ch_lower = 0;
                                }
                        }

                        chars[ch_lower] += wordfreqs[i].count;
                        chars[ch_upper] += wordfreqs[i].count;
                }

apply_table:
        /* Apply characters table */
        sampleiter_reset();
        while ((sample = sampleiter_next()))
                if (sample->ch >= 32 && sample->ch < 127)
                        sample->ratings[ENGINE_WORDFREQ] = chars[sample->ch];
}

#endif /* DISABLE_WORDFREQ */
