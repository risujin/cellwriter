
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
#include <string.h>

/*
        Preprocessing engine
*/

/* Maximum and variable versions of the number of samples to prepare for
   thorough examination */
#define PREP_MAX     (SAMPLES_MAX * 4)
#define PREP_SAMPLES (samples_max * 4)

/* Greedy mapping */
#define VALUE_MAX 2048.f
#define VALUE_MIN 1024.f

/* Penalties (proportion of final score deducted) */
#define VERTICAL_PENALTY 16.00f
#define GLUABLE_PENALTY   0.08f
#define GLUE_PENALTY      0.02f

int ignore_stroke_dir = TRUE, ignore_stroke_num = TRUE, prep_examined;

static float measure_partial(Stroke *as, Stroke *b, Vec2 *offset, float scale_b)
{
        Stroke *bs;
        float value;
        int b_len, min_len;

        b_len = b->distance * scale_b / ROUGH_RESOLUTION + 0.5;
        if (b_len < 4)
                b_len = 4;
        min_len = as->len >= b_len ? b_len : as->len;
        bs = sample_stroke(NULL, b, b_len, min_len);
        value = measure_strokes(as, bs, (MeasureFunc)measure_distance, offset,
                                min_len, ROUGH_ELASTICITY);
        stroke_free(bs);
        return value;
}

static float greedy_map(Sample *larger, Sample *smaller, Transform *ptfm,
                        Vec2 *offset)
{
        Transform tfm;
        int i, unmapped_len;
        float total;

        unmapped_len = larger->len;

        /* Prepare transform structure */
        memset(&tfm, 0, sizeof (tfm));
        *ptfm = tfm;
        tfm.valid = TRUE;

        for (i = 0, total = 0.f; i < smaller->len; i++) {
                float best, best_reach = G_MAXFLOAT, best_value = G_MAXFLOAT,
                      value, penalty = G_MAXFLOAT, seg_dist = 0.f;
                int j, last_j = 0, best_j = 0, glue = 0;

        glue_more:
                for (j = 0, best = G_MAXFLOAT; j < larger->len; j++) {
                        Stroke *stroke;
                        float reach, scale;
                        unsigned char gluable;

                        if (tfm.order[j])
                                continue;
                        tfm.reverse[j] = FALSE;

                        /* Do not glue on oversize segments */
                        if (seg_dist +
                            larger->strokes[j]->distance / 2 >
                            smaller->strokes[i]->distance &&
                            (larger->strokes[j]->spread > DOT_SPREAD ||
                             smaller->strokes[i]->spread > DOT_SPREAD))
                                continue;

                        tfm.order[j] = i + 1;
                        tfm.glue[j] = glue;

                measure:
                        reach = 0.f;
                        gluable = 0;
                        if (glue) {
                                Vec2 v;
                                Point *p1, *p2;
                                unsigned char gluable2;

                                /* Can we glue these strokes together? */
                                if (!tfm.reverse[j]) {
                                        gluable = larger->strokes[j]->
                                                  gluable_start[last_j];
                                        gluable2 = larger->strokes[last_j]->
                                                   gluable_end[j];
                                        if (gluable2 < gluable)
                                                gluable = gluable2;
                                        if (gluable >= GLUABLE_MAX) {
                                                if (!ignore_stroke_dir)
                                                        continue;
                                                tfm.reverse[j] = TRUE;
                                        }
                                }
                                if (tfm.reverse[j]) {
                                        gluable = larger->strokes[j]->
                                                  gluable_end[last_j];
                                        gluable2 = larger->strokes[last_j]->
                                                   gluable_start[j];
                                        if (gluable2 < gluable)
                                                gluable = gluable2;
                                        if (gluable >= GLUABLE_MAX)
                                                continue;
                                }

                                /* Get the inter-stroke (reach) distance */
                                p1 = larger->strokes[last_j]->points +
                                     (tfm.reverse[last_j] ? 0 :
                                      larger->strokes[last_j]->len - 1);
                                p2 = larger->strokes[j]->points +
                                     (!tfm.reverse[j] ? 0 :
                                      larger->strokes[j]->len - 1);
                                vec2_set(&v, p2->x - p1->x,
                                         p2->y - p1->y);
                                reach = vec2_mag(&v);
                        }

                        /* Transform and measure the distance */
                        stroke = transform_stroke(larger, &tfm, i);
                        scale = smaller->distance /
                                (reach + ptfm->reach + larger->distance);
                        value = measure_partial(smaller->roughs[i], stroke,
                                                offset, scale);
                        stroke_free(stroke);

                        /* Keep track of the best result */
                        if (value < best && value < VALUE_MAX) {
                                best = value;
                                best_j = j;
                                best_reach = reach;
                                *ptfm = tfm;

                                /* Penalize glue and reach distance */
                                penalty = glue * GLUE_PENALTY +
                                          gluable * GLUABLE_PENALTY /
                                                    GLUABLE_MAX;
                        }

                        /* Bail if we have a really good match */
                        if (value < VALUE_MIN)
                                break;

                        /* Glue on with reversed direction */
                        if (ignore_stroke_dir && !tfm.reverse[j] &&
                            larger->strokes[j]->spread > DOT_SPREAD) {
                                tfm.reverse[j] = TRUE;
                                goto measure;
                        }

                        tfm.reverse[j] = FALSE;
                        tfm.order[j] = 0;
                }
                if (best < G_MAXFLOAT) {
                        best_value = best;
                        larger->penalty += penalty;
                        smaller->penalty += penalty;
                        seg_dist += best_reach +
                                    larger->strokes[best_j]->distance;
                        ptfm->reach += best_reach;
                        tfm = *ptfm;

                        /* If we still have strokes and we didn't just add on
                           a dot, try gluing them on */
                        unmapped_len--;
                        if (unmapped_len >= smaller->len - i &&
                            larger->strokes[best_j]->spread >
                            DOT_SPREAD) {
                                last_j = best_j;
                                glue++;
                                goto glue_more;
                        }
                }

                /* Didn't map a target stroke? */
                else if (!glue) {
                        ptfm->valid = FALSE;
                        return G_MAXFLOAT;
                }

                total += best_value;
        }

        /* Didn't assign all of the strokes? */
        if (unmapped_len) {
                ptfm->valid = FALSE;
                return G_MAXFLOAT;
        }

        return total / smaller->len;
}

static int prep_sample(Sample *sample)
{
        Vec2 offset;
        float dist;

        /* Structural disqualification */
        if (!sample->used || !sample->enabled ||
            (!ignore_stroke_num && sample->len != input->len))
                return FALSE;

        prep_examined++;
        sample->penalty = 0.f;

        /* Account for displacement */
        center_samples(&offset, sample, input);

        /* Compare each input stroke to every stroke in the sample and
           generate the stroke order information which will be used by other
           engines */
        if (input->len >= sample->len)
                dist = greedy_map(input, sample, &sample->transform, &offset);
        else {
                vec2_set(&offset, -offset.x, -offset.y);
                dist = greedy_map(sample, input, &sample->transform, &offset);
        }
        if (!sample->transform.valid)
                return FALSE;

        /* Undo square distortion */
        dist = sqrtf(dist);
        if (dist > MAX_DIST)
                return FALSE;

        /* Penalize vertical displacement */
        sample->penalty += VERTICAL_PENALTY *
                           offset.y * offset.y / SCALE / SCALE;

        sample->ratings[ENGINE_PREP] = RATING_MAX -
                                       RATING_MAX * dist / MAX_DIST;
        return TRUE;
}

void engine_prep(void)
{
        Sample *sample, *list[PREP_MAX];
        int i;

        /* Rate every sample in every possible configuration */
        list[0] = NULL;
        prep_examined = 0;
        sampleiter_reset();
        while ((sample = sampleiter_next())) {
                sample->disqualified = TRUE;
                if (!sample->used || !sample->ch || !prep_sample(sample))
                        continue;

                /* Bubble-sort sample into the list */
                for (i = 0; i < PREP_SAMPLES; i++)
                        if (!list[i]) {
                                list[i] = sample;
                                if (i < PREP_MAX - 1)
                                        list[i + 1] = NULL;
                                break;
                        } else if (list[i]->ratings[ENGINE_PREP] <
                                   sample->ratings[ENGINE_PREP]) {
                                memmove(list + i + 1, list + i,
                                        (PREP_MAX - i - 1) * sizeof (*list));
                                list[i] = sample;
                                break;
                        }
        }

        /* Qualify the best samples */
        for (i = 0; i < PREP_SAMPLES && list[i]; i++)
                list[i]->disqualified = FALSE;
}
