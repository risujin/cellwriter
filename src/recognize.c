
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
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gtk/gtk.h>
#include "common.h"
#include "recognize.h"

/* preprocess.c */
int prep_examined;

void engine_prep(void);

/*
        Engines
*/

Engine engines[] = {

        /* Preprocessor engine must run first */
        { "Key-point distance", engine_prep, MAX_RANGE, TRUE, -1, 0, 0 },

        /* Averaging engines */
        { "Average distance", engine_average, MAX_RANGE, TRUE, -1, 0, 0 },
        { "Average angle", NULL, MAX_RANGE, TRUE, 0, 0, 0 },

#ifndef DISABLE_WORDFREQ
        /* Word frequency engine */
        { "Word context", engine_wordfreq, MAX_RANGE / 3, FALSE, -1, 0, 0 },
#endif
};

static int engine_rating(const Sample *sample, int j)
/* Get the processed rating for engine j on a sample */
{
        int value;

        if (!engines[j].range || engines[j].max < 1)
                return 0;
        value = ((int)sample->ratings[j] - engines[j].average) *
                engines[j].range / engines[j].max;
        if (engines[j].scale >= 0)
                value = value * engines[j].scale / ENGINE_SCALE;
        return value;
}

/*
        Sample chain wrapper
*/

typedef struct SampleLink {
        Sample sample;
        struct SampleLink *prev, *next;
} SampleLink;

static SampleLink *samplelink_root = NULL, *samplelink_iter = NULL;
static int current = 1;

static Sample *sample_new(void)
/* Allocate a link in the sample linked list */
{
        SampleLink *link;

        link = g_malloc0(sizeof (*link));
        link->next = samplelink_root;
        if (samplelink_root)
                samplelink_root->prev = link;
        samplelink_root = link;
        return &link->sample;
}

void sampleiter_reset(void)
/* Reset the sample linked list iterator */
{
        samplelink_iter = samplelink_root;
}

Sample *sampleiter_next(void)
/* Get the next sample link from the sample linked list iterator */
{
        SampleLink *link;

        if (!samplelink_iter)
                return NULL;
        link = samplelink_iter;
        samplelink_iter = samplelink_iter->next;
        return &link->sample;
}

int samples_loaded(void)
{
        return samplelink_root != NULL;
}

/*
        Samples
*/

int samples_max = 5, no_latin_alpha = FALSE;

void clear_sample(Sample *sample)
/* Free stroke data associated with a sample and reset its parameters */
{
        int i;

        for (i = 0; i < sample->len; i++) {
                stroke_free(sample->strokes[i]);
                stroke_free(sample->roughs[i]);
        }
        memset(sample, 0, sizeof (*sample));
}

void copy_sample(Sample *dest, const Sample *src)
/* Copy a sample, cloing its strokes, overwriting dest */
{
        int i;

        *dest = *src;
        for (i = 0; i < src->len; i++) {
                dest->strokes[i] = stroke_clone(src->strokes[i], FALSE);
                dest->roughs[i] = stroke_clone(src->roughs[i], FALSE);
        }
}

static void process_gluable(const Sample *sample, int stroke_num)
/* Calculates the lowest distance between the start or end of one stroke and any
   other point on each other stroke in the sample */
{
        Point point;
        Stroke *s1;
        int i, start;

        /* Dots cannot be glued */
        s1 = sample->strokes[stroke_num];
        memset(s1->gluable_start, -1, sizeof (s1->gluable_start));
        memset(s1->gluable_end, -1, sizeof (s1->gluable_end));
        if (s1->spread < DOT_SPREAD)
                return;

        start = TRUE;
scan:
        point = start ? s1->points[0] : s1->points[s1->len - 1];
        for (i = 0; i < sample->len; i++) {
                Vec2 v;
                Stroke *s2;
                float dist, min = GLUE_DIST;
                int j;
                char gluable;

                s2 = sample->strokes[i];
                if (i == stroke_num || s2->spread < DOT_SPREAD)
                        continue;

                /* Check the distance to the first point */
                vec2_set(&v, s2->points[0].x - point.x,
                         s2->points[0].y - point.y);
                dist = vec2_mag(&v);
                if (dist < min)
                        min = dist;

                /* Find the lowest distance from the glue point to any other
                   point on the other stroke */
                for (j = 0; j < s2->len - 1; j++) {
        	        Vec2 l, w;
        		double dist, mag, dot;

                        /* Vector l is a unit vector from point j to j + 1 */
        		vec2_set(&l, s2->points[j].x - s2->points[j + 1].x,
        		         s2->points[j].y - s2->points[j + 1].y);
                        mag = vec2_norm(&l, &l);

        		/* Vector w is a vector from point j to our point */
        		vec2_set(&w, s2->points[j].x - point.x,
        		         s2->points[j].y - point.y);

        		/* For points that are not in between a segment,
        		   get the distance from the points themselves,
        		   otherwise get the distance from the segment line */
        		dot = vec2_dot(&l, &w);
        		if (dot < 0. || dot > mag) {
        		        vec2_set(&v, s2->points[j + 1].x - point.x,
        		                 s2->points[j + 1].y - point.y);
        		        dist = vec2_mag(&v);
		        } else {
                		dist = vec2_cross(&w, &l);
                		if (dist < 0)
                		        dist = -dist;
		        }
                        if (dist < min)
                                min = dist;
                }
                gluable = min * GLUABLE_MAX / GLUE_DIST;
                if (start)
                        s1->gluable_start[i] = gluable;
                else
                        s1->gluable_end[i] = gluable;
        }
        if (start) {
                start = FALSE;
                goto scan;
        }
}

void process_sample(Sample *sample)
/* Generate cached properties of a sample */
{
        int i;
        float distance;

        if (sample->processed)
                return;
        sample->processed = TRUE;

        /* Make sure all strokes have been processed first */
        for (i = 0; i < sample->len; i++)
                process_stroke(sample->strokes[i]);

        /* Compute properties for each stroke */
        vec2_set(&sample->center, 0., 0.);
        for (i = 0, distance = 0.; i < sample->len; i++) {
                Vec2 v;
                Stroke *stroke;
                float weight;
                int points;

                stroke = sample->strokes[i];

                /* Add the stroke center to the center vector, weighted by
                   length */
                vec2_copy(&v, &stroke->center);
                weight = stroke->spread < DOT_SPREAD ?
                         DOT_SPREAD : stroke->distance;
                vec2_scale(&v, &v, weight);
                vec2_sum(&sample->center, &sample->center, &v);
                distance += weight;

                /* Get gluing distances */
                process_gluable(sample, i);

                /* Create a rough-sampled version */
                points = stroke->distance / ROUGH_RESOLUTION + 0.5;
                if (points < 4)
                        points = 4;
                sample->roughs[i] = sample_stroke(NULL, stroke, points, points);
        }
        vec2_scale(&sample->center, &sample->center, 1.f / distance);
        sample->distance = distance;
}

void center_samples(Vec2 *ac_to_bc, Sample *a, Sample *b)
/* Adjust for the difference between two sample centers */
{
        vec2_sub(ac_to_bc, &b->center, &a->center);
}

int char_disabled(gunichar ch)
/* Returns TRUE if a character is not renderable or is explicity disabled by
   a setting (not counting disabled Unicode blocks) */
{
        return (no_latin_alpha && ch >= unicode_blocks[0].start &&
                ch <= unicode_blocks[0].end && g_ascii_isalpha(ch)) ||
               !g_unichar_isgraph(ch);
}

int sample_disqualified(const Sample *sample)
/* Check disqualification conditions for a sample during recognition.
   The preprocessor engine must run before any calls to this or
   disqualification will not work. */
{
        if ((!ignore_stroke_num && sample->len != input->len) ||
            !sample->enabled)
                return 1;
        if (sample->disqualified)
                return 2;
        if (char_disabled(sample->ch))
                return 3;
        return 0;
}

int sample_valid(const Sample *sample, int used)
/* Check if this sample has changed since it was last referenced */
{
        if (!sample || !used)
                return FALSE;
        return sample->used == used;
}

static void sample_rating(Sample *sample)
/* Get the composite processed rating on a sample */
{
        int i, rating;

        if (!sample->ch || sample_disqualified(sample) ||
            sample->penalty >= 1.f) {
                sample->rating = RATING_MIN;
                return;
        }
        for (i = 0, rating = 0; i < ENGINES; i++)
                rating += engine_rating(sample, i);
        rating *= 1.f - sample->penalty;
        if (rating > RATING_MAX)
                rating = RATING_MAX;
        if (rating < RATING_MIN)
                rating = RATING_MIN;
        sample->rating = rating;
}

void update_enabled_samples(void)
/* Run through the samples list and enable samples in enabled blocks */
{
        Sample *sample;

        sampleiter_reset();
        while ((sample = sampleiter_next())) {
                UnicodeBlock *block;

                sample->enabled = FALSE;
                if (!sample->ch)
                        continue;
                block = unicode_blocks;
                while (block->name) {
                        if (sample->ch >= block->start &&
                            sample->ch <= block->end) {
                                sample->enabled = block->enabled;
                                break;
                        }
                        block++;
                }
        }
}

void promote_sample(Sample *sample)
/* Update usage counter for a sample */
{
        sample->used = current++;
}

void demote_sample(Sample *sample)
/* Remove the sample from our set if we can */
{
        if (char_trained(sample->ch) > 1)
                clear_sample(sample);
        else
                sample->used = 1;
}

Stroke *transform_stroke(Sample *src, Transform *tfm, int i)
/* Create a new stroke by applying the transformation to the source */
{
        Stroke *stroke;
        int k, j;

        stroke = stroke_new(0);
        for (k = 0, j = 0; k < STROKES_MAX && j < src->len; k++)
                for (j = 0; j < src->len; j++)
                        if (tfm->order[j] - 1 == i && tfm->glue[j] == k) {
                                glue_stroke(&stroke, src->strokes[j],
                                            tfm->reverse[j]);
                                break;
                        }
        process_stroke(stroke);
        return stroke;
}

/*
        Recognition and training
*/

Sample *input = NULL;
int strength_sum = 0;

static GTimer *timer;

void recognize_init(void)
{
#ifndef DISABLE_WORDFREQ
        load_wordfreq();
#endif
        timer = g_timer_new();
}

void recognize_sample(Sample *sample, Sample **alts, int num_alts)
{
        gulong microsec;
        int i, range, strength, msec;

        g_timer_start(timer);
        input = sample;
        process_sample(input);

        /* Clear ratings */
        sampleiter_reset();
        while ((sample = sampleiter_next())) {
                memset(sample->ratings, 0, sizeof (sample->ratings));
                sample->rating = 0;
        }

        /* Run engines */
        for (i = 0, range = 0; i < ENGINES; i++) {
                int rated = 0;

                if (engines[i].func)
                        engines[i].func();

                /* Compute average and maximum value */
                engines[i].max = 0;
                engines[i].average = 0;
                sampleiter_reset();
                while ((sample = sampleiter_next())) {
                        int value = 0;

                        if (!sample->ch)
                                continue;
                        if (sample->ratings[i] > value)
                                value = sample->ratings[i];
                        if (!value && engines[i].ignore_zeros)
                                continue;
                        if (value > engines[i].max)
                                engines[i].max = value;
                        engines[i].average += value;
                        rated++;
                }
                if (!rated)
                        continue;
                engines[i].average /= rated;
                if (engines[i].max > 0)
                        range += engines[i].range;
                if (engines[i].max == engines[i].average) {
                        engines[i].average = 0;
                        continue;
                }
                engines[i].max -= engines[i].average;
        }
        if (!range) {
                g_timer_elapsed(timer, &microsec);
                msec = microsec / 100;
                g_message("Recognized -- No ratings, %dms", msec);
                input->ch = 0;
                return;
        }

        /* Rank the top samples */
        alts[0] = NULL;
        sampleiter_reset();
        while ((sample = sampleiter_next())) {
                int j;

                sample_rating(sample);
                if (sample->rating < 1)
                        continue;

                /* Bubble-sort the new rating in */
                for (j = 0; j < num_alts; j++)
                        if (!alts[j]) {
                                if (j < num_alts - 1)
                                        alts[j + 1] = NULL;
                                break;
                        } else if (alts[j]->ch == sample->ch) {
                                if (alts[j]->rating >= sample->rating)
                                        j = num_alts;
                                break;
                        } else if (alts[j]->rating < sample->rating) {
                                int k;

                                if (j == num_alts - 1)
                                        break;

                                /* See if the character is in the list */
                                for (k = j + 1; k < num_alts - 1 && alts[k] &&
                                     alts[k]->ch != sample->ch; k++);

                                /* Do not swallow zeroes */
                                if (!alts[k] && k < num_alts - 1)
                                        alts[k + 1] = NULL;

                                memmove(alts + j + 1, alts + j,
                                        sizeof (*alts) * (k - j));
                                break;
                        }
                if (j >= num_alts)
                        continue;
                alts[j] = sample;
        }

        /* Normalize the alternates' accuracies to 100 */
        if (range)
                for (i = 0; i < num_alts && alts[i]; i++)
                        alts[i]->rating = alts[i]->rating * 100 / range;

        /* Keep track of strength stat */
        strength = 0;
        if (alts[0]) {
                strength = alts[1] ? alts[0]->rating - alts[1]->rating :
                                        100;
                strength_sum += strength;
        }

        g_timer_elapsed(timer, &microsec);
        msec = microsec / 100;
        g_message("Recognized -- %d/%d (%d%%) disqualified, "
                  "%dms (%dms/symbol), %d%% strong",
                  num_disqualified, prep_examined,
                  num_disqualified * 100 / prep_examined, msec,
                  prep_examined - num_disqualified ?
                  msec / (prep_examined - num_disqualified) : -1,
                  strength);

        /*  Print out the top candidate scores in detail */
        if (log_level >= G_LOG_LEVEL_DEBUG)
                for (i = 0; i < num_alts && alts[i]; i++) {
                        int j, len;

                        len = input->len >= alts[i]->len ? input->len :
                                                           alts[i]->len;
                        log_print("| '%C' (", alts[i]->ch);
                        for (j = 0; j < ENGINES; j++)
                                log_print("%4d [%5d]%s",
                                        engine_rating(alts[i], j),
                                        alts[i]->ratings[j],
                                        j < ENGINES - 1 ? "," : "");
                        log_print(") %3d%% [", alts[i]->rating);
                        for (j = 0; j < len; j++)
                                log_print("%d",
                                          alts[i]->transform.order[j] - 1);
                        for (j = 0; j < len; j++)
                                log_print("%c", alts[i]->transform.reverse[j] ?
                                                'R' : '-');
                        for (j = 0; j < len; j++)
                                log_print("%d", alts[i]->transform.glue[j]);
                        log_print("]\n");
                }

        /* Select the top result */
        input->ch = alts[0] ? alts[0]->ch : 0;
}

static void insert_sample(const Sample *new_sample, int force_overwrite)
/* Insert a sample into the sample chain, possibly overwriting an older
   sample */
{
        int last_used, count = 0;
        Sample *sample, *overwrite = NULL, *create = NULL;

        last_used = force_overwrite ? current + 1 : new_sample->used;
        sampleiter_reset();
        while ((sample = sampleiter_next())) {
                if (!sample->used) {
                        create = sample;
                        continue;
                }
                if (sample->ch != new_sample->ch)
                        continue;
                if (sample->used < last_used) {
                        overwrite = sample;
                        last_used = sample->used;
                }
                count++;
        }
        if (overwrite && count >= samples_max) {
                sample = overwrite;
                clear_sample(sample);
        } else if (create)
                sample = create;
        else
                sample = sample_new();
        *sample = *new_sample;
        process_sample(sample);
}

void train_sample(const Sample *sample, int trusted)
/* Overwrite a blank or least-recently-used slot in the samples set */
{
        Sample new_sample;

        /* Do not allow zero-length samples */
        if (sample->len < 1) {
                g_warning("Attempted to train zero length sample for '%C'",
                          sample->ch);
                return;
        }

        copy_sample(&new_sample, sample);
        new_sample.used = trusted ? current++ : 1;
        new_sample.enabled = TRUE;
        insert_sample(&new_sample, TRUE);
}

int char_trained(gunichar ch)
/* Count the number of samples for this character */
{
        Sample *sample;
        int count = 0;

        sampleiter_reset();
        while ((sample = sampleiter_next())) {
                if (sample->ch != ch)
                        continue;
                count++;
        }
        return count;
}

void untrain_char(gunichar ch)
/* Delete all samples for a character */
{
        Sample *sample;

        sampleiter_reset();
        while ((sample = sampleiter_next()))
                if (sample->ch == ch)
                        clear_sample(sample);
}

/*
        Profile
*/

void recognize_sync(void)
/* Sync params with the profile */
{
        int i;

        profile_write("recognize");
        profile_sync_int(&current);
        profile_sync_int(&samples_max);
        if (samples_max < 1)
                samples_max = 1;
        profile_sync_int(&no_latin_alpha);
        for (i = 0; i < ENGINES; i++)
                profile_sync_int(&engines[i].range);
        profile_write("\n");
}

void sample_read(void)
/* Read a sample from the profile */
{
        Sample sample;
        Stroke *stroke;

        memset(&sample, 0, sizeof (sample));
        sample.ch = atoi(profile_read());
        if (!sample.ch) {
                g_warning("Sample on line %d has NULL symbol", profile_line);
                return;
        }
        sample.used = atoi(profile_read());
        stroke = sample.strokes[0];
        for (;;) {
                const char *str;
                int x, y;

                str = profile_read();
                if (!str[0]) {
                        if (!sample.strokes[0]) {
                                g_warning("Sample on line %d ('%C') with no "
                                          "point data", profile_line,
                                          sample.ch);
                                break;
                        }
                        insert_sample(&sample, FALSE);
                        break;
                }
                if (str[0] == ';') {
                        stroke = sample.strokes[sample.len];
                        continue;
                }
                if (sample.len >= STROKES_MAX) {
                        g_warning("Sample on line %d ('%C') is oversize",
                                  profile_line, sample.ch);
                        clear_sample(&sample);
                        break;
                }
                if (!stroke) {
                        stroke = stroke_new(0);
                        sample.strokes[sample.len++] = stroke;
                }
                if (stroke->len >= POINTS_MAX) {
                        g_warning("Symbol '%C' stroke %d is oversize",
                                  sample.ch, sample.len);
                        clear_sample(&sample);
                        break;
                }
                x = atoi(str);
                y = atoi(profile_read());
                draw_stroke(&stroke, x, y);
        }
}

static void sample_write(Sample *sample)
/* Write a sample link to the profile */
{
        int k, l;

        profile_write(va("sample %5d %5d", sample->ch, sample->used));
        for (k = 0; k < sample->len; k++) {
                for (l = 0; l < sample->strokes[k]->len; l++)
                        profile_write(va(" %4d %4d",
                                         sample->strokes[k]->points[l].x,
                                         sample->strokes[k]->points[l].y));
                profile_write("    ;");
        }
        profile_write("\n");
}

void samples_write(void)
/* Write all of the samples to the profile */
{
        Sample *sample;

        sampleiter_reset();
        while ((sample = sampleiter_next()))
                if (sample->ch && sample->used)
                        sample_write(sample);
}
