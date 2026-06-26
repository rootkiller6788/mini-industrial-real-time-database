/**
 * pv_trend.h - PI Vision Trend Display (L2-L3, L5)
 *
 * Trend visualization with multi-resolution data handling,
 * anti-aliased line rendering, and decimation algorithms.
 *
 * Knowledge Coverage:
 *   L2 Core Concepts: Multi-pen trends, time range sync, auto-scaling
 *   L3 Engineering Structures: Data buffer management, render pipeline
 *   L5 Algorithms: Douglas-Peucker decimation, Wu anti-aliasing
 *
 * Reference: OSIsoft PI Vision Trend Object Model
 * Course Map: MIT 2.171, Stanford ENGR205, Berkeley ME233
 */

#ifndef PV_TREND_H
#define PV_TREND_H

#include "pv_display.h"
#include "pv_symbol.h"
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    time_t timestamp;
    double value;
    int    quality;
    int    is_good;
} pv_trend_point_t;

typedef struct {
    pv_trend_point_t *buffer;
    int    capacity;
    int    head;
    int    count;
    double min_value;
    double max_value;
    double sum;
    time_t earliest;
    time_t latest;
} pv_trend_buffer_t;

typedef struct {
    int    plot_left;
    int    plot_top;
    int    plot_width;
    int    plot_height;
    double x_scale;
    double y_left_scale;
    double y_right_scale;
    double x_offset;
    double y_left_offset;
    double y_right_offset;
} pv_trend_render_config_t;

typedef struct {
    int    x1, y1, x2, y2;
    double intensity;
    pv_color_t color;
} pv_trend_line_segment_t;

pv_trend_buffer_t* pv_trend_buffer_create(int capacity);
void pv_trend_buffer_destroy(pv_trend_buffer_t *buf);
void pv_trend_buffer_push(pv_trend_buffer_t *buf, const pv_trend_point_t *point);
int pv_trend_buffer_get(const pv_trend_buffer_t *buf, int index, pv_trend_point_t *point);
int pv_trend_buffer_get_range(const pv_trend_buffer_t *buf, time_t start_time, time_t end_time, pv_trend_point_t *out_points, int max_out);
void pv_trend_buffer_get_statistics(const pv_trend_buffer_t *buf, double *min, double *max, double *avg, int *count);
int pv_trend_decimate_douglas_peucker(const pv_trend_point_t *points, int num_points, int max_points, double epsilon, pv_trend_point_t *out_points);
int pv_trend_render_line_wu(int x1, int y1, int x2, int y2, pv_color_t color, pv_trend_line_segment_t *segments, int max_segs);
int pv_trend_value_to_pixel_y(double value, double y_min, double y_max, int plot_top, int plot_height);
int pv_trend_time_to_pixel_x(time_t timestamp, time_t start_time, time_t end_time, int plot_left, int plot_width);
void pv_trend_compute_auto_scale(const pv_trend_buffer_t *buf, double *y_min, double *y_max);
void pv_trend_build_render_config(const pv_trend_symbol_t *trend, pv_rect_t bounds, int canvas_w, int canvas_h, const pv_time_range_t *time_range, pv_trend_render_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* PV_TREND_H */

/* L5: Additional trend algorithms */
void pv_trend_moving_average(const pv_trend_point_t *input, int num_points, pv_trend_point_t *output, int window_size);
double pv_trend_interpolate_value(const pv_trend_point_t *before, const pv_trend_point_t *after, time_t timestamp);
void pv_trend_rate_of_change(const pv_trend_point_t *points, int num_points, double *roc_output);
void pv_trend_envelope(const pv_trend_point_t *points, int num_points, int num_buckets, pv_trend_point_t *upper, pv_trend_point_t *lower);
int pv_trend_export_csv(const pv_trend_buffer_t *buf, const char *filename);
int pv_trend_filter_by_quality(const pv_trend_point_t *input, int num_points, pv_trend_point_t *output, int min_quality);
/* Additional utility */
int pv_display_serialize_size(const pv_display_t *display);
int pv_display_serialize(const pv_display_t *display, char *buffer, int buf_size);
