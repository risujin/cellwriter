
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

/*
        Stroke data
*/

/* Maximum number of points a stroke can have */
#define POINTS_MAX 256

/* Scale of the point coordinates */
#define SCALE    256
#define MAX_DIST 362 /* sqrt(2) * SCALE */

/* Maximum number of strokes a sample can have */
#define STROKES_MAX 32

/* Largest value the gluable matrix entries can take */
#define GLUABLE_MAX 255

typedef struct {
        signed char x, y;
        ANGLE angle;
} Point;

typedef struct {
        Vec2 center;
        float distance;
        int len, size, spread;
        unsigned char processed,
                      gluable_start[STROKES_MAX], gluable_end[STROKES_MAX];
        signed char min_x, max_x, min_y, max_y;
        Point points[];
} Stroke;

/* Stroke allocation */
Stroke *stroke_new(int size);
Stroke *stroke_clone(const Stroke *src, int reverse);
void stroke_free(Stroke *stroke);
void clear_stroke(Stroke *stroke);

/* Stroke manipulation */
void process_stroke(Stroke *stroke);
void draw_stroke(Stroke **stroke, int x, int y);
void smooth_stroke(Stroke *s);
void simplify_stroke(Stroke *s);
Stroke *sample_stroke(Stroke *out, Stroke *in, int points, int size);
void sample_strokes(Stroke *a, Stroke *b, Stroke **as, Stroke **bs);
void glue_stroke(Stroke **a, const Stroke *b, int reverse);
void dump_stroke(Stroke *stroke);

/*
        Recognition engines
*/

/* This will prevent the word frequency table from loading */
/* #define DISABLE_WORDFREQ */

/* Largest allowed engine weight */
#define MAX_RANGE 100

/* Range of the scale value for engines */
#define ENGINE_SCALE STROKES_MAX

/* Minimum stroke spread distance for angle measurements */
#define DOT_SPREAD (SCALE / 10)

/* Maximum distance between glue points */
#define GLUE_DIST (SCALE / 6)

enum {
        ENGINE_PREP,
        ENGINE_AVGDIST,
        ENGINE_AVGANGLE,
#ifndef DISABLE_WORDFREQ
        ENGINE_WORDFREQ,
#endif
        ENGINES
};

typedef struct {
        const char *name;
        void (*func)(void);
        int range, ignore_zeros, scale, average, max;
} Engine;

typedef struct Cell Cell;

/* Generalized measure function */
typedef float (*MeasureFunc)(Stroke *a, int i, Stroke *b, int j, void *extra);

extern int ignore_stroke_order, ignore_stroke_dir, ignore_stroke_num,
           elasticity, no_latin_alpha, wordfreq_enable;
extern Engine engines[ENGINES];

void engine_average(void);
void engine_wordfreq(void);
void load_wordfreq(void);
float measure_distance(const Stroke *a, int i, const Stroke *b, int j,
                       const Vec2 *offset);
float measure_strokes(Stroke *a, Stroke *b, MeasureFunc func,
                      void *extra, int points, int elasticity);

/*
        Samples and characters
*/

/* Highest range a rating can have */
#define RATING_MAX 32767
#define RATING_MIN -32767

/* Maximum number of samples we can have per character */
#define SAMPLES_MAX 16

/* Fine sampling parameters */
#define FINE_RESOLUTION 8.f
#define FINE_ELASTICITY 2

/* Rough sampling parameters */
#define ROUGH_RESOLUTION 24.f
#define ROUGH_ELASTICITY 0

typedef struct {
        unsigned char valid, order[STROKES_MAX], reverse[STROKES_MAX],
                      glue[STROKES_MAX];
        float reach;
} Transform;

typedef struct {
        int used;
        gunichar ch;
        unsigned short len;
        short rating, ratings[ENGINES];
        unsigned char enabled, disqualified, processed;
        Transform transform;
        Vec2 center;
        float distance, penalty;
        Stroke *strokes[STROKES_MAX], *roughs[STROKES_MAX];
} Sample;

extern Sample *input;
extern int num_disqualified, training_block, samples_max;

/* Sample list iteration */
void sampleiter_reset(void);
Sample *sampleiter_next(void);

/* Properties */
void process_sample(Sample *sample);
void center_samples(Vec2 *ac_to_bc, Sample *a, Sample *b);
int sample_disqualified(const Sample *sample);
int sample_valid(const Sample *sample, int used);
int char_trained(gunichar ch);
int char_disabled(gunichar ch);

/* Processing */
void clear_sample(Sample *sample);
void recognize_sample(Sample *cell, Sample **alts, int num_alts);
void train_sample(const Sample *cell, int trusted);
void untrain_char(gunichar ch);
void update_enabled_samples(void);
void promote_sample(Sample *sample);
void demote_sample(Sample *sample);
Stroke *transform_stroke(Sample *src, Transform *tfm, int i);
