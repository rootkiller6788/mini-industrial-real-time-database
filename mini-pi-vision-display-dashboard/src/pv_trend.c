/**
 * pv_trend.c - PI Vision Trend Data Processing & Rendering (L3, L5)
 *
 * Implements trend data buffer management, Douglas-Peucker decimation,
 * Wu anti-aliased line rendering, and coordinate mapping for trends.
 *
 * Knowledge Points:
 *   L3: Circular buffer with O(1) running statistics
 *   L5: Douglas-Peucker polyline simplification (1973)
 *   L5: Xiaolin Wu anti-aliased line algorithm (1991)
 *   L3: Data-to-pixel coordinate transformation
 *   L2: Auto-scale computation with padding
 *
 * Reference: Douglas & Peucker (1973) Cartographica 10(2):112-122
 *            Wu, X. (1991) SIGGRAPH Computer Graphics 25(4)
 */

#include "pv_trend.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdio.h>

/* =========================================================================
 * L3: Circular Buffer with Running Statistics
 *
 * The trend buffer uses a circular array to efficiently store streaming
 * time-series data. Running min, max, and sum are maintained in O(1)
 * per insertion for fast auto-scaling and aggregation queries.
 * ========================================================================= */

pv_trend_buffer_t* pv_trend_buffer_create(int capacity) {
    if (capacity <= 0) return NULL;
    pv_trend_buffer_t *buf = (pv_trend_buffer_t*)calloc(1, sizeof(pv_trend_buffer_t));
    if (!buf) return NULL;
    buf->buffer = (pv_trend_point_t*)calloc((size_t)capacity, sizeof(pv_trend_point_t));
    if (!buf->buffer) { free(buf); return NULL; }
    buf->capacity = capacity;
    buf->head = 0;
    buf->count = 0;
    buf->min_value = DBL_MAX;
    buf->max_value = -DBL_MAX;
    buf->sum = 0.0;
    buf->earliest = 0;
    buf->latest = 0;
    return buf;
}

void pv_trend_buffer_destroy(pv_trend_buffer_t *buf) {
    if (!buf) return;
    free(buf->buffer);
    free(buf);
}

void pv_trend_buffer_push(pv_trend_buffer_t *buf, const pv_trend_point_t *point) {
    if (!buf || !point) return;

    if (buf->count == buf->capacity) {
        /* Buffer full: overwrite oldest and advance head */
        pv_trend_point_t *oldest = &buf->buffer[buf->head];
        buf->sum -= oldest->value;
        if (oldest->value <= buf->min_value || oldest->value >= buf->max_value) {
            buf->min_value = DBL_MAX;
            buf->max_value = -DBL_MAX;
            for (int i = 0; i < buf->count; i++) {
                int idx = (buf->head + i) % buf->capacity;
                double v = buf->buffer[idx].value;
                if (v < buf->min_value) buf->min_value = v;
                if (v > buf->max_value) buf->max_value = v;
            }
        }
        /* Overwrite at head position, then advance head */
        buf->buffer[buf->head] = *point;
        buf->head = (buf->head + 1) % buf->capacity;
        /* Recompute earliest from new head */
        buf->earliest = buf->buffer[buf->head].timestamp;
    } else {
        /* Buffer not full: append at next position */
        int write_idx = (buf->head + buf->count) % buf->capacity;
        buf->buffer[write_idx] = *point;
        buf->count++;
        if (buf->count == 1 || point->timestamp < buf->earliest)
            buf->earliest = point->timestamp;
    }

    buf->sum += point->value;
    if (point->value < buf->min_value) buf->min_value = point->value;
    if (point->value > buf->max_value) buf->max_value = point->value;
    buf->latest = point->timestamp;
}

int pv_trend_buffer_get(const pv_trend_buffer_t *buf, int index,
                         pv_trend_point_t *point) {
    if (!buf || !point) return 0;
    if (index < 0 || index >= buf->count) return 0;
    int real_idx = (buf->head + index) % buf->capacity;
    *point = buf->buffer[real_idx];
    return 1;
}

int pv_trend_buffer_get_range(const pv_trend_buffer_t *buf,
                               time_t start_time, time_t end_time,
                               pv_trend_point_t *out_points, int max_out) {
    if (!buf || !out_points || max_out <= 0) return 0;
    int found = 0;
    for (int i = 0; i < buf->count && found < max_out; i++) {
        pv_trend_point_t pt;
        if (!pv_trend_buffer_get(buf, i, &pt)) continue;
        if (pt.timestamp >= start_time && pt.timestamp <= end_time) {
            out_points[found++] = pt;
        }
    }
    return found;
}

void pv_trend_buffer_get_statistics(const pv_trend_buffer_t *buf,
                                     double *min, double *max,
                                     double *avg, int *count) {
    if (!buf) {
        if (min) *min = 0.0;
        if (max) *max = 0.0;
        if (avg) *avg = 0.0;
        if (count) *count = 0;
        return;
    }
    if (min) *min = (buf->count > 0) ? buf->min_value : 0.0;
    if (max) *max = (buf->count > 0) ? buf->max_value : 0.0;
    if (avg) *avg = (buf->count > 0) ? buf->sum / buf->count : 0.0;
    if (count) *count = buf->count;
}

/* =========================================================================
 * L5: Douglas-Peucker Polyline Simplification
 *
 * Reduces polyline points within epsilon tolerance.
 * Reference: Douglas & Peucker (1973) Cartographica 10(2):112-122.
 * Complexity: O(N log N) average, O(N^2) worst case.
 * ========================================================================= */

static double perp_dist(double x1, double y1, double x2, double y2, double x, double y) {
    double dx = x2 - x1, dy = y2 - y1;
    double len_sq = dx * dx + dy * dy;
    if (len_sq < 1e-12) {
        double ex = x - x1, ey = y - y1;
        return sqrt(ex * ex + ey * ey);
    }
    double t = ((x - x1) * dx + (y - y1) * dy) / len_sq;
    double px, py;
    if (t < 0.0) { px = x1; py = y1; }
    else if (t > 1.0) { px = x2; py = y2; }
    else { px = x1 + t * dx; py = y1 + t * dy; }
    double ex = x - px, ey = y - py;
    return sqrt(ex * ex + ey * ey);
}

static int dp_recursive(const pv_trend_point_t *pts, int start, int end,
                         double eps, char *keep) {
    if (end <= start + 1) { keep[start] = 1; if (end > start) keep[end] = 1; return (end > start) ? 2 : 1; }
    double max_d = 0.0; int max_i = start;
    double x1 = (double)pts[start].timestamp, y1 = pts[start].value;
    double x2 = (double)pts[end].timestamp, y2 = pts[end].value;
    for (int i = start + 1; i < end; i++) {
        double d = perp_dist(x1, y1, x2, y2, (double)pts[i].timestamp, pts[i].value);
        if (d > max_d) { max_d = d; max_i = i; }
    }
    if (max_d > eps) {
        int lc = dp_recursive(pts, start, max_i, eps, keep);
        int rc = dp_recursive(pts, max_i, end, eps, keep);
        return lc + rc - 1;
    }
    keep[start] = 1; keep[end] = 1;
    return (end > start) ? 2 : 1;
}

int pv_trend_decimate_douglas_peucker(const pv_trend_point_t *points,
                                       int num_points, int max_points,
                                       double epsilon, pv_trend_point_t *out_points) {
    if (!points || !out_points || num_points <= 0 || max_points <= 0) return 0;
    if (num_points <= max_points) {
        memcpy(out_points, points, (size_t)num_points * sizeof(pv_trend_point_t));
        return num_points;
    }
    char *keep = (char*)calloc((size_t)num_points, sizeof(char));
    if (!keep) return 0;
    int kept = dp_recursive(points, 0, num_points - 1, epsilon, keep);
    double cur_eps = epsilon;
    while (kept > max_points && cur_eps < 1e12) {
        cur_eps *= 2.0;
        memset(keep, 0, (size_t)num_points);
        kept = dp_recursive(points, 0, num_points - 1, cur_eps, keep);
    }
    if (kept > max_points) {
        /* Uniform subsampling fallback */
        kept = 0;
        double step = (num_points > 1) ? (double)(num_points - 1) / (max_points - 1) : 0.0;
        for (int i = 0; i < max_points; i++) {
            int idx = (int)(i * step + 0.5);
            if (idx >= num_points) idx = num_points - 1;
            out_points[kept++] = points[idx];
        }
        free(keep);
        return kept;
    }
    int out_n = 0;
    for (int i = 0; i < num_points && out_n < max_points; i++)
        if (keep[i]) out_points[out_n++] = points[i];
    free(keep);
    return out_n;
}

/* =========================================================================
 * L5: Xiaolin Wu Anti-Aliased Line Algorithm (1991)
 *
 * Renders lines with sub-pixel intensity for smooth appearance.
 * Reference: Wu, X. (1991) SIGGRAPH 25(4):175-180.
 * ========================================================================= */

static double ipart(double x) { return floor(x); }
static double fpart(double x) { return x - floor(x); }
static double rfpart(double x) { return 1.0 - fpart(x); }
static void swap_int(int *a, int *b) { int t = *a; *a = *b; *b = t; }

int pv_trend_render_line_wu(int x1, int y1, int x2, int y2,
                             pv_color_t color, pv_trend_line_segment_t *segs,
                             int max_segs) {
    if (!segs || max_segs <= 0) return 0;
    int count = 0;
    int steep = abs(y2 - y1) > abs(x2 - x1);
    if (steep) { swap_int(&x1, &y1); swap_int(&x2, &y2); }
    if (x1 > x2) { swap_int(&x1, &x2); swap_int(&y1, &y2); }

    double dx = (double)(x2 - x1), dy = (double)(y2 - y1);
    double grad = (dx > 0.0) ? dy / dx : 0.0;

    /* First endpoint */
    double xend = round((double)x1), yend = (double)y1 + grad * (xend - (double)x1);
    double xgap = rfpart((double)x1 + 0.5);
    int xpxl1 = (int)xend, ypxl1 = (int)ipart(yend);
    if (count + 1 < max_segs) {
        if (steep) { segs[count].x1 = ypxl1; segs[count].y1 = xpxl1; }
        else { segs[count].x1 = xpxl1; segs[count].y1 = ypxl1; }
        segs[count].intensity = rfpart(yend) * xgap;
        segs[count].color = color; count++;
    }
    if (count < max_segs) {
        if (steep) { segs[count].x1 = ypxl1+1; segs[count].y1 = xpxl1; }
        else { segs[count].x1 = xpxl1; segs[count].y1 = ypxl1+1; }
        segs[count].intensity = fpart(yend) * xgap;
        segs[count].color = color; count++;
    }

    double intery = yend + grad;
    /* Second endpoint x */
    double xend2 = round((double)x2), yend2 = (double)y2 + grad * (xend2 - (double)x2);
    double xgap2 = fpart((double)x2 + 0.5);
    int xpxl2 = (int)xend2, ypxl2 = (int)ipart(yend2);

    /* Main loop */
    for (int x = xpxl1 + 1; x <= xpxl2 - 1 && count + 1 < max_segs; x++) {
        int iy = (int)ipart(intery);
        if (count < max_segs) {
            if (steep) { segs[count].x1 = iy; segs[count].y1 = x; }
            else { segs[count].x1 = x; segs[count].y1 = iy; }
            segs[count].intensity = rfpart(intery);
            segs[count].color = color; count++;
        }
        if (count < max_segs) {
            if (steep) { segs[count].x1 = iy+1; segs[count].y1 = x; }
            else { segs[count].x1 = x; segs[count].y1 = iy+1; }
            segs[count].intensity = fpart(intery);
            segs[count].color = color; count++;
        }
        intery += grad;
    }

    /* Second endpoint */
    if (count + 1 < max_segs) {
        if (steep) { segs[count].x1 = ypxl2; segs[count].y1 = xpxl2; }
        else { segs[count].x1 = xpxl2; segs[count].y1 = ypxl2; }
        segs[count].intensity = rfpart(yend2) * xgap2;
        segs[count].color = color; count++;
    }
    if (count < max_segs) {
        if (steep) { segs[count].x1 = ypxl2+1; segs[count].y1 = xpxl2; }
        else { segs[count].x1 = xpxl2; segs[count].y1 = ypxl2+1; }
        segs[count].intensity = fpart(yend2) * xgap2;
        segs[count].color = color; count++;
    }
    return count;
}

/* =========================================================================
 * L3: Trend Coordinate Mapping
 *
 * Convert data values and timestamps to pixel coordinates for rendering.
 * Uses linear mapping with boundary clamping.
 * ========================================================================= */

int pv_trend_value_to_pixel_y(double value, double y_min, double y_max,
                               int plot_top, int plot_height) {
    double range = y_max - y_min;
    if (range <= 0.0) return plot_top + plot_height / 2;
    /* Clamp to range */
    double clamped = value;
    if (clamped < y_min) clamped = y_min;
    if (clamped > y_max) clamped = y_max;
    /* Map: 0.0 = bottom, 1.0 = top */
    double fraction = (clamped - y_min) / range;
    /* Y pixel: plot_top + (1 - fraction) * plot_height */
    return plot_top + (int)((1.0 - fraction) * plot_height + 0.5);
}

int pv_trend_time_to_pixel_x(time_t timestamp, time_t start_time,
                              time_t end_time, int plot_left, int plot_width) {
    double total_span = difftime(end_time, start_time);
    if (total_span <= 0.0) return plot_left;
    double offset = difftime(timestamp, start_time);
    double fraction = offset / total_span;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    return plot_left + (int)(fraction * plot_width + 0.5);
}

void pv_trend_compute_auto_scale(const pv_trend_buffer_t *buf,
                                  double *y_min, double *y_max) {
    if (!buf || buf->count == 0) {
        if (y_min) *y_min = 0.0;
        if (y_max) *y_max = 100.0;
        return;
    }
    double range = buf->max_value - buf->min_value;
    double padding;
    if (range < 1e-9) {
        /* All values identical: create artificial range */
        padding = (buf->min_value != 0.0) ? fabs(buf->min_value) * 0.1 : 10.0;
    } else {
        padding = range * 0.1;  /* 10% padding */
    }
    if (y_min) *y_min = buf->min_value - padding;
    if (y_max) *y_max = buf->max_value + padding;
}

void pv_trend_build_render_config(const pv_trend_symbol_t *trend,
                                   pv_rect_t bounds, int canvas_w, int canvas_h,
                                   const pv_time_range_t *time_range,
                                   pv_trend_render_config_t *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));

    /* Convert bounds to pixel coordinates */
    int bx, by, bw, bh;
    pv_rect_to_pixel(bounds, canvas_w, canvas_h, &bx, &by, &bw, &bh);

    /* Plot area: inset for axis labels (estimate 40px each side) */
    int margin_left = 50, margin_right = 20, margin_top = 10, margin_bottom = 30;
    config->plot_left = bx + margin_left;
    config->plot_top = by + margin_top;
    config->plot_width = bw - margin_left - margin_right;
    config->plot_height = bh - margin_top - margin_bottom;
    if (config->plot_width < 10) config->plot_width = 10;
    if (config->plot_height < 10) config->plot_height = 10;

    /* X scale: pixels per second */
    time_t t_start, t_end;
    if (time_range) {
        if (time_range->is_relative) {
            t_end = time(NULL);
            t_start = t_end - (time_t)time_range->relative_seconds;
        } else {
            t_start = time_range->start_time;
            t_end = time_range->end_time;
        }
    } else {
        t_end = time(NULL);
        t_start = t_end - 8 * 3600;
    }
    double span = difftime(t_end, t_start);
    config->x_scale = (span > 0.0) ? (double)config->plot_width / span : 1.0;
    config->x_offset = (double)config->plot_left - (double)t_start * config->x_scale;

    /* Y scales */
    double yl_min = trend->y_left_min, yl_max = trend->y_left_max;
    double yr_min = trend->y_right_min, yr_max = trend->y_right_max;
    /* Check if any pen has auto_scale for left axis */
    for (int p = 0; trend->pens && p < trend->num_pens; p++) {
        if (trend->pens[p].y_axis_index == 0 && trend->pens[p].auto_scale) {
            yl_min = 0.0; yl_max = 100.0;
        }
        if (trend->pens[p].y_axis_index == 1 && trend->pens[p].auto_scale) {
            yr_min = 0.0; yr_max = 50.0;
        }
    }
    double yl_range = yl_max - yl_min;
    double yr_range = yr_max - yr_min;
    if (yl_range <= 0.0) yl_range = 100.0;
    if (yr_range <= 0.0) yr_range = 100.0;
    config->y_left_scale = (double)config->plot_height / yl_range;
    config->y_right_scale = (double)config->plot_height / yr_range;
    config->y_left_offset = (double)(config->plot_top + config->plot_height);
    config->y_right_offset = (double)(config->plot_top + config->plot_height);
}
/* =========================================================================
 * L5: Moving Average Filter for Trend Smoothing
 *
 * Applies a simple moving average (SMA) to reduce noise in trend data.
 * Window size N: output[i] = average of input[i-N+1..i].
 * Edge points use truncated windows.
 *
 * Knowledge: Signal filtering for industrial trend displays.
 * Reference: Smith, S.W. "Digital Signal Processing" (1997)
 * ========================================================================= */

void pv_trend_moving_average(const pv_trend_point_t *input, int num_points,
                              pv_trend_point_t *output, int window_size) {
    if (!input || !output || num_points <= 0 || window_size <= 0) return;

    for (int i = 0; i < num_points; i++) {
        int start = i - window_size + 1;
        if (start < 0) start = 0;
        int count = i - start + 1;
        double sum = 0.0;
        for (int j = start; j <= i; j++) {
            sum += input[j].value;
        }
        output[i].timestamp = input[i].timestamp;
        output[i].value = sum / count;
        output[i].quality = input[i].quality;
        output[i].is_good = input[i].is_good;
    }
}

/* =========================================================================
 * L5: Linear Interpolation Between Trend Points
 *
 * Estimates the value at any timestamp between two data points.
 * Uses linear interpolation: y = y0 + (y1-y0)*(x-x0)/(x1-x0).
 *
 * Knowledge: Data interpolation for cursor/hover displays.
 * ========================================================================= */

double pv_trend_interpolate_value(const pv_trend_point_t *before,
                                   const pv_trend_point_t *after,
                                   time_t timestamp) {
    if (!before || !after) return 0.0;
    double dt = difftime(after->timestamp, before->timestamp);
    if (dt <= 0.0) return before->value;
    double offset = difftime(timestamp, before->timestamp);
    double fraction = offset / dt;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    return before->value + (after->value - before->value) * fraction;
}

/* =========================================================================
 * L5: Rate of Change Calculation
 *
 * Computes the first derivative estimate at each point using
 * central differences (where available) or forward/backward differences
 * at endpoints. Result is in EU/second.
 *
 * Knowledge: Process trend analysis, alarm on rate-of-change.
 * ========================================================================= */

void pv_trend_rate_of_change(const pv_trend_point_t *points, int num_points,
                              double *roc_output) {
    if (!points || !roc_output || num_points < 2) return;

    /* Forward difference at start */
    double dt0 = difftime(points[1].timestamp, points[0].timestamp);
    roc_output[0] = (dt0 > 0.0) ? (points[1].value - points[0].value) / dt0 : 0.0;

    /* Central differences for interior points */
    for (int i = 1; i < num_points - 1; i++) {
        double dt = difftime(points[i+1].timestamp, points[i-1].timestamp);
        roc_output[i] = (dt > 0.0) ?
            (points[i+1].value - points[i-1].value) / dt : 0.0;
    }

    /* Backward difference at end */
    if (num_points >= 2) {
        int last = num_points - 1;
        double dtn = difftime(points[last].timestamp, points[last-1].timestamp);
        roc_output[last] = (dtn > 0.0) ?
            (points[last].value - points[last-1].value) / dtn : 0.0;
    }
}

/* =========================================================================
 * L5: Min/Max Envelope Computation
 *
 * Computes the upper and lower envelope of a trend for
 * shaded area display (shows the min-max range).
 * Splits data into N equally-spaced time buckets.
 *
 * Knowledge: Statistical visualization for process capability.
 * ========================================================================= */

void pv_trend_envelope(const pv_trend_point_t *points, int num_points,
                        int num_buckets,
                        pv_trend_point_t *upper,
                        pv_trend_point_t *lower) {
    if (!points || !upper || !lower || num_points < 1 || num_buckets < 1) return;
    if (num_buckets > num_points) num_buckets = num_points;

    time_t t_min = points[0].timestamp;
    time_t t_max = points[num_points - 1].timestamp;
    double total_span = difftime(t_max, t_min);
    if (total_span <= 0.0) total_span = 1.0;

    double bucket_width = total_span / num_buckets;

    for (int b = 0; b < num_buckets; b++) {
        time_t bucket_start = t_min + (time_t)(b * bucket_width);
        time_t bucket_end = t_min + (time_t)((b + 1) * bucket_width);

        double bucket_min = 1e308, bucket_max = -1e308;
        int count = 0;

        for (int i = 0; i < num_points; i++) {
            if (points[i].timestamp >= bucket_start &&
                points[i].timestamp < bucket_end) {
                if (points[i].value < bucket_min) bucket_min = points[i].value;
                if (points[i].value > bucket_max) bucket_max = points[i].value;
                count++;
            }
        }

        time_t mid = bucket_start + (time_t)(bucket_width / 2.0);
        if (count > 0) {
            upper[b].timestamp = mid;
            upper[b].value = bucket_max;
            upper[b].quality = 100;
            upper[b].is_good = 1;
            lower[b].timestamp = mid;
            lower[b].value = bucket_min;
            lower[b].quality = 100;
            lower[b].is_good = 1;
        } else {
            upper[b].timestamp = mid; upper[b].value = 0.0;
            lower[b].timestamp = mid; lower[b].value = 0.0;
        }
    }
}

/* =========================================================================
 * L5: Trend Data Export to CSV
 *
 * Exports trend buffer data as comma-separated values for
 * external analysis tools (Excel, MATLAB, Python).
 *
 * Knowledge: Data exchange between PI System and analytics tools.
 * ========================================================================= */

int pv_trend_export_csv(const pv_trend_buffer_t *buf,
                         const char *filename) {
    if (!buf || !filename || buf->count == 0) return 0;
    FILE *fp = fopen(filename, "w");
    if (!fp) return 0;

    fprintf(fp, "timestamp,value,quality\n");
    for (int i = 0; i < buf->count; i++) {
        pv_trend_point_t pt;
        if (pv_trend_buffer_get(buf, i, &pt)) {
            char time_str[32];
            struct tm *tm_info = localtime(&pt.timestamp);
            strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", tm_info);
            fprintf(fp, "%s,%.6f,%d\n", time_str, pt.value, pt.quality);
        }
    }
    fclose(fp);
    return 1;
}

/* =========================================================================
 * L5: Data Quality Filter
 *
 * Filters out bad-quality data points from a trend buffer.
 * Returns the number of good points retained.
 *
 * Knowledge: Data quality management per OPC UA quality codes.
 * ========================================================================= */

int pv_trend_filter_by_quality(const pv_trend_point_t *input, int num_points,
                                pv_trend_point_t *output, int min_quality) {
    if (!input || !output || num_points <= 0) return 0;
    int kept = 0;
    for (int i = 0; i < num_points; i++) {
        if (input[i].quality >= min_quality && input[i].is_good) {
            output[kept++] = input[i];
        }
    }
    return kept;
}
