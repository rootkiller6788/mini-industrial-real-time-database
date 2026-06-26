/**
 * pv_symbol.h - PI Vision Symbol Definitions (L1-L3)
 */
#ifndef PV_SYMBOL_H
#define PV_SYMBOL_H

#include "pv_display.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PV_FMT_DECIMAL    = 0,
    PV_FMT_SCIENTIFIC = 1,
    PV_FMT_ENGINEERING = 2,
    PV_FMT_HEX        = 3,
    PV_FMT_BINARY     = 4
} pv_number_format_t;

typedef struct {
    pv_number_format_t format;
    int    precision;
    int    show_uom;
    int    show_timestamp;
    double range_min;
    double range_max;
    double warn_high;
    double warn_low;
    double alarm_high;
    double alarm_low;
    char   custom_format[32];
} pv_value_symbol_t;

typedef struct {
    char       pen_name[64];
    pv_color_t line_color;
    double     line_width;
    int        line_style;
    int        show_markers;
    int        y_axis_index;
    double     y_min;
    double     y_max;
    int        auto_scale;
    pv_data_binding_t binding;
} pv_trend_pen_t;

typedef struct {
    int           num_pens;
    int           max_pens;
    pv_trend_pen_t *pens;
    pv_color_t    grid_color;
    pv_color_t    axis_color;
    pv_color_t    plot_bg;
    int           show_grid;
    int           show_legend;
    int           show_x_axis;
    int           show_y_axis;
    int           show_cursor;
    double        y_left_min;
    double        y_left_max;
    double        y_right_min;
    double        y_right_max;
    pv_resolution_mode_t resolution;
    int           max_data_points;
} pv_trend_symbol_t;

typedef enum {
    PV_BAR_VERTICAL   = 0,
    PV_BAR_HORIZONTAL = 1
} pv_bar_orientation_t;

typedef struct {
    pv_bar_orientation_t orientation;
    double  bar_min;
    double  bar_max;
    int     num_bars;
    double *bar_values;
    pv_color_t *bar_colors;
    char   **bar_labels;
    pv_color_t grid_color;
    int     show_values;
    int     show_scale;
    double  target_line;
    int     show_target;
} pv_bar_graph_symbol_t;

typedef enum {
    PV_GAUGE_RADIAL      = 0,
    PV_GAUGE_SEMI_CIRCLE = 1,
    PV_GAUGE_LINEAR      = 2,
    PV_GAUGE_HORIZONTAL  = 3
} pv_gauge_type_t;

typedef struct {
    double     range_start;
    double     range_end;
    pv_color_t color;
} pv_gauge_band_t;

typedef struct {
    pv_gauge_type_t type;
    double  gauge_min;
    double  gauge_max;
    double  major_tick_step;
    int     minor_ticks_per_major;
    int     num_bands;
    pv_gauge_band_t *bands;
    pv_color_t needle_color;
    pv_color_t dial_color;
    pv_color_t tick_color;
    int     show_digital_value;
    char    uom[32];
} pv_gauge_symbol_t;

typedef struct {
    int        state_value;
    char       state_label[64];
    pv_color_t state_color;
    char       icon_name[64];
} pv_state_def_t;

typedef struct {
    int            num_states;
    pv_state_def_t *states;
    int            default_state;
    int            show_label;
    int            blink_on_alarm;
    double         blink_rate_hz;
} pv_state_indicator_t;

typedef enum {
    PV_KPI_TREND_UP      = 0,
    PV_KPI_TREND_DOWN    = 1,
    PV_KPI_TREND_FLAT    = 2,
    PV_KPI_TREND_UNKNOWN = 3
} pv_kpi_trend_t;

typedef struct {
    double  target_value;
    double  min_acceptable;
    double  max_acceptable;
    double  previous_value;
    pv_kpi_trend_t trend_direction;
    double  change_percent;
    pv_color_t good_color;
    pv_color_t warn_color;
    pv_color_t bad_color;
    int     show_sparkline;
    int     sparkline_points;
    double *sparkline_data;
} pv_kpi_indicator_t;

typedef enum {
    PV_ALARM_CRITICAL = 0,
    PV_ALARM_HIGH     = 1,
    PV_ALARM_MEDIUM   = 2,
    PV_ALARM_LOW      = 3,
    PV_ALARM_INFO     = 4
} pv_alarm_severity_t;

typedef enum {
    PV_ALARM_UNACKED   = 0,
    PV_ALARM_ACKED     = 1,
    PV_ALARM_RTN_UNACK = 2,
    PV_ALARM_NORMAL    = 3
} pv_alarm_state_t;

typedef struct {
    uint32_t           alarm_id;
    pv_alarm_severity_t severity;
    pv_alarm_state_t   state;
    char               source[128];
    char               description[256];
    time_t             trigger_time;
    time_t             ack_time;
    double             trigger_value;
    double             limit_value;
} pv_alarm_record_t;

typedef struct {
    int              max_alarms;
    int              current_alarms;
    pv_alarm_record_t *alarms;
    int              filter_severity;
    int              show_acked;
    int              show_rtn;
    int              sort_by_time;
    pv_color_t       critical_color;
    pv_color_t       high_color;
    pv_color_t       medium_color;
    pv_color_t       low_color;
    pv_color_t       info_color;
    pv_color_t       unacked_bg;
} pv_alarm_list_symbol_t;

void* pv_symbol_create_value(const pv_value_symbol_t *cfg);
void* pv_symbol_create_trend(const pv_trend_symbol_t *cfg);
void* pv_symbol_create_bar_graph(const pv_bar_graph_symbol_t *cfg);
void* pv_symbol_create_gauge(const pv_gauge_symbol_t *cfg);
void* pv_symbol_create_state_indicator(const pv_state_indicator_t *cfg);
void* pv_symbol_create_kpi(const pv_kpi_indicator_t *cfg);
void* pv_symbol_create_alarm_list(const pv_alarm_list_symbol_t *cfg);
void pv_symbol_destroy(pv_symbol_type_t sym_type, void *symbol);

double pv_gauge_angle_to_value(const pv_gauge_symbol_t *gauge, double angle_deg);
double pv_gauge_value_to_angle(const pv_gauge_symbol_t *gauge, double value);
pv_color_t pv_value_get_display_color(const pv_value_symbol_t *symbol,
                                       double value,
                                       pv_color_t normal,
                                       pv_color_t warn,
                                       pv_color_t alarm);
int pv_bar_get_optimal_bar_width(int total_width_px, int num_bars);
int pv_trend_get_display_points(int canvas_width_px);

#ifdef __cplusplus
}
#endif

#endif /* PV_SYMBOL_H */

/* L5: Symbol interaction and geometry functions */
int pv_symbol_hit_test(pv_symbol_type_t sym_type, pv_rect_t bounds, pv_coord_t point, pv_coord_t canvas_size);
double pv_symbol_scale_value(double raw_percent, double zero, double span);
const pv_state_def_t* pv_symbol_find_state(const pv_state_indicator_t *si, int value);
int pv_symbol_kpi_determine_status(const pv_kpi_indicator_t *kpi, double current_value);
double pv_gauge_calculate_nice_step(double range, int max_ticks);
void pv_alarm_sort_by_severity(pv_alarm_record_t *alarms, int count);
void pv_alarm_count_by_severity(const pv_alarm_record_t *alarms, int count, int severity_counts[5]);
