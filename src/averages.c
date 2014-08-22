
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

/*
        Average distance engine
*/

/* Maximum measures */
#define MEASURE_DIST  (MAX_DIST)
#define MEASURE_ANGLE (ANGLE_PI / 4)

int num_disqualified;

float measure_distance(const Stroke *a, int i, const Stroke *b, int j,
                       const Vec2 *offset)
/* Measure the offset Euclidean distance between two points */
{
        Vec2 v;

        vec2_set(&v, a->points[i].x + offset->x - b->points[j].x,
                 a->points[i].y + offset->y - b->points[j].y);
        return vec2_square(&v);
}

static float measure_angle(const Stroke *a, int i, const Stroke *b, int j)
/* Measure the lesser angular difference between two segments */
{
        float diff;

        diff = (ANGLE)(a->points[i].angle - b->points[j].angle);
        return diff >= 0 ? diff : -diff;
}

float measure_strokes(Stroke *a, Stroke *b, MeasureFunc func,
                      void *extra, int points, int elasticity)
/* Find optimal match between A points and B points for lowest distance via
   dynamic programming */
{
        int i, j, j_to;
        float table[(points + 1) * (points + 1) + 1];

        /* Coordinates are counted from 1 because of buffer areas */
        points++;

        /* Fill out the buffer row */
        j_to = elasticity + 2;
        if (points < j_to)
                j_to = points;
        for (j = 1; j < j_to; j++)
                table[j] = G_MAXFLOAT;

        /* The first table entry is given */
        table[points + 1] = 2 * func(a, 0, b, 0, extra);

        for (i = 1; i < points; i++) {
                float value;

                /* Starting position */
                j = i - elasticity;
                if (j < 1)
                        j = 1;

                /* Buffer column entry */
                table[i * points + j - 1] = G_MAXFLOAT;

                /* Start from the 2nd cell on the first row */
                j += i == 1;

                /* End limit */
                j_to = i + elasticity + 1;
                if (j_to > points)
                        j_to = points;

                /* Start with up-left */
                value = table[(i - 1) * points + j - 1];

                /* Dynamically program the row segment */
                for (; j < j_to; j++) {
                        float low_value, measure;

                        measure = func(a, i - 1, b, j - 1, extra);
                        low_value = value + measure * 2;

                        /* Check if left is lower */
                        value = table[i * points + j - 1] + measure;
                        if (value <= low_value)
                                low_value = value;

                        /* Check if up is lower */
                        value = table[(i - 1) * points + j];
                        if (value + measure <= low_value)
                                low_value = value + measure;

                        table[i * points + j] = low_value;
                }

                /* End of the row buffer */
                table[i * points + j_to] = G_MAXFLOAT;
        }

        /* Return final lowest progression */
        return table[points * points - 1] / ((points - 1) * 2);
}

static void stroke_average(Stroke *a, Stroke *b, float *pdist, float *pangle,
                           Vec2 *ac_to_bc)
/* Compute the average measures for A vs B */
{
        Stroke *a_sampled, *b_sampled;

        /* Sample strokes to equal lengths */
        if (a->len < 1 || b->len < 1) {
                g_warning("Attempted to measure zero-length stroke");
                return;
        }
        sample_strokes(a, b, &a_sampled, &b_sampled);

        /* Average the distance between the corresponding points */
        *pdist = 0.f;
        if (engines[ENGINE_AVGDIST].range)
                *pdist = measure_strokes(a_sampled, b_sampled,
                                         (MeasureFunc)measure_distance,
                                         ac_to_bc, a_sampled->len,
                                         FINE_ELASTICITY);

        /* We cannot run angle averages if one of the two strokes has no
           segments */
        *pangle = 0.f;
        if (a->spread < DOT_SPREAD)
                goto cleanup;
        else if (b->spread < DOT_SPREAD) {
                *pangle = ANGLE_PI;
                goto cleanup;
        }

        /* Average the angle differences between the points */
        if (engines[ENGINE_AVGANGLE].range)
                *pangle = measure_strokes(a_sampled, b_sampled,
                                          (MeasureFunc)measure_angle, NULL,
                                          a_sampled->len - 1, FINE_ELASTICITY);

cleanup:
        /* Free stroke data */
        stroke_free(a_sampled);
        stroke_free(b_sampled);
}

static void sample_average(Sample *sample)
/* Take the distance between the input and the sample, enumerating the best
   match assignment between input and sample strokes
   TODO scale the measures by stroke distance */
{
        Vec2 ic_to_sc;
        Sample *smaller;
        float distance, m_dist, m_angle;
        int i;

        /* Ignore disqualified samples */
        if ((i = sample_disqualified(sample))) {
                if (i == 2)
                        num_disqualified++;
                return;
        }

        /* Adjust for the difference between sample centers */
        center_samples(&ic_to_sc, input, sample);

        /* Run the averages */
        smaller = input->len < sample->len ? input : sample;
        for (i = 0, distance = 0.f, m_dist = 0.f, m_angle = 0.f;
             i < smaller->len; i++) {
                Stroke *input_stroke, *sample_stroke;
                float weight, s_dist = MAX_DIST, s_angle = ANGLE_PI;

                /* Transform strokes, mapping the larger sample onto the
                   smaller one */
                if (input->len >= sample->len) {
                        input_stroke = transform_stroke(input,
                                                        &sample->transform, i);
                        sample_stroke = sample->strokes[i];
                } else {
                        input_stroke = input->strokes[i];
                        sample_stroke = transform_stroke(sample,
                                                         &sample->transform, i);
                }

                weight = smaller->strokes[i]->spread < DOT_SPREAD ?
                         DOT_SPREAD : smaller->strokes[i]->distance;
                stroke_average(input_stroke, sample_stroke,
                               &s_dist, &s_angle, &ic_to_sc);
                m_dist += s_dist * weight;
                m_angle += s_angle * weight;
                distance += weight;

                /* Clear the created stroke */
                stroke_free(input->len >= sample->len ?
                            input_stroke : sample_stroke);
        }

        /* Undo square distortion and account for multiple strokes */
        m_dist = sqrtf(m_dist) / distance;
        m_angle /= distance;

        /* Check limits */
        if (m_dist > MAX_DIST)
                m_dist = MAX_DIST;
        if (m_angle > ANGLE_PI)
                m_angle = ANGLE_PI;

        /* Assign the ratings */
        sample->ratings[ENGINE_AVGDIST] = RATING_MAX -
                                          RATING_MAX * m_dist / MEASURE_DIST;
        sample->ratings[ENGINE_AVGANGLE] = RATING_MAX -
                                           RATING_MAX * m_angle / MEASURE_ANGLE;
}

void engine_average(void)
/* Computes average distance and angle differences */
{
        Sample *sample;
        int i;

        num_disqualified = 0;
        if (!engines[ENGINE_AVGDIST].range &&
            !engines[ENGINE_AVGANGLE].range)
                return;

        /* Average angle engine needs to be discounted when the input
           contains segments too short to produce meaningful angles */
        engines[ENGINE_AVGANGLE].scale = 0;
        for (i = 0; i < input->len; i++)
                if (input->strokes[i]->spread >= DOT_SPREAD)
                        engines[ENGINE_AVGANGLE].scale++;
        engines[ENGINE_AVGANGLE].scale = engines[ENGINE_AVGANGLE].scale *
                                         ENGINE_SCALE / input->len;

        /* Run the averaging engine on every sample */
        sampleiter_reset();
        while ((sample = sampleiter_next()))
                if (sample->ch)
                        sample_average(sample);
}
