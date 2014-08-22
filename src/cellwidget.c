
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
#include "keys.h"
#include <string.h>

/* stroke.c */
void smooth_stroke(Stroke *s);
void simplify_stroke(Stroke *s);

/* cellwidget.c */
int cell_widget_scrollbar_width(void);
static void start_timeout(void);
static void show_context_menu(int button, int time);
static void stop_drawing(void);

/*
        Cells
*/

#define ALTERNATES 5
#define CELL_BASELINE (cell_height / 3)
#define CELL_BORDER (cell_height / 12)
#define KEY_WIDGET_COLS 4
#define KEY_WIDGET_BORDER 6

/* Msec of no mouse motion before a cell is finished */
#define MOTION_TIMEOUT 500

/* Cell flags */
#define CELL_SHOW_INK   0x01
#define CELL_DIRTY      0x02
#define CELL_VERIFIED   0x04
#define CELL_SHIFTED    0x08

struct Cell {
        Sample sample, *alts[ALTERNATES];
        gunichar ch;
        int alt_used[ALTERNATES];
        char flags, alt_ratings[ALTERNATES];
};

/* Cell preferences */
int cell_width = 40, cell_height = 70, cell_cols_pref = 12, cell_rows_pref = 4,
    enable_cairo = TRUE, training = FALSE, train_on_input = TRUE,
    right_to_left = FALSE, keyboard_enabled = TRUE, xinput_enabled = FALSE;

/* Statistics */
int corrections = 0, rewrites = 0, characters = 0, inputs = 0;

/* Colors */
GdkColor custom_active_color = RGB_TO_GDKCOLOR(255, 255, 255),
         custom_inactive_color = RGB_TO_GDKCOLOR(212, 222, 226),
         custom_ink_color = RGB_TO_GDKCOLOR(0, 0, 0),
         custom_select_color = RGB_TO_GDKCOLOR(204, 0, 0);
static GdkColor color_active, color_inactive, color_ink, color_select;

static Cell *cells = NULL, *cells_saved = NULL;
static GtkWidget *drawing_area = NULL, *training_menu, *scrollbar;
static GdkPixmap *pixmap = NULL;
static GdkGC *pixmap_gc = NULL;
static GdkColor color_bg, color_bg_dark;
static cairo_t *cairo = NULL;
static PangoContext *pango = NULL;
static PangoFontDescription *pango_font_desc = NULL;
static KeyWidget *key_widget;
static gunichar *history[HISTORY_MAX];
static int cell_cols, cell_rows, cell_row_view = 0, current_cell = -1, old_cc,
           cell_cols_saved, cell_rows_saved, cell_row_view_saved,
           timeout_source,
           drawing = FALSE, inserting = FALSE, eraser = FALSE, invalid = FALSE,
           potential_insert = FALSE, potential_hold = FALSE, cross_out = FALSE,
           show_keys = TRUE, is_clear = TRUE, keys_dirty = FALSE;
static double cursor_x, cursor_y;

static void cell_coords(int cell, int *px, int *py)
/* Get the int position of a cell from its index */
{
        int cell_y, cell_x;

        cell -= cell_row_view * cell_cols;
        cell_y = cell / cell_cols;
        cell_x = cell - cell_y * cell_cols;
        *px = (!right_to_left ? cell_x * cell_width :
                                (cell_cols - cell_x - 1) * cell_width) + 1;
        *py = cell_y * cell_height + 1;
}

static void set_pen_color(Sample *sample, int cell)
/* Selects the pen color depending on if the sample being drawn is the input
   or the template sample */
{
        if (sample == input || sample == &cells[cell].sample)
                cairo_set_source_gdk_color(cairo, &color_ink, 1.);
        else
                cairo_set_source_gdk_color(cairo, &color_select, 1.);
}

static void render_point(Sample *sample, int cell, int stroke, Vec2 *offset)
/* Draw a single point stroke */
{
        double x, y, radius;
        int cx, cy;

        if (!pixmap || stroke < 0 || !sample || stroke >= sample->len ||
            sample->strokes[stroke]->len < 1)
                return;

        /* Apply offset */
        x = sample->strokes[stroke]->points[0].x;
        y = sample->strokes[stroke]->points[0].y;
        if (offset) {
                x += offset->x;
                y += offset->y;
        }

        /* Unscale coordinates */
        cell_coords(cell, &cx, &cy);
        x = cx + cell_width / 2 + x * cell_height / SCALE;
        y = cy + cell_height / 2 + y * cell_height / SCALE;

        /* Draw a dot with cairo */
        cairo_new_path(cairo);
        radius = cell_height / 33.;
        cairo_arc(cairo, x, y, radius > 1. ? radius : 1., 0., 2 * M_PI);
        set_pen_color(sample, cell);
        cairo_fill(cairo);

        gtk_widget_queue_draw_area(drawing_area, x - radius - 0.5,
                                   y - radius - 0.5, radius * 2 + 0.5,
                                   radius * 2 + 0.5);
}

static void render_segment(Sample *sample, int cell, int stroke, int seg,
                           Vec2 *offset)
/* Draw a segment of the stroke
   FIXME since the segments are not properly connected according to Cairo,
         there is a bit of missing value at the segment connection points */
{
        double pen_width, x1, x2, y1, y2;
        int xmin, xmax, ymin, ymax, cx, cy, pen_range;

        if (!cairo || stroke < 0 || !sample || stroke >= sample->len ||
            seg < 0 || seg >= sample->strokes[stroke]->len - 1)
                return;

        x1 = sample->strokes[stroke]->points[seg].x;
        x2 = sample->strokes[stroke]->points[seg + 1].x;
        y1 = sample->strokes[stroke]->points[seg].y;
        y2 = sample->strokes[stroke]->points[seg + 1].y;

        /* Apply offset */
        if (offset) {
                x1 += offset->x;
                y1 += offset->y;
                x2 += offset->x;
                y2 += offset->y;
        }

        /* Unscale coordinates */
        cell_coords(cell, &cx, &cy);
        x1 = cx + cell_width / 2 + x1 * cell_height / SCALE;
        x2 = cx + cell_width / 2 + x2 * cell_height / SCALE;
        y1 = cy + cell_height / 2 + y1 * cell_height / SCALE;
        y2 = cy + cell_height / 2 + y2 * cell_height / SCALE;

        /* Find minimum and maximum x and y */
        if (x1 > x2) {
                xmax = x1 + 0.9999;
                xmin = x2;
        } else {
                xmin = x1;
                xmax = x2 + 0.9999;
        }
        if (y1 > y2) {
                ymax = y1 + 0.9999;
                ymin = y2;
        } else {
                ymin = y1;
                ymax = y2 + 0.9999;
        }

        /* Draw the new segment using Cairo */
        cairo_new_path(cairo);
        cairo_move_to(cairo, x1, y1);
        cairo_line_to(cairo, x2, y2);
        set_pen_color(sample, cell);
        pen_width = cell_height / 33.;
        if (pen_width < 1.)
                pen_width = 1.;
        cairo_set_line_width(cairo, pen_width);
        cairo_stroke(cairo);

        /* Dirty only the new segment */
        pen_range = 2 * pen_width + 0.9999;
        gtk_widget_queue_draw_area(drawing_area, xmin - pen_range,
                                   ymin - pen_range,
                                   xmax - xmin + pen_range + 1,
                                   ymax - ymin + pen_range + 1);
}

static void render_sample(Sample *sample, int cell)
/* Render the ink from a sample in a cell */
{
        Vec2 sc_to_ic;
        int i, j;

        if (!sample)
                return;

        /* Center stored samples on input */
        if (sample != &cells[cell].sample)
                center_samples(&sc_to_ic, sample, &cells[cell].sample);
        else
                vec2_set(&sc_to_ic, 0., 0.);

        for (i = 0; i < sample->len; i++)
                if (sample->strokes[i]->len <= 1 ||
                    sample->strokes[i]->spread < DOT_SPREAD)
                        render_point(sample, cell, i, &sc_to_ic);
                else
                        for (j = 0; j < sample->strokes[i]->len - 1; j++)
                                render_segment(sample, cell, i, j, &sc_to_ic);
}

static int cell_offscreen(int cell)
{
        int rows, cols;

        cols = cell_cols;
        if (show_keys)
                cols -= KEY_WIDGET_COLS;
        rows = cell_rows < cell_rows_pref ? cell_rows : cell_rows_pref;
        return cell < cell_row_view * cols ||
               cell >= (cell_row_view + rows) * cols;
}

static void dirty_cell(int cell)
{
        if (!cell_offscreen(cell))
                cells[cell].flags |= CELL_DIRTY;
}

static void dirty_all(void)
{
        int i, rows;

        rows = cell_row_view + cell_rows_pref > cell_rows ?
               cell_rows : cell_row_view + cell_rows_pref;
        for (i = cell_cols * cell_row_view; i < rows * cell_cols; i++)
                cells[i].flags |= CELL_DIRTY;
}

static void render_cell(int i)
{
        cairo_pattern_t *pattern;
        GdkColor color, *base_color;
        Cell *pc;
        int x, y, active, cols, samples = 0;

        if (!cairo || !pixmap || !pixmap_gc || cell_offscreen(i))
                return;
        pc = cells + i;
        cell_coords(i, &x, &y);
        if (training) {
                samples = char_trained(pc->ch);
                active = pc->ch && (samples > 0 ||
                                    (current_cell == i && input &&
                                     !invalid && input->len));
        } else
                active = pc->ch || (current_cell == i && !inserting &&
                                    !invalid && input && input->len);
        base_color = active ? &color_active : &color_inactive;

        /* Fill above baseline */
        gdk_gc_set_rgb_fg_color(pixmap_gc, base_color);
        gdk_draw_rectangle(pixmap, pixmap_gc, TRUE, x, y, cell_width,
                                cell_height - CELL_BASELINE);

        /* Fill baseline */
        highlight_gdk_color(base_color, &color, 0.1);
        gdk_gc_set_rgb_fg_color(pixmap_gc, &color);
        gdk_draw_rectangle(pixmap, pixmap_gc, TRUE, x, y + cell_height -
                           CELL_BASELINE, cell_width, CELL_BASELINE);

        /* Cairo clip region */
        cairo_reset_clip(cairo);
        cairo_rectangle(cairo, x, y, cell_width, cell_height);
        cairo_clip(cairo);

        /* Separator line */
        cols = cell_cols;
        if (show_keys)
                cols -= KEY_WIDGET_COLS;
        if ((!right_to_left && i % cell_cols) ||
            (right_to_left && i % cell_cols != cols - 1)) {
                highlight_gdk_color(base_color, &color, 0.5);
                pattern = cairo_pattern_create_linear(x, y, x, y + cell_height);
                cairo_pattern_add_gdk_color_stop(pattern, 0.0, &color, 0.);
                cairo_pattern_add_gdk_color_stop(pattern, 0.5, &color, 1.);
                cairo_pattern_add_gdk_color_stop(pattern, 1.0, &color, 0.);
                cairo_set_source(cairo, pattern);
                cairo_set_line_width(cairo, 0.5);
                cairo_move_to(cairo, x + 0.5, y);
                cairo_line_to(cairo, x + 0.5, y + cell_height - 1);
                cairo_stroke(cairo);
                cairo_pattern_destroy(pattern);
        }

        /* Draw ink if shown */
        if ((cells[i].ch && cells[i].flags & CELL_SHOW_INK) ||
            (current_cell == i && input && input->len)) {
                int j;

                render_sample(&cells[i].sample, i);
                if (cells[i].ch)
                        for (j = 0; j < ALTERNATES && cells[i].alts[j]; j++)
                                if (sample_valid(cells[i].alts[j],
                                                 cells[i].alt_used[j]) &&
                                    cells[i].alts[j]->ch == cells[i].ch) {
                                        render_sample(cells[i].alts[j], i);
                                        break;
                                }
        }

        /* Draw letter if recognized or training */
        else if (pc->ch && (current_cell != i || !input || !input->len)) {
                PangoLayout *layout;
                PangoRectangle ink_ext, log_ext;
                char string[6] = { 0, 0, 0, 0, 0, 0 };

                /* Training color is determined by how well a character is
                   trained */
                if (training) {
                        if (samples)
                                highlight_gdk_color(&color_ink, &color,
                                                    0.5 - ((double)samples) /
                                                    samples_max / 2.);
                        else
                                highlight_gdk_color(&color_inactive,
                                                    &color, 0.2);
                }

                /* Use ink color unless this is a questionable match */
                else {
                        color = color_ink;
                        if (!(pc->flags & CELL_VERIFIED) && pc->alts[0] &&
                            pc->alts[1] && pc->ch == pc->alts[0]->ch &&
                            pc->alt_ratings[0] - pc->alt_ratings[1] <= 10)
                                color = color_select;
                }

                cairo_set_source_gdk_color(cairo, &color, 1.);
                layout = pango_layout_new(pango);
                cairo_move_to(cairo, x, y);
                g_unichar_to_utf8(pc->ch, string);
                pango_layout_set_text(layout, string, 6);
                pango_layout_set_font_description(layout, pango_font_desc);
                pango_layout_get_pixel_extents(layout, &ink_ext, &log_ext);
                cairo_rel_move_to(cairo,
                                  cell_width / 2 - log_ext.width / 2, 2);
                pango_cairo_show_layout(cairo, layout);
                g_object_unref(layout);
        }

        /* Insertion arrows */
        if (!invalid && inserting &&
            (current_cell == i || current_cell == i + 1)) {
                double width, stem, height;

                cairo_set_source_gdk_color(cairo, &color_select, 1.);
                width = CELL_BORDER;
                stem = CELL_BORDER / 2;
                height = CELL_BORDER;
                if ((!right_to_left && current_cell == i) ||
                    (right_to_left && current_cell == i + 1)) {

                        /* Top right arrow */
                        cairo_move_to(cairo, x, y + 1);
                        cairo_line_to(cairo, x + stem, y + 1);
                        cairo_line_to(cairo, x + stem, y + height);
                        cairo_line_to(cairo, x + width, y + height);
                        cairo_line_to(cairo, x, y + height * 2);
                        cairo_close_path(cairo);
                        cairo_fill(cairo);

                        /* Bottom right arrow */
                        cairo_move_to(cairo, x, y + cell_height - 1);
                        cairo_line_to(cairo, x + stem, y + cell_height - 1);
                        cairo_line_to(cairo, x + stem,
                                      y + cell_height - height);
                        cairo_line_to(cairo, x + width,
                                      y + cell_height - height);
                        cairo_line_to(cairo, x, y + cell_height - height * 2);
                        cairo_close_path(cairo);
                        cairo_fill(cairo);

                } else if ((!right_to_left && current_cell == i + 1) ||
                           (right_to_left && current_cell == i)) {
                        double ox;

                        ox = i % cell_cols == cell_cols - 1 ? 0. : 1.;

                        /* Top left arrow */
                        cairo_move_to(cairo, x + cell_width + ox, y + 1);
                        cairo_line_to(cairo, x + cell_width - stem + ox,
                                      y + 1);
                        cairo_line_to(cairo, x + cell_width - stem + ox,
                                      y + height);
                        cairo_line_to(cairo, x + cell_width - width + ox,
                                      y + height);
                        cairo_line_to(cairo, x + cell_width + ox,
                                      y + height * 2);
                        cairo_close_path(cairo);
                        cairo_fill(cairo);

                        /* Bottom left arrow */
                        cairo_move_to(cairo, x + cell_width + ox,
                                      y + cell_height - 1);
                        cairo_line_to(cairo, x + cell_width - stem + ox,
                                      y + cell_height - 1);
                        cairo_line_to(cairo, x + cell_width - stem + ox,
                                      y + cell_height - height);
                        cairo_line_to(cairo, x + cell_width - width + ox,
                                      y + cell_height - height);
                        cairo_line_to(cairo, x + cell_width + ox,
                                      y + cell_height - height * 2);
                        cairo_close_path(cairo);
                        cairo_fill(cairo);

                }
        }

        gtk_widget_queue_draw_area(drawing_area, x, y, cell_width, cell_height);
        pc->flags &= ~CELL_DIRTY;

        /* This cell may have dirtied the on-screen keyboard */
        if (show_keys && x < key_widget->x + key_widget->width &&
                         y < key_widget->y + key_widget->height &&
                         x + cell_width > key_widget->x &&
                         y + cell_height > key_widget->y)
                keys_dirty = TRUE;
}

static void render_dirty(void)
/* Render cells marked dirty */
{
        int i;

        for (i = cell_row_view * cell_cols; i < cell_rows * cell_cols; i++)
                if (cells[i].flags & CELL_DIRTY)
                        render_cell(i);
        if (show_keys && keys_dirty) {
                key_widget_render(key_widget);
                keys_dirty = FALSE;
        }
}

void cell_widget_render(void)
/* Render the cells */
{
        int i, cols, rows, width, height;

        if (!cairo || !pixmap || !pixmap_gc)
                return;

        /* On-screen keyboard eats up some cells on the end */
        cols = cell_cols;
        if (show_keys)
                cols -= KEY_WIDGET_COLS;

        /* Render cells */
        for (i = cell_row_view * cols; i < cell_rows * cols; i++)
                render_cell(i);

        /* Draw border */
        rows = cell_rows < cell_rows_pref ? cell_rows : cell_rows_pref;
        width = cell_width * cols + 1;
        height = cell_height * rows + 1;
        gdk_gc_set_rgb_fg_color(pixmap_gc, &color_bg_dark);
        if (!right_to_left)
                gdk_draw_rectangle(pixmap, pixmap_gc, FALSE, 0, 0,
                                   width, height);
        else
                gdk_draw_rectangle(pixmap, pixmap_gc, FALSE,
                                   drawing_area->allocation.width - width - 1,
                                   0, width, height);

        /* Fill extra space to the right */
        gdk_gc_set_rgb_fg_color(pixmap_gc, &color_bg);
        if (!right_to_left)
                gdk_draw_rectangle(pixmap, pixmap_gc, TRUE, width + 1, 0,
                                   drawing_area->allocation.width - width,
                                   height + 1);
        else
                gdk_draw_rectangle(pixmap, pixmap_gc, TRUE, 0, 0,
                                   drawing_area->allocation.width - width - 1,
                                   height + 1);

        /* Fill extra space below */
        gdk_draw_rectangle(pixmap, pixmap_gc, TRUE, 0, height + 1,
                           drawing_area->allocation.width,
                           drawing_area->allocation.height - height + 1);

        /* Render the on-screen keyboard */
        if (show_keys) {
                key_widget_render(key_widget);
                keys_dirty = FALSE;
        }

        /* Dirty the entire drawing area */
        gtk_widget_queue_draw(drawing_area);
}

static void clear_cell(int i)
{
        Cell *cell;

        cell = cells + i;
        cell->flags = 0;
        if (cell->ch || i == current_cell) {
                if (i == current_cell)
                        input = NULL;
                cell->flags |= CELL_DIRTY;
        }
        clear_sample(&cell->sample);
        cell->ch = 0;
        cell->alts[0] = NULL;
}

static void pad_cell(int cell)
{
        int i;

        /* Turn any blank cells behind the cell into spaces */
        for (i = cell - 1; i >= 0 && !cells[i].ch; i--) {
                cells[i].ch = ' ';
                cells[i].flags |= CELL_DIRTY;
        }
}

static void free_cells(void)
/* Free sample data */
{
        int i;

        if (!cells)
                return;
        for (i = 0; i < cell_rows * cell_cols; i++)
                clear_cell(i);
        g_free(cells);
        cells = NULL;
        input = NULL;
}

static void wrap_cells(int new_rows, int new_cols)
/* Word wrap cells */
{
        Cell *new_cells;
        int i, j, size, row, col, break_i = -1, break_j = -1;

        /* Allocate and clear the new grid */
        if (new_rows < 1)
                new_rows = 1;
        size = new_rows * new_cols * sizeof (Cell);
        new_cells = g_malloc0(size);

        for (i = 0, j = 0, row = 0, col = 0; i < cell_rows * cell_cols; i++) {
                if (!cells[i].ch)
                        continue;

                /* Break at non-alphanumeric characters */
                if (!g_unichar_isalnum(cells[i].ch)) {
                        break_i = i;
                        break_j = j;
                }

                if (col >= new_cols) {

                        /* If we need to, allocate room for the new row */
                        if (++row >= new_rows) {
                                size = ++new_rows * new_cols * sizeof (Cell);
                                new_cells = g_realloc(new_cells, size);
                                memset(new_cells + (new_rows - 1) * new_cols,
                                       0, new_cols * sizeof (Cell));
                        }

                        /* Move any hanging words down to the next row */
                        size = i - break_i - 1;
                        if (size >= 0 && size < i - 1) {
                                memset(new_cells + break_j + 1, 0,
                                       sizeof (Cell) * size);
                                i = break_i + 1;
                                break_i = -1;
                        }
                        col = 0;
                        if (!cells[i].ch)
                                continue;
                }
                new_cells[j++] = cells[i];
                col++;
        }

        /* If we have filled the last row, we need to add a new row */
        if (col >= new_cols && row >= new_rows - 1) {
                size = ++new_rows * new_cols * sizeof (Cell);
                new_cells = g_realloc(new_cells, size);
                memset(new_cells + (new_rows - 1) * new_cols, 0,
                       new_cols * sizeof (Cell));
        }

        /* Only free the cell array, NOT the samples as we have copied the
           Sample data over to the new cell array */
        g_free(cells);
        cells = new_cells;

        /* Scroll the grid */
        if (new_rows > cell_rows && new_rows > cell_rows_pref)
                cell_row_view += new_rows - cell_rows;

        /* Do not let the row view look too far down */
        if (cell_row_view + cell_rows_pref > new_rows) {
                cell_row_view = new_rows - cell_rows_pref;
                if (cell_row_view < 0)
                        cell_row_view = 0;
        }

        cell_rows = new_rows;
        cell_cols = new_cols;
}

static int set_size_request(int force)
/* Resize the drawing area if necessary */
{
        int new_w, new_h, rows, resized;

        new_w = cell_cols * cell_width + 2;
        rows = cell_rows;
        if (rows > cell_rows_pref)
                rows = cell_rows_pref;
        new_h = rows * cell_height + 2;
        resized = new_w != drawing_area->allocation.width ||
                  new_h != drawing_area->allocation.height || force;
        if (!resized)
                return FALSE;
        gtk_widget_set_size_request(drawing_area, new_w, new_h);
        return TRUE;
}

static int pack_cells(int new_rows, int new_cols)
/* Pack and position cells, resize widget and window when necessary.
   Returns TRUE if the widget was resized in the process and can expect a
   configure event in the near future. */
{
        int i, rows, range, new_range;

        /* Must have at least one row */
        if (new_rows < 1)
                new_rows = 1;

        /* Word wrapping will perform its own memory allocation */
        if (!training && cells)
                wrap_cells(new_rows, new_cols);

        else if (!cells || new_rows != cell_rows || new_cols != cell_cols) {

                /* Find minimum number of rows necessary */
                if (cells) {
                        for (i = cell_rows * cell_cols - 1; i > 0; i--)
                                if (cells[i].ch)
                                        break;
                        rows = i / new_cols + 1;
                        if (new_rows < rows)
                                new_rows = rows;
                        new_range = new_rows * new_cols;

                        /* If we have shrunk the grid, clear cells outside */
                        range = cell_rows * cell_cols;
                        for (i = new_range; i < range; i++)
                                clear_cell(i);
                } else {
                        range = 0;
                        new_range = new_rows * new_cols;
                }

                /* Allocate enough room, clear any new cells */
                cells = g_realloc(cells, new_rows * new_cols * sizeof (Cell));
                if (new_range > range)
                        memset(cells + range, 0,
                               (new_range - range) * sizeof (Cell));

                cell_rows = new_rows;
                cell_cols = new_cols;
        }
        dirty_all();

        /* Update the scrollbar */
        if (cell_rows <= cell_rows_pref) {
                cell_row_view = 0;
                gtk_widget_hide(scrollbar);
        } else {
                GtkObject *adjustment;

                if (cell_row_view > cell_rows - cell_rows_pref)
                        cell_row_view = cell_rows - cell_rows_pref;
                if (cell_row_view < 0)
                        cell_row_view = 0;
                adjustment = gtk_adjustment_new(cell_row_view, 0, cell_rows, 1,
                                                cell_rows_pref, cell_rows_pref);
                gtk_range_set_adjustment(GTK_RANGE(scrollbar),
                                         GTK_ADJUSTMENT(adjustment));
                gtk_widget_show(scrollbar);
        }

        return set_size_request(FALSE);
}

static void stop_timeout(void)
{
        if (!timeout_source)
                return;
        g_source_remove(timeout_source);
        timeout_source = 0;
}

static void finish_cell(int cell)
{
        stop_timeout();
        if (cell < 0 || cell >= cell_rows * cell_cols ||
            !input || input->len < 1)
                return;
        cells[cell].flags |= CELL_DIRTY;

        /* Train on the input */
        if (training)
                train_sample(&cells[cell].sample, TRUE);

        /* Recognize input */
        else if (input && input->strokes[0] && input->strokes[0]->len) {
                Cell *pc = cells + cell;
                int i;

                /* Track stats */
                if (pc->ch && pc->ch != ' ')
                        rewrites++;
                inputs++;

                old_cc = cell;
                recognize_sample(input, pc->alts, ALTERNATES);
                pc->ch = input->ch;
                pc->flags &= ~CELL_VERIFIED;
                if (pc->ch)
                        pad_cell(cell);

                /* Copy the alternate ratings and usage stamps before they're
                   overwritten by another call to recognize_sample() */
                for (i = 0; i < ALTERNATES && pc->alts[i]; i++) {
                        pc->alt_ratings[i] = pc->alts[i]->rating;
                        pc->alt_used[i] = pc->alts[i]->used;
                }

                /* Add a row if this is the last cell */
                if (cell == cell_rows * cell_cols - 1)
                        pack_cells(0, cell_cols);
        }

        input = NULL;
        drawing = FALSE;
}

static gboolean finish_timeout(void)
/* Motion timeout for finishing drawing a cell */
{
        finish_cell(current_cell);
        render_dirty();
        timeout_source = 0;
        start_timeout();
        return FALSE;
}

static gboolean row_timeout(void)
/* Motion timeout for adding a row */
{
        pack_cells(cell_rows + 1, cell_cols);
        cell_widget_render();
        timeout_source = 0;
        return FALSE;
}

static int check_clear(void)
{
        int i;

        if (is_clear)
                return TRUE;
        if (training || (input && input->len))
                return FALSE;
        for (i = 0; i < cell_cols * cell_rows; i++)
                if (cells[i].ch)
                        return FALSE;
        return TRUE;
}

static gboolean is_clear_timeout(void)
/* Motion timeout for checking clear state */
{
        timeout_source = 0;
        if (is_clear || !check_clear())
                return FALSE;

        /* Show the on-screen keyboard */
        show_keys = keyboard_enabled;
        is_clear = TRUE;

        pack_cells(1, cell_cols);
        cell_widget_render();
        return FALSE;
}

static gboolean hold_timeout(void)
/* Motion timeout for popping up a hold-click context menu */
{
        if (potential_hold) {
                potential_hold = FALSE;
                stop_drawing();
                show_context_menu(1, gtk_get_current_event_time());
        }
        timeout_source = 0;
        return FALSE;
}

static void start_timeout(void)
/* If a timeout action is approriate for the current situation, start a
   timeout */
{
        GSourceFunc func = NULL;

        if (potential_hold)
                return;
        stop_timeout();
        if (cross_out)
                return;

        /* Events below are not triggered while drawing */
        if (!drawing) {
                if (input)
                        func = (GSourceFunc)finish_timeout;
                else if (!cells[cell_rows * cell_cols - 1].ch &&
                         cells[cell_rows * cell_cols - 2].ch && !training)
                        func = (GSourceFunc)row_timeout;
                else if (!is_clear && check_clear())
                        func = (GSourceFunc)is_clear_timeout;
        }

        if (func)
                timeout_source = g_timeout_add(MOTION_TIMEOUT, func, NULL);
}

static void start_hold(void)
{
        potential_hold = TRUE;
        if (timeout_source)
                g_source_remove(timeout_source);
        timeout_source = g_timeout_add(MOTION_TIMEOUT,
                                       (GSourceFunc)hold_timeout, NULL);
}

void cell_widget_set_cursor(int recreate)
/* Set the drawing area cursor to a black box pen cursor or to a blank cursor
   depending on which is appropriate */
{
        static char bits[] = { 0xff, 0xff, 0xff };      /* Square cursor */
                           /*{ 0x02, 0xff, 0x02 };*/    /* Cross cursor */
        static GdkCursor *square;
        GdkPixmap *pixmap;
        GdkCursor *cursor;

        /* Ink color changed, recreate cursor */
        if (recreate) {
                if (square)
                        gdk_cursor_unref(square);
                pixmap = gdk_bitmap_create_from_data(NULL, bits, 3, 3);
                square = gdk_cursor_new_from_pixmap(pixmap, pixmap,
                                                    &color_ink,
                                                    &color_ink, 1, 1);
                g_object_unref(pixmap);
        }
        cursor = square;

        /* Eraser cursor */
        if (eraser || cross_out) {
                GdkDisplay *display;

                display = gtk_widget_get_display(drawing_area);
                cursor = gdk_cursor_new_for_display(display, GDK_CIRCLE);
        }

        gdk_window_set_cursor(drawing_area->window,
                              invalid || inserting ? NULL : cursor);
}

static void stop_drawing(void)
/* Ends the current stroke and applies various processing functions */
{
        Stroke *stroke;

        if (!drawing) {
                if (cross_out) {
                        cross_out = FALSE;
                        cell_widget_set_cursor(FALSE);
                }
                return;
        }
        drawing = FALSE;
        if (!input || input->len >= STROKES_MAX)
                return;
        stroke = input->strokes[input->len - 1];
        smooth_stroke(stroke);
        simplify_stroke(stroke);
        process_stroke(stroke);
        render_cell(current_cell);
        render_sample(input, current_cell);
        start_timeout();
}

static void erase_cell(int cell)
{
        if (!training) {
                clear_cell(cell);
                render_dirty();
        } else {
                untrain_char(cells[cell].ch);
                render_cell(cell);
        }
}

static void check_cell(double x, double y, GdkDevice *device)
/* Check if we have changed to a different cell */
{
        int cell_x, cell_y, cell, rem_x, rem_y,
            old_inserting, old_invalid, old_eraser, old_cross_out;

        /* Stop drawing first */
        old_cross_out = cross_out;
        if (drawing && !cross_out) {
                int dx, dy;

                /* Check if we have started the cross-out gesture */
                cell_coords(current_cell, &cell_x, &cell_y);
                cell_x += cell_width / 2;
                cell_y += cell_height / 2;
                dx = cell_x - x;
                dy = cell_y - y;
                if (dx < 0)
                        dx = -dx;
                if (dy < 0)
                        dy = -dy;
                if (dx < cell_width && dy < cell_height)
                        return;

                cross_out = TRUE;
                drawing = FALSE;
                clear_sample(input);
                input = NULL;
                erase_cell(current_cell);
        }

        /* Is this the eraser tip? */
        old_eraser = eraser;
        eraser = device && device->source == GDK_SOURCE_ERASER;

        /* Adjust for border */
        x--;
        y--;

        /* Right-to-left mode inverts the x-axis */
        if (right_to_left)
                x = cell_cols * cell_width - x - 1;

        /* What cell are we hovering over? */
        cell_y = y / cell_height + cell_row_view;
        cell_x = x / cell_width;
        cell = cell_cols * cell_y + cell_x;

        /* Out of bounds or invalid cell */
        old_invalid = invalid;
        invalid = cell_x < 0 || cell_y < 0 || cell_x >= cell_cols ||
                  cell_y >= cell_rows || cell_offscreen(cell) ||
                  (training && !cells[cell].ch);

        /* Are we in the insertion hotspot? */
        rem_x = x - cell_x * cell_width;
        rem_y = y - (cell_y - cell_row_view) * cell_height;
        old_inserting = inserting;
        inserting = FALSE;
        if (!cross_out && !eraser && !invalid && !training && !input &&
            (rem_y <= CELL_BORDER * 2 ||
             rem_y > cell_height - CELL_BORDER * 2)) {
                if (rem_x <= CELL_BORDER + 1)
                        inserting = TRUE;
                else if (cell < cell_rows * cell_cols - 1 &&
                         rem_x > cell_width - CELL_BORDER) {
                        inserting = TRUE;
                        cell++;
                }
        }

        /* Current cell has changed */
        old_cc = current_cell;
        if (current_cell != cell) {
                current_cell = cell;
                if (!cross_out)
                        finish_cell(old_cc);
        }

        /* We have moved into or out of the insertion hotspot */
        if (old_inserting != inserting || old_cc != cell) {
                if (old_inserting) {
                        dirty_cell(old_cc);
                        dirty_cell(old_cc - 1);
                }
                if (inserting) {
                        dirty_cell(current_cell);
                        dirty_cell(current_cell - 1);
                }
        }

        /* Update cursor if necessary */
        if (old_invalid != invalid || old_inserting != inserting ||
            old_eraser != eraser || old_cross_out != cross_out)
                cell_widget_set_cursor(FALSE);

        render_dirty();
}

static void unclear(int render)
/* Hides the on-screen keyboard and re-renders the cells.
   FIXME we only need to render dirty cells */
{
        is_clear = FALSE;
        if (!show_keys)
                return;
        show_keys = FALSE;
        if (render)
                cell_widget_render();
}

static void draw(double x, double y)
{
        int cx, cy;

        if (current_cell < 0)
                return;

        /* Hide the on-screen keyboard */
        unclear(TRUE);

        /* New character */
        if (!input || !input->len) {
                clear_sample(&cells[current_cell].sample);
                cells[current_cell].alts[0] = NULL;
                input = &cells[current_cell].sample;
                cells[current_cell].sample.ch = cells[current_cell].ch;
        }

        /* Allocate a new stroke if we aren't already drawing */
        if (!drawing) {
                if (input->len >= STROKES_MAX)
                        return;
                input->strokes[input->len++]= stroke_new(0);
                drawing = TRUE;
                if (input->len == 1)
                        render_cell(current_cell);
        }

        /* Check bounds */
        cell_coords(current_cell, &cx, &cy);

        /* Normalize the input */
        x = (x - cx - cell_width / 2) * SCALE / cell_height;
        y = (y - cy - cell_height / 2) * SCALE / cell_height;

        draw_stroke(&input->strokes[input->len - 1], x, y);
}

static void insert_cell(int cell)
{
        int i;

        /* Find a blank to consume */
        for (i = cell; i < cell_rows * cell_cols; i++)
                if (!cells[i].ch)
                        break;

        /* Insert a row if necessary */
        if (i >= cell_rows * cell_cols - 1) {
                cells = g_realloc(cells,
                                  ++cell_rows * cell_cols * sizeof (Cell));
                memset(cells + (cell_rows - 1) * cell_cols, 0,
                       cell_cols * sizeof (Cell));
                if (cell_rows > cell_rows_pref)
                        cell_row_view++;
        }

        if (i > cell)
                memmove(cells + cell + 1, cells + cell,
                        (i - cell) * sizeof (Cell));
        cells[cell].ch = ' ';
        cells[cell].alts[0] = NULL;
        cells[cell].sample.len = 0;
        cells[cell].sample.ch = 0;
        pad_cell(cell);
        pack_cells(0, cell_cols);
        unclear(FALSE);
        cell_widget_render();
}

static void delete_cell(int cell)
{
        int i, rows;

        clear_cell(cell);
        memmove(cells + cell, cells + cell + 1,
                (cell_rows * cell_cols - cell - 1) * sizeof (Cell));

        /* Delete a row if necessary */
        for (i = 0; i < cell_cols &&
             !cells[(cell_rows - 1) * cell_cols + i].ch; i++);
        rows = cell_rows;
        if (i == cell_cols && cell_rows > 1 &&
            !cells[(cell_rows - 1) * cell_cols - 1].ch)
                rows--;
        cells[cell_rows * cell_cols - 1].ch = 0;
        cells[cell_rows * cell_cols - 1].alts[0] = NULL;

        pack_cells(0, cell_cols);
        cell_widget_render();
}

static void send_cell_key(int cell)
/* Send the key event for the cell */
{
        int i;

        if (!cells[cell].ch)
                return;

        /* Collect stats and train on corrections */
        if (cells[cell].ch != ' ') {
                if (cells[cell].ch != cells[cell].sample.ch)
                        corrections++;
                if (train_on_input && !(cells[cell].flags & CELL_SHIFTED) &&
                    cells[cell].sample.len) {
                        cells[cell].sample.ch = cells[cell].ch;
                        train_sample(&cells[cell].sample, FALSE);
                }
                characters++;
        }

        /* Update the usage time for the sample that matched this character */
        for (i = 0; i < ALTERNATES && cells[cell].alts[i]; i++) {
                if (!sample_valid(cells[cell].alts[i], cells[cell].alt_used[i]))
                        break;
                if (cells[cell].alts[i]->ch == cells[cell].ch) {
                        promote_sample(cells[cell].alts[i]);
                        break;
                }
                demote_sample(cells[cell].alts[i]);
        }

        key_event_send_char(cells[cell].ch);
}

/*
        Events
*/

/* Hold click area radius */
#define HOLD_CLICK_WIDTH 3.

/* Mask for possible buttons used by the eraser */
#define ERASER_BUTTON_MASK (GDK_MOD5_MASK | GDK_BUTTON1_MASK | \
                            GDK_BUTTON2_MASK | GDK_BUTTON3_MASK | \
                            GDK_BUTTON4_MASK | GDK_BUTTON5_MASK)

static int menu_cell, alt_menu_alts[ALTERNATES];

static void training_menu_reset(void)
{
        untrain_char(cells[menu_cell].ch);
        render_cell(menu_cell);
}

static void alt_menu_selection_done(GtkWidget *widget)
{
        gtk_widget_destroy(widget);
}

static void alt_menu_activate(GtkWidget *widget, int *alt)
{
        cells[menu_cell].ch = *alt;
        cells[menu_cell].flags |= CELL_VERIFIED;
        cells[menu_cell].flags &= ~CELL_SHIFTED;
        render_cell(menu_cell);
}

static void alt_menu_delete(void)
{
        delete_cell(menu_cell);
}

static void alt_menu_show_ink(void)
{
        cells[menu_cell].flags ^= CELL_SHOW_INK;
        render_cell(menu_cell);
}

static void alt_menu_change_case(void)
{
        if (g_unichar_isupper(cells[menu_cell].ch)) {
                cells[menu_cell].ch = g_unichar_tolower(cells[menu_cell].ch);
                cells[menu_cell].flags |= CELL_SHIFTED;
                render_cell(menu_cell);
        } else if (g_unichar_islower(cells[menu_cell].ch)) {
                cells[menu_cell].ch = g_unichar_toupper(cells[menu_cell].ch);
                cells[menu_cell].flags |= CELL_SHIFTED;
                render_cell(menu_cell);
        } else
                g_debug("Cannot change case, not an alphabetic character");
}

static gboolean scrollbar_scroll_event(GtkWidget *widget, GdkEventScroll *event)
{
        check_cell(event->x, event->y, event->device);
        return FALSE;
}

static gboolean scroll_event(GtkWidget *widget, GdkEventScroll *event)
{
        if (scrollbar && GTK_WIDGET_VISIBLE(scrollbar))
                gtk_widget_event(scrollbar, (GdkEvent*)event);
        return FALSE;
}

static void context_menu_position(GtkMenu *menu, gint *x, gint *y,
                                  gboolean *push_in)
/* Positions the two-column context menu so that the column divide is at
   the cursor rather than the upper left hand point */
{
        if (cells[menu_cell].alts[0])
                *x -= GTK_WIDGET(menu)->requisition.width / 2;
        *push_in = TRUE;
}

static void show_context_menu(int button, int time)
/* Popup the cell context menu for the current cell */
{
        GtkWidget *menu, *widget;
        int i, pos;

        /* Training menu is the same for all cells */
        if (training) {
                if (!char_trained(cells[current_cell].ch))
                        return;
                menu_cell = current_cell;
                gtk_menu_popup(GTK_MENU(training_menu), 0, 0, 0, 0,
                               button, time);
                return;
        }

        /* Can't delete blanks */
        if (!cells[current_cell].ch)
                return;

        /* Construct an alternates menu for the current button */
        menu = gtk_menu_new();
        menu_cell = current_cell;

        /* Menu -> Delete */
        widget = gtk_menu_item_new_with_label("Delete");
        g_signal_connect(G_OBJECT(widget), "activate",
                         G_CALLBACK(alt_menu_delete), NULL);
        gtk_menu_attach(GTK_MENU(menu), widget, 0, 1, 0, 1);

        /* Menu -> Show Ink */
        if (cells[menu_cell].sample.ch) {
                const char *label;

                label = cells[menu_cell].flags & CELL_SHOW_INK ?
                        "Hide ink" : "Show ink";
                widget = gtk_menu_item_new_with_label(label);
                g_signal_connect(G_OBJECT(widget), "activate",
                                 G_CALLBACK(alt_menu_show_ink), NULL);
                gtk_menu_attach(GTK_MENU(menu), widget, 0, 1, 1, 2);
        }

        /* Menu -> Change case */
        if (g_unichar_isupper(cells[menu_cell].ch) ||
                g_unichar_islower(cells[menu_cell].ch)) {
                const char *string = "To upper";

                if (g_unichar_isupper(cells[menu_cell].ch))
                        string = "To lower";
                widget = gtk_menu_item_new_with_label(string);
                g_signal_connect(G_OBJECT(widget), "activate",
                                 G_CALLBACK(alt_menu_change_case), NULL);
                gtk_menu_attach(GTK_MENU(menu), widget, 0, 1, 2, 3);
        }

        /* Menu -> Alternates */
        for (i = 0, pos = 0; i < ALTERNATES &&
                cells[current_cell].alts[i]; i++) {
                char *str;

                if (!sample_valid(cells[current_cell].alts[i],
                                  cells[current_cell].alt_used[i]))
                        continue;
                str = va("%C\t%d%%", cells[current_cell].alts[i]->ch,
                         cells[current_cell].alt_ratings[i]);
                alt_menu_alts[i] = cells[current_cell].alts[i]->ch;
                widget = gtk_check_menu_item_new_with_label(str);
                if (cells[current_cell].ch == cells[current_cell].alts[i]->ch)
                        gtk_check_menu_item_set_active(
                                             GTK_CHECK_MENU_ITEM(widget), TRUE);
                g_signal_connect(G_OBJECT(widget), "activate",
                                 G_CALLBACK(alt_menu_activate),
                                 alt_menu_alts + i);
                gtk_menu_attach(GTK_MENU(menu), widget, 1, 2, pos, pos + 1);
                pos++;
        }
        g_signal_connect(G_OBJECT(menu), "selection-done",
                         G_CALLBACK(alt_menu_selection_done), NULL);
        gtk_widget_show_all(menu);
        gtk_menu_popup(GTK_MENU(menu), 0, 0,
                       (GtkMenuPositionFunc)context_menu_position,
                       0, button, time);

}

static gboolean button_press_event(GtkWidget *widget, GdkEventButton *event)
/* Mouse button is pressed over drawing area */
{
        /* Pass on event to the on-screen keyboard */
        if (show_keys && key_widget_button_press(widget, event, key_widget))
                return TRUE;

        /* Don't process double clicks */
        if (event->type != GDK_BUTTON_PRESS)
                return TRUE;

        /* Check validity every time */
        check_cell(event->x, event->y, event->device);
        if (invalid)
                return TRUE;

        /* If we are drawing and we get a button press event it is possible
           that we never received a button release event for some reason.
           This is a fix for Zaurus drawing connected lines. */
        if (drawing)
                stop_drawing();

        /* If we have pressed with the eraser, erase the cell */
        if (eraser || event->button == 2) {
                erase_cell(current_cell);
                return TRUE;
        }

        /* Draw/activate insert with left click */
        if (event->button == 1) {
                if (inserting)
                        potential_insert = TRUE;
                else if (cells[current_cell].ch) {
                        start_hold();
                } else
                        draw(event->x, event->y);

                /* We are now counting on getting valid coordinates here so
                   save in case we are doing a potential insert/hold and we
                   don't get a motion event in between */
                cursor_x = event->x;
                cursor_y = event->y;

                return TRUE;
        }

        /* Right-click opens context menu */
        else if (event->button == 3 && current_cell >= 0 && !inserting &&
                 (!input || !input->len)) {
                show_context_menu(event->button, event->time);
                return TRUE;
        }

        return FALSE;
}

static gboolean button_release_event(GtkWidget *widget, GdkEventButton *event)
/* Mouse button is released over drawing area */
{
        /* Pass on event to the on-screen keyboard */
        if (show_keys && key_widget_button_release(widget, event, key_widget))
                return TRUE;

        /* Only handle left-clicks */
        if (event->button != 1)
                return TRUE;

        /* Complete an insertion */
        if (potential_insert && inserting) {
                insert_cell(current_cell);
                potential_insert = FALSE;
                return TRUE;
        }

        /* Cancel a hold-click */
        if (potential_hold) {
                potential_hold = FALSE;
                draw(cursor_x, cursor_y);
        }

        stop_drawing();
        return TRUE;
}

static gboolean motion_notify_event(GtkWidget *widget, GdkEventMotion *event)
/* Mouse is moved over drawing area */
{
        GdkModifierType state;
        double x, y;

        /* Fetch event coordinates */
        x = event->x;
        y = event->y;
        if (xinput_enabled) {
                gdk_device_get_state(event->device, event->window, NULL,
                                     &state);
                gdk_event_get_coords((GdkEvent*)event, &x, &y);
        }

#if GTK_CHECK_VERSION(2, 12, 0)
        /* Process a hint event (GTK >= 2.12) */
        gdk_event_request_motions(event);
#else
        /* Process a hint event (GTK <= 2.10) */
        else if (event->is_hint) {
                int nx, ny;

                gdk_window_get_pointer(event->window, &nx, &ny, &state);
                x = nx;
                y = ny;
        }
#endif

        /* If we are getting invalid output from this device with XInput
           enabled, ignore it */
        if (x < 0 || x > drawing_area->allocation.width ||
            y < 0 || y > drawing_area->allocation.width) {
                return TRUE;
        }

        /* Check where the pointer is */
        check_cell(x, y, event->device);

        /* Cancel a potential insert */
        if (potential_insert) {
                if (!inserting) {
                        potential_insert = FALSE;
                        draw(cursor_x, cursor_y);
                } else
                        return TRUE;
        }

        /* Cancel a potential hold-click */
        if (potential_hold) {
                double dx, dy;

                dx = x - cursor_x;
                dy = y - cursor_y;
                if (dx < -HOLD_CLICK_WIDTH || dx > HOLD_CLICK_WIDTH ||
                    dy < -HOLD_CLICK_WIDTH || dy > HOLD_CLICK_WIDTH) {
                        potential_hold = FALSE;
                        draw(cursor_x, cursor_y);
                } else
                        return TRUE;
        }

        cursor_x = x;
        cursor_y = y;

        /* Record and draw new segment */
        if (drawing) {
                draw(cursor_x, cursor_y);
                render_segment(input, current_cell, input->len - 1,
                               input->strokes[input->len - 1]->len - 2, NULL);
        }

        /* Erasing with the eraser. We get MOD5 rather than a button for the
           eraser being pressed on a Tablet PC. */
        else if (!invalid &&
                 (cross_out || (eraser && (state & ERASER_BUTTON_MASK))))
                erase_cell(current_cell);

        /* Plain motion restarts the finish countdown */
        start_timeout();

        return TRUE;
}

static void configure_keys(void)
{
        int width;

        /* The key widget is slaved so we need to update all of the pointers
           to the objects derived from our drawing area */
        key_widget->pixmap = pixmap;
        key_widget->cairo = cairo;
        key_widget->pixmap_gc = pixmap_gc;
        key_widget->pango = pango;

        /* Right-to-left mode affects keyboard placement */
        width = cell_width * KEY_WIDGET_COLS - KEY_WIDGET_BORDER;
        if (!right_to_left)
                key_widget_configure(key_widget, cell_cols * cell_width - width,
                                     1, width, cell_height);
        else
                key_widget_configure(key_widget, 0, 1, width, cell_height);
}

static gboolean configure_event(void)
/* Create a new backing pixmap of the appropriate size */
{
        int new_cols;

        /* Do nothing if we are not visible */
        if (!drawing_area || !drawing_area->window ||
            !GTK_WIDGET_VISIBLE(drawing_area))
                return TRUE;

        /* Backing pixmap */
        if (pixmap) {
                int old_width, old_height;

                /* Do not update if the size has not changed */
                gdk_drawable_get_size(key_widget->pixmap,
                                      &old_width, &old_height);
                if (old_width == drawing_area->allocation.width &&
                    old_height == drawing_area->allocation.height)
                        return TRUE;

                g_object_unref(pixmap);
        }
        pixmap = gdk_pixmap_new(drawing_area->window,
                                drawing_area->allocation.width,
                                drawing_area->allocation.height, -1);
        trace("%dx%d", drawing_area->allocation.width,
              drawing_area->allocation.height);

        /* GDK graphics context */
        if (pixmap_gc)
                g_object_unref(pixmap_gc);
        pixmap_gc = gdk_gc_new(GDK_DRAWABLE(pixmap));

        /* Cairo context */
        if (cairo)
                cairo_destroy(cairo);
        cairo = gdk_cairo_create(GDK_DRAWABLE(pixmap));

        /* Set font size */
        pango_font_description_set_absolute_size(pango_font_desc, PANGO_SCALE *
                                                 (cell_height -
                                                  CELL_BASELINE - 2));

        /* Get the background color */
        color_bg = window->style->bg[0];
        color_bg_dark = window->style->bg[1];

        /* Cursor */
        cell_widget_set_cursor(TRUE);

        /* If the cell dimensions changed, repack */
        if (window_embedded) {
                new_cols = (drawing_area->allocation.width -
                            cell_widget_scrollbar_width() - 6) / cell_width;
                if (new_cols != cell_cols)
                        pack_cells(1, new_cols);
        }

        /* If we are embedded we won't be able to resize the window so we
           can't honor the maximum rows preference */
        if (window_embedded)
                cell_rows_pref = drawing_area->allocation.height / cell_height;

        /* Update the key widget with new values */
        configure_keys();

        /* Render the cells */
        cell_widget_render();

        return TRUE;
}

static gboolean expose_event(GtkWidget *widget, GdkEventExpose *event)
/* Redraw the drawing area from the backing pixmap */
{
        if (!pixmap)
                return FALSE;
        gdk_draw_drawable(widget->window,
                          widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
                          pixmap, event->area.x, event->area.y, event->area.x,
                          event->area.y, event->area.width, event->area.height);
        return FALSE;
}

static gboolean enter_notify_event(GtkWidget *widget, GdkEventCrossing *event)
{
        check_cell(event->x, event->y, NULL);
        return FALSE;
}

static void scrollbar_value_changed(void)
/* The cell widget has been scrolled */
{
        double value;

        value = gtk_range_get_value(GTK_RANGE(scrollbar));
        if ((int)value == cell_row_view)
                return;
        cell_row_view = value;
        cell_widget_render();
}

/*
        Widget
*/

void cell_widget_enable_xinput(int on)
/* Enable Xinput devices. We set everything to screen mode despite the fact
   that we actually want window coordinates. Window mode just seems to break
   everything and we get window coords with screen mode anyway! */
{
        GList *list;
        GdkDevice *device;
        int i, mode;

        gtk_widget_set_extension_events(drawing_area,
                                        on ? GDK_EXTENSION_EVENTS_ALL :
                                             GDK_EXTENSION_EVENTS_NONE);
        mode = on ? GDK_MODE_SCREEN : GDK_MODE_DISABLED;
        list = gdk_devices_list();
        for (i = 0; (device = (GdkDevice*)g_list_nth_data(list, i)); i++)
                gdk_device_set_mode(device, mode);
        xinput_enabled = on;
        g_debug(on ? "Xinput events enabled" : "Xinput events disabled");
}

int cell_widget_update_colors(void)
{
        GdkColor old_active, old_inactive, old_ink, old_select;

        old_active = color_active;
        old_inactive = color_inactive;
        old_ink = color_ink;
        old_select = color_select;
        color_active = custom_active_color;
        color_inactive = custom_inactive_color;
        color_ink = custom_ink_color;
        color_select = custom_select_color;
        if (style_colors) {
                color_active = window->style->base[0];
                color_ink = window->style->text[0];
                color_inactive = window->style->bg[1];
        }
        return !gdk_colors_equal(&old_active, &color_active) ||
               !gdk_colors_equal(&old_inactive, &color_inactive) ||
               !gdk_colors_equal(&old_ink, &color_ink) ||
               !gdk_colors_equal(&old_select, &color_select);
}

const char *cell_widget_word(void)
/* Return the current word and the current cell's position in that word
   FIXME this function ignores wide chars */
{
        static char buf[64];
        int i, min, max;

        memset(buf, 0, sizeof (buf));
        if (cell_offscreen(old_cc))
                return buf;

        /* Find the start of the word */
        for (min = old_cc - 1; min >= 0 && cells[min].ch &&
             g_ascii_isalnum(cells[min].ch) && cells[min].ch < 0x7f; min--);

        /* Find the end of the word */
        for (max = old_cc + 1; max < cell_rows * cell_cols && cells[max].ch &&
             g_ascii_isalnum(cells[max].ch) && cells[max].ch < 0x7f; max++);

        /* Copy the word to a buffer */
        for (++min, i = 0; i < max - min && i < (int)sizeof (buf) - 1; i++)
                buf[i] = cells[min + i].ch;
        buf[old_cc - min] = 0;
        buf[i] = 0;

        return buf;
}

void cell_widget_clear(void)
{
        stop_timeout();
        free_cells();

        /* Restore cells if we just finished training */
        if (training) {
                cells = cells_saved;
                cell_rows = cell_rows_saved;
                cell_cols = cell_cols_saved;
                cell_row_view = cell_row_view_saved;
                training = FALSE;
                pack_cells(cell_rows, cell_cols);

                /* Show the on-screen keyboard */
                if (check_clear()) {
                        show_keys = keyboard_enabled;
                        is_clear = TRUE;
                }
        }

        /* Clear cells otherwise */
        else {
                pack_cells(1, cell_cols);

                /* Show the on-screen keyboard */
                show_keys = keyboard_enabled;
                is_clear = TRUE;
        }

        cell_widget_render();
}

void cell_widget_train(void)
{
        UnicodeBlock *block;
        int i, pos, range;

        stop_timeout();

        /* Save cells */
        if (!training) {
                cells_saved = cells;
                cell_rows_saved = cell_rows;
                cell_cols_saved = cell_cols;
                cell_row_view_saved = cell_row_view;
                cells = NULL;
                cell_row_view = 0;
        }

        /* Clear if not training any block */
        if (training_block < 0) {
                free_cells();
                pack_cells(1, cell_cols);
                cell_widget_render();
                return;
        }

        /* Pack the Unicode block's characters into the cell grid */
        block = unicode_blocks + training_block;
        range = block->end - block->start + 1;
        training = TRUE;
        pack_cells((range + cell_cols - 1) / cell_cols, cell_cols);

        /* Preset all of the characters for training */
        for (i = 0, pos = 0; i < range; i++) {
                gunichar ch;

                ch = block->start + i;
                if (char_disabled(ch))
                        continue;
                cells[pos].ch = ch;
                cells[pos].alts[0] = NULL;
                cells[pos++].flags = 0;
        }
        range = pos;
        for (; pos < cell_rows * cell_cols; pos++)
                clear_cell(pos);
        pack_cells(1, cell_cols);

        unclear(FALSE);
        cell_widget_render();
}

void cell_widget_pack(void)
{
        int cols;

        if (training) {
                cell_widget_train();
                return;
        }
        cols = cell_cols_pref;
        if (window_docked) {
                GdkScreen *screen;

                screen = gtk_window_get_screen(GTK_WINDOW(window));
                cols = (gdk_screen_get_width(screen) -
                        cell_widget_scrollbar_width() - 6) / cell_width;
        }
        if (!pack_cells(0, cols))
                set_size_request(TRUE);
        if (is_clear)
                show_keys = keyboard_enabled;

        /* Right-to-left mode may have changed so we need to reconfigure the
           on-screen keyboard */
        configure_keys();

        cell_widget_render();
        trace("%dx%d, scrollbar %d",
               cell_cols, cell_rows, cell_widget_scrollbar_width());
}

int cell_widget_insert(void)
{
        gunichar *utf16;
        int i, j, slot, chars;

        if (training)
                return FALSE;
        chars = 0;

        /* Prepare for sending key events */
        key_event_update_mappings();

        /* Need to send the keys out in reverse order for right_to_left mode
           because the cells are displayed with columns reversed */
        if (right_to_left)
                for (i = cell_cols - 1; i < cell_rows * cell_cols; i--) {
                        if (cells[i].ch) {
                                chars++;
                                send_cell_key(i);
                        }
                        if (i % cell_cols == 0)
                                i += cell_cols * 2;
                }

        else
                for (i = 0; i < cell_rows * cell_cols; i++) {
                        if (!cells[i].ch)
                                continue;
                        chars++;
                        send_cell_key(i);
                }

        /* If nothing was entered, send Enter key event */
        if (!chars) {
                key_event_send_enter();
                return FALSE;
        }

        /* Create a UTF-16 string representation */
        utf16 = g_malloc(sizeof (**history) * (chars + 1));
        for (i = 0, j = 0; i < cell_rows * cell_cols; i++)
                if (cells[i].ch)
                        utf16[j++] = cells[i].ch;
        utf16[j] = 0;

        /* If this text has been entered before, consume that history slot */
        slot = HISTORY_MAX - 1;
        for (i = 0; i < slot && history[i]; i++)
                for (j = 0; history[i][j] == utf16[j]; j++)
                        if (!utf16[j]) {
                                slot = i;
                                break;
                        }

        /* Save entered text to history */
        g_free(history[slot]);
        memmove(history + 1, history, sizeof (*history) * slot);
        history[0] = utf16;

        cell_widget_clear();
        return TRUE;
}

static void buffer_menu_deactivate(GtkMenuShell *shell, GtkWidget *button)
{
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);
}

static void buffer_menu_item_activate(GtkWidget *widget, gunichar *history)
{
        int i;

        stop_timeout();
        free_cells();
        for (i = 0; history[i]; i++);
        cell_rows = i / cell_cols + 1;
        cell_cols = cell_cols;
        cells = g_malloc0(sizeof (*cells) * cell_cols * cell_rows);
        for (i = 0; history[i]; i++)
                cells[i].ch = history[i];
        pack_cells(cell_rows, cell_cols);
        unclear(TRUE);
}

static void buffer_menu_item_destroy(GtkWidget *widget, gchar *string)
{
        g_free(string);
}

static void buffer_menu_position_func(GtkMenu *menu, gint *x, gint *y,
                                      gboolean *push_in, GtkWidget *button)
{
        gdk_window_get_origin(button->window, x, y);
        *x += button->allocation.x + button->allocation.width -
              GTK_WIDGET(menu)->requisition.width;
        *y += button->allocation.y + button->allocation.height;
        *push_in = TRUE;
}

void cell_widget_show_buffer(GtkWidget *button)
/* Show input back buffer menu */
{
        static GtkWidget *menu;
        int i;

        if (menu)
                gtk_widget_destroy(GTK_WIDGET(menu));
        menu = gtk_menu_new();
        g_signal_connect(G_OBJECT(menu), "deactivate",
                         G_CALLBACK(buffer_menu_deactivate), button);
        for (i = 0; history[i] && i < HISTORY_MAX; i++) {
                GtkWidget *item;
                GError *error = NULL;
                gchar *string;

                /* Convert string from a UTF-16 array to displayable UTF-8 */
                string = g_ucs4_to_utf8(history[i], -1, NULL, NULL, &error);
                if (error) {
                        g_warning("g_ucs4_to_utf8(): %s", error->message);
                        continue;
                }

                /* Reverse the displayed string for right-to-left mode */
                if (right_to_left) {
                        gchar *reversed;

                        reversed = g_utf8_strreverse(string, -1);
                        g_free(string);
                        string = reversed;
                }

                /* Create menu item */
                item = gtk_menu_item_new_with_label(string);
                g_signal_connect(G_OBJECT(item), "destroy",
                                 G_CALLBACK(buffer_menu_item_destroy), string);
                g_signal_connect(G_OBJECT(item), "activate",
                                 G_CALLBACK(buffer_menu_item_activate),
                                 history[i]);
                gtk_menu_attach(GTK_MENU(menu), item, 0, 1, i, i + 1);
        }

        /* Show back buffer menu */
        gtk_widget_show_all(menu);
        gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
                       (GtkMenuPositionFunc)buffer_menu_position_func,
                       button, 0, gtk_get_current_event_time());
}

int cell_widget_scrollbar_width(void)
/* Gets the width of the scrollbar even if it is hidden */
{
        GtkRequisition requisition;

        if (scrollbar->requisition.width <= 1) {
                gtk_widget_size_request(scrollbar, &requisition);
                return requisition.width;
        }
        return scrollbar->requisition.width + 4;
}

int cell_widget_get_height(void)
{
        int rows;

        rows = cell_rows > cell_rows_pref ? cell_rows_pref : cell_rows;
        return rows * cell_height + 2;
}

GtkWidget *cell_widget_new(void)
/* Creates the Cell widget. Should only be called once per program run! */
{
        PangoFontMap *font_map;
        GtkWidget *widget, *hbox;

        /* Initial settings */
        cell_cols = cell_cols_pref;

        /* Create drawing area */
        drawing_area = gtk_drawing_area_new();
        g_signal_connect(G_OBJECT(drawing_area), "expose_event",
                         G_CALLBACK(expose_event), NULL);
        g_signal_connect(G_OBJECT(drawing_area), "configure_event",
                         G_CALLBACK(configure_event), NULL);
        g_signal_connect(G_OBJECT(drawing_area), "show",
                         G_CALLBACK(configure_event), NULL);
        g_signal_connect(G_OBJECT(drawing_area), "button_press_event",
                         G_CALLBACK(button_press_event), NULL);
        g_signal_connect(G_OBJECT(drawing_area), "button_release_event",
                         G_CALLBACK(button_release_event), NULL);
        g_signal_connect(G_OBJECT(drawing_area), "motion_notify_event",
                         G_CALLBACK(motion_notify_event), NULL);
        g_signal_connect(G_OBJECT(drawing_area), "enter_notify_event",
                         G_CALLBACK(enter_notify_event), NULL);
        g_signal_connect(G_OBJECT(drawing_area), "scroll_event",
                         G_CALLBACK(scroll_event), NULL);
        g_signal_connect(G_OBJECT(drawing_area), "style-set",
                         G_CALLBACK(cell_widget_update_colors), NULL);
        // Do not listen to leave_notify_event, after a certain GTK version
        // it fires for just about anything you do on the screen.
        gtk_widget_set_events(drawing_area,
                              GDK_EXPOSURE_MASK |
                              GDK_BUTTON_PRESS_MASK |
                              GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK |
                              GDK_POINTER_MOTION_HINT_MASK |
                              GDK_ENTER_NOTIFY_MASK |
                              GDK_SCROLL_MASK);

        /* Update colors */
        cell_widget_update_colors();

        /* Create small on-screen keyboard */
        key_widget = key_widget_new_small(drawing_area);

        /* Create training menu */
        training_menu = gtk_menu_new();
        widget = gtk_menu_item_new_with_label("Reset");
        g_signal_connect(G_OBJECT(widget), "activate",
                         G_CALLBACK(training_menu_reset), NULL);
        gtk_menu_attach(GTK_MENU(training_menu), widget, 0, 1, 0, 1);
        gtk_widget_show_all(training_menu);

        /* Create scroll bar */
        scrollbar = gtk_vscrollbar_new(NULL);
        gtk_widget_set_no_show_all(scrollbar, TRUE);
        g_signal_connect(G_OBJECT(scrollbar), "value-changed",
                         G_CALLBACK(scrollbar_value_changed), NULL);
        g_signal_connect(G_OBJECT(scrollbar), "scroll_event",
                         G_CALLBACK(scrollbar_scroll_event), NULL);
        gtk_widget_add_events(drawing_area, GDK_SCROLL_MASK);

        /* Box container */
        hbox = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), drawing_area, TRUE, TRUE, 2);
        gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, FALSE, 2);

        /* Create Pango font description
           FIXME font characteristics, not family */
        pango_font_desc = pango_font_description_new();
        pango_font_description_set_family(pango_font_desc, "Monospace");

        /* Pango context */
        font_map = pango_cairo_font_map_new();
        pango = pango_font_map_create_context(font_map);
        g_object_unref(font_map);

        /* Clear cells */
        cell_widget_clear();

        /* Set Xinput state */
        cell_widget_enable_xinput(xinput_enabled);

        /* Clear history */
        memset(history, 0, sizeof (history));

        return hbox;
}

void cell_widget_cleanup(void)
{
        key_widget_cleanup(key_widget);

        /* Freeing memory when closing is important when trying to sort
           legitimate memory leaks from left-over memory */
        if (pixmap)
                g_object_unref(pixmap);
        if (pixmap_gc)
                g_object_unref(pixmap_gc);
        if (cairo)
                cairo_destroy(cairo);
        if (pango)
                g_object_unref(pango);
}
