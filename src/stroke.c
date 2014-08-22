
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
#include <string.h>
#include <math.h>
#include <gtk/gtk.h>
#include "common.h"
#include "recognize.h"

/*
        Stroke functions
*/

/* Distance from the line formed by the two neighbors of a point, which, if
   not exceeded, will cause the point to be culled during simplification */
#define SIMPLIFY_THRESHOLD 0.5

/* Granularity of stroke point array in points */
#define POINTS_GRAN 64

/* Size of a stroke structure */
#define STROKE_SIZE(size) (sizeof (Stroke) + (size) * sizeof (Point))

void process_stroke(Stroke *stroke)
/* Generate cached parameters of a stroke */
{
        int i;
        float distance;

        if (stroke->processed)
                return;
        stroke->processed = TRUE;

        /* Dot strokes */
        if (stroke->len == 1) {
                vec2_set(&stroke->center, stroke->points[0].x,
                         stroke->points[0].y);
                stroke->spread = 0.f;
                return;
        }

        stroke->min_x = stroke->max_x = stroke->points[0].x;
        stroke->min_y = stroke->max_y = stroke->points[0].y;
        for (i = 0, distance = 0.; i < stroke->len - 1; i++) {
                Vec2 v;
                float weight;

                /* Angle */
                vec2_set(&v, stroke->points[i + 1].x - stroke->points[i].x,
                         stroke->points[i + 1].y - stroke->points[i].y);
                stroke->points[i].angle = vec2_angle(&v);

                /* Point contribution to spread */
                if (stroke->points[i + 1].x < stroke->min_x)
                        stroke->min_x = stroke->points[i + 1].x;
                if (stroke->points[i + 1].y < stroke->min_y)
                        stroke->min_y = stroke->points[i + 1].y;
                if (stroke->points[i + 1].x > stroke->max_x)
                        stroke->max_x = stroke->points[i + 1].x;
                if (stroke->points[i + 1].y > stroke->max_y)
                        stroke->max_y = stroke->points[i + 1].y;

                /* Segment contribution to center */
                vec2_set(&v, stroke->points[i + 1].x - stroke->points[i].x,
                         stroke->points[i + 1].y - stroke->points[i].y);
                distance += weight = vec2_mag(&v);
                vec2_set(&v, stroke->points[i + 1].x + stroke->points[i].x,
                         stroke->points[i + 1].y + stroke->points[i].y);
                vec2_scale(&v, &v, weight / 2.);
                vec2_sum(&stroke->center, &stroke->center, &v);
        }
        vec2_scale(&stroke->center, &stroke->center, 1. / distance);
        stroke->points[i].angle = stroke->points[i - 1].angle;
        stroke->distance = distance;

        /* Stroke spread */
        stroke->spread = stroke->max_x - stroke->min_x;
        if (stroke->max_y - stroke->min_y > stroke->spread)
                stroke->spread = stroke->max_y - stroke->min_y;
}

void clear_stroke(Stroke *stroke)
/* Clear cached parameters */
{
        int size;

        size = stroke->size;
        memset(stroke, 0, sizeof (*stroke));
        stroke->size = size;
}

Stroke *stroke_new(int size)
/* Allocate memory for a new stroke */
{
        Stroke *stroke;

        if (size < POINTS_GRAN)
                size = POINTS_GRAN;
        stroke = g_malloc(STROKE_SIZE(size));
        stroke->size = size;
        clear_stroke(stroke);
        return stroke;
}

static void reverse_copy_points(Point *dest, const Point *src, int len)
{
        int i;

        for (i = 0; i < len; i++) {
                ANGLE angle = 0;

                if (i < len - 1)
                        angle = src[len - i - 2].angle + ANGLE_PI;
                dest[i] = src[len - i - 1];
                dest[i].angle = angle;
        }
}

Stroke *stroke_clone(const Stroke *src, int reverse)
{
        Stroke *stroke;

        if (!src)
                return NULL;
        stroke = stroke_new(src->size);
        if (!reverse)
                memcpy(stroke, src, STROKE_SIZE(src->size));
        else {
                memcpy(stroke, src, sizeof (Stroke));
                reverse_copy_points(stroke->points, src->points, src->len);
        }
        return stroke;
}

void stroke_free(Stroke *stroke)
{
        g_free(stroke);
}

void glue_stroke(Stroke **pa, const Stroke *b, int reverse)
/* Glue B onto the end of A preserving processed properties */
{
        Vec2 glue_seg, glue_center, b_center;
        Point start;
        Stroke *a;
        float glue_mag;

        a = *pa;

        /* If there is no stroke to glue to, just copy */
        if (!a || a->len < 1) {
                if (a->len < 1)
                        stroke_free(a);
                *pa = stroke_clone(b, reverse);
                return;
        }

        /* Allocate memory */
        if (a->size < a->len + b->len) {
                a->size = a->len + b->len;
                a = g_realloc(a, STROKE_SIZE(a->size));
        }

        /* Gluing two strokes creates a new segment between them */
        start = reverse ? b->points[b->len - 1] : b->points[0];
        vec2_set(&glue_seg, start.x - a->points[a->len - 1].x,
                 start.y - a->points[a->len - 1].y);
        vec2_set(&glue_center, (start.x + a->points[a->len - 1].x) / 2,
                 (start.y + a->points[a->len - 1].y) / 2);
        glue_mag = vec2_mag(&glue_seg);

        /* Compute new spread */
        if (b->min_x < a->min_x)
                a->min_x = b->min_x;
        if (b->max_x > a->max_x)
                a->max_x = b->max_x;
        if (b->min_y < a->min_y)
                a->min_y = b->min_y;
        if (b->max_y > a->max_y)
                a->max_y = b->max_y;
        a->spread = a->max_x - a->min_x;
        if (a->max_y - a->min_y > a->spread)
                a->spread = a->max_y - a->min_y;

        /* Compute new center point */
        vec2_scale(&a->center, &a->center, a->distance);
        vec2_scale(&b_center, &b->center, b->distance);
        vec2_scale(&glue_center, &glue_center, glue_mag);
        vec2_set(&a->center, a->center.x + b_center.x + glue_center.x,
                 a->center.y + b_center.y + glue_center.y);
        vec2_scale(&a->center, &a->center,
                   1.f / (a->distance + b->distance + glue_mag));

        /* Copy points */
        if (!reverse || b->len < 2)
                memcpy(a->points + a->len, b->points, b->len * sizeof (Point));
        else
                reverse_copy_points(a->points + a->len, b->points, b->len);

        a->points[a->len - 1].angle = vec2_angle(&glue_seg);
        a->distance += glue_mag + b->distance;
        a->len += b->len;
        *pa = a;
}

void draw_stroke(Stroke **ps, int x, int y)
/* Add a point in scaled coordinates to a stroke */
{
        /* Create a new stroke if necessary */
        if (!(*ps))
                *ps = stroke_new(0);

        /* If we run out of room, resample the stroke to fit */
        if ((*ps)->len >= POINTS_MAX) {
                Stroke *new_stroke;

                new_stroke = sample_stroke(NULL, *ps, POINTS_MAX - POINTS_GRAN,
                                           POINTS_MAX);
                stroke_free(*ps);
                *ps = new_stroke;
        }

        /* Range limits */
        if (x <= -SCALE / 2)
                x = -SCALE / 2 + 1;
        if (x >= SCALE / 2)
                x = SCALE / 2 - 1;
        if (y <= -SCALE / 2)
                y = -SCALE / 2 + 1;
        if (y >= SCALE / 2)
                y = SCALE / 2 - 1;

        /* Do we need more memory? */
        if ((*ps)->len >= (*ps)->size) {
                (*ps)->size += POINTS_GRAN;
                *ps = g_realloc(*ps, STROKE_SIZE((*ps)->size));
        }

        (*ps)->points[(*ps)->len].x = x;
        (*ps)->points[(*ps)->len++].y = y;
}

void smooth_stroke(Stroke *s)
/* Smooth stroke points by moving each point halfway toward the line between
   its two neighbors */
{
        int i, last_x, last_y;

        last_x = s->points[0].x;
        last_y = s->points[0].y;
        for (i = 1; i < s->len - 1; i++) {
                Vec2 a, b, c, m, ab, ac, am;

                if (last_x == s->points[i + 1].x &&
                    last_y == s->points[i + 1].y) {
                        last_x = s->points[i].x;
                        last_y = s->points[i].y;
                        continue;
                }
                vec2_set(&a, last_x, last_y);
                vec2_set(&b, s->points[i].x, s->points[i].y);
                vec2_set(&c, s->points[i + 1].x, s->points[i + 1].y);
                vec2_sub(&ac, &c, &a);
                vec2_sub(&ab, &b, &a);
                vec2_proj(&am, &ab, &ac);
                vec2_sum(&m, &a, &am);
                vec2_avg(&b, &b, &m, 0.5);
                last_x = s->points[i].x;
                last_y = s->points[i].y;
                s->points[i].x = b.x + 0.5;
                s->points[i].y = b.y + 0.5;
        }
}

void simplify_stroke(Stroke *s)
/* Remove excess points between neighbors */
{
	int i;

	for (i = 1; i < s->len - 1; i++) {
	        Vec2 l, w;
		double dist, mag, dot;

                /* Vector l is a unit vector from point i - 1 to point i + 1 */
		vec2_set(&l, s->points[i - 1].x - s->points[i + 1].x,
		         s->points[i - 1].y - s->points[i + 1].y);
		mag = vec2_norm(&l, &l);

		/* Vector w is a vector from point i - 1 to point i */
		vec2_set(&w, s->points[i - 1].x - s->points[i].x,
		         s->points[i - 1].y - s->points[i].y);

		/* Do not touch mid points that are not in between their
		   neighbors */
		dot = vec2_dot(&l, &w);
		if (dot < 0. || dot > mag)
		        continue;

		/* Remove any points that are less than some threshold away
		   from their neighbor points */
		dist = vec2_cross(&w, &l);
		if (dist < SIMPLIFY_THRESHOLD && dist > -SIMPLIFY_THRESHOLD) {
			memmove(s->points + i, s->points + i + 1,
			        (--s->len - i) * sizeof (*s->points));
			i--;
		}
	}
}

void dump_stroke(Stroke *stroke)
{
        int i;

        /* Print stats */
        g_message("Stroke data --");
        g_debug("Distance: %g", stroke->distance);
        g_debug("  Center: (%g, %g)", stroke->center.x, stroke->center.y);
        g_debug("  Spread: %d", stroke->spread);
        g_message("%d points --", stroke->len);

        /* Print point data */
        for (i = 0; i < stroke->len; i++)
                g_debug("%3d: (%4d,%4d)\n",
                        i, stroke->points[i].x, stroke->points[i].y);
}

Stroke *sample_stroke(Stroke *out, Stroke *in, int points, int size)
/* Recreate the stroke by sampling at regular distance intervals.
   Sampled strokes always have angle data. */
{
        Vec2 v;
        double dist_i, dist_j, dist_per;
        int i, j, len;

        if (!in || in->len < 1) {
                g_warning("Attempted to sample an invalid stroke");
                return NULL;
        }

        /* Check ranges */
        if (size >= POINTS_MAX) {
                g_warning("Stroke sized to maximum length possible");
                size = POINTS_MAX;
        }
        if (points >= POINTS_MAX) {
                g_warning("Stroke sampled to maximum length possible");
                points = POINTS_MAX;
        }
        if (size < 1)
                size = 1;
        if (points < 1)
                points = 1;

        /* Allocate memory and copy cached data */
        if (!out)
                out = g_malloc(STROKE_SIZE(size));
        out->size = size;
        len = out->size < points ? out->size - 1 : points - 1;
        out->len = len + 1;
        out->spread = in->spread;
        out->center = in->center;

        /* Special case for sampling a single point */
        if (in->len <= 1 || points <= 1) {
                for (i = 0; i < len + 1; i++)
                        out->points[i] = in->points[0];
                out->distance = 0.;
                return out;
        }

        dist_per = in->distance / (points - 1);
        out->distance = in->distance;
        vec2_set(&v, in->points[1].x - in->points[0].x,
                 in->points[1].y - in->points[0].y);
        dist_j = vec2_mag(&v);
        dist_i = dist_per;
        out->points[0] = in->points[0];
        for (i = 1, j = 0; i < len; i++) {

                /* Advance our position */
                while (dist_i >= dist_j) {
                        if (j >= in->len - 2)
                                goto finish;
                        dist_i -= dist_j;
                        j++;
                        vec2_set(&v, in->points[j + 1].x - in->points[j].x,
                                 in->points[j + 1].y - in->points[j].y);
                        dist_j = vec2_mag(&v);
                }

                /* Interpolate points */
                out->points[i].x = in->points[j].x +
                                   (in->points[j + 1].x - in->points[j].x) *
                                   dist_i / dist_j;
                out->points[i].y = in->points[j].y +
                                   (in->points[j + 1].y - in->points[j].y) *
                                   dist_i / dist_j;
                out->points[i].angle = in->points[j].angle;

                dist_i += dist_per;
        }
finish:
        for (; i < len + 1; i++)
                out->points[i] = in->points[j + 1];

        return out;
}

void sample_strokes(Stroke *a, Stroke *b, Stroke **as, Stroke **bs)
/* Sample multiple strokes to equal lengths */
{
        double dist;
        int points;

        /* Find the sample length */
        dist = a->distance;
        if (b->distance > dist)
                dist = b->distance;
        points = 1 + dist / FINE_RESOLUTION;
        if (points > POINTS_MAX)
                points = POINTS_MAX;

        *as = sample_stroke(NULL, a, points, points);
        *bs = sample_stroke(NULL, b, points, points);
}
