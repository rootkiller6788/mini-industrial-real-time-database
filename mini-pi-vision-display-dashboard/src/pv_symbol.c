/**
 * pv_symbol.c - PI Vision Symbol Factory & Geometry (L1-L3)
 *
 * Symbol creation/destruction for all PI Vision symbol types.
 * Gauge angle<->value conversion, color determination per ISA-101.
 *
 * Knowledge Points:
 *   L1: 7 symbol factory functions with deep copy
 *   L2: Gauge geometry conversion formulas
 *   L3: Bar width optimization, display point calculation
 */

#include "pv_symbol.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void* pv_symbol_create_value(const pv_value_symbol_t *cfg) {
    if (!cfg) return NULL;
    pv_value_symbol_t *s = (pv_value_symbol_t*)malloc(sizeof(pv_value_symbol_t));
    if (s) *s = *cfg;
    return s;
}

void* pv_symbol_create_trend(const pv_trend_symbol_t *cfg) {
    if (!cfg) return NULL;
    pv_trend_symbol_t *s = (pv_trend_symbol_t*)malloc(sizeof(pv_trend_symbol_t));
    if (!s) return NULL;
    *s = *cfg;
    if (cfg->pens && cfg->num_pens > 0) {
        size_t sz = (size_t)cfg->num_pens * sizeof(pv_trend_pen_t);
        s->pens = (pv_trend_pen_t*)malloc(sz);
        if (s->pens) memcpy(s->pens, cfg->pens, sz);
        else s->num_pens = 0;
    } else { s->pens = NULL; s->num_pens = 0; }
    return s;
}

void* pv_symbol_create_bar_graph(const pv_bar_graph_symbol_t *cfg) {
    if (!cfg) return NULL;
    pv_bar_graph_symbol_t *s = (pv_bar_graph_symbol_t*)malloc(sizeof(pv_bar_graph_symbol_t));
    if (!s) return NULL;
    *s = *cfg;
    s->bar_values = NULL; s->bar_colors = NULL; s->bar_labels = NULL;
    if (cfg->num_bars > 0 && cfg->bar_values) {
        s->bar_values = (double*)malloc((size_t)cfg->num_bars * sizeof(double));
        if (s->bar_values) memcpy(s->bar_values, cfg->bar_values, (size_t)cfg->num_bars * sizeof(double));
    }
    if (cfg->num_bars > 0 && cfg->bar_colors) {
        s->bar_colors = (pv_color_t*)malloc((size_t)cfg->num_bars * sizeof(pv_color_t));
        if (s->bar_colors) memcpy(s->bar_colors, cfg->bar_colors, (size_t)cfg->num_bars * sizeof(pv_color_t));
    }
    return s;
}

void* pv_symbol_create_gauge(const pv_gauge_symbol_t *cfg) {
    if (!cfg) return NULL;
    pv_gauge_symbol_t *s = (pv_gauge_symbol_t*)malloc(sizeof(pv_gauge_symbol_t));
    if (!s) return NULL;
    *s = *cfg;
    s->bands = NULL;
    if (cfg->num_bands > 0 && cfg->bands) {
        size_t sz = (size_t)cfg->num_bands * sizeof(pv_gauge_band_t);
        s->bands = (pv_gauge_band_t*)malloc(sz);
        if (s->bands) memcpy(s->bands, cfg->bands, sz);
    }
    return s;
}

void* pv_symbol_create_state_indicator(const pv_state_indicator_t *cfg) {
    if (!cfg) return NULL;
    pv_state_indicator_t *s = (pv_state_indicator_t*)malloc(sizeof(pv_state_indicator_t));
    if (!s) return NULL;
    *s = *cfg;
    s->states = NULL;
    if (cfg->num_states > 0 && cfg->states) {
        size_t sz = (size_t)cfg->num_states * sizeof(pv_state_def_t);
        s->states = (pv_state_def_t*)malloc(sz);
        if (s->states) memcpy(s->states, cfg->states, sz);
    }
    return s;
}

void* pv_symbol_create_kpi(const pv_kpi_indicator_t *cfg) {
    if (!cfg) return NULL;
    pv_kpi_indicator_t *s = (pv_kpi_indicator_t*)malloc(sizeof(pv_kpi_indicator_t));
    if (!s) return NULL;
    *s = *cfg;
    s->sparkline_data = NULL;
    if (cfg->sparkline_points > 0 && cfg->sparkline_data) {
        size_t sz = (size_t)cfg->sparkline_points * sizeof(double);
        s->sparkline_data = (double*)malloc(sz);
        if (s->sparkline_data) memcpy(s->sparkline_data, cfg->sparkline_data, sz);
    }
    return s;
}

void* pv_symbol_create_alarm_list(const pv_alarm_list_symbol_t *cfg) {
    if (!cfg) return NULL;
    pv_alarm_list_symbol_t *s = (pv_alarm_list_symbol_t*)malloc(sizeof(pv_alarm_list_symbol_t));
    if (!s) return NULL;
    *s = *cfg;
    s->alarms = NULL;
    if (cfg->max_alarms > 0 && cfg->alarms) {
        size_t sz = (size_t)cfg->max_alarms * sizeof(pv_alarm_record_t);
        s->alarms = (pv_alarm_record_t*)malloc(sz);
        if (s->alarms) memcpy(s->alarms, cfg->alarms, sz);
    }
    return s;
}

void pv_symbol_destroy(pv_symbol_type_t sym_type, void *symbol) {
    if (!symbol) return;
    switch (sym_type) {
    case PV_SYM_TREND: { pv_trend_symbol_t *t = (pv_trend_symbol_t*)symbol; free(t->pens); break; }
    case PV_SYM_BAR_GRAPH: { pv_bar_graph_symbol_t *b = (pv_bar_graph_symbol_t*)symbol; free(b->bar_values); free(b->bar_colors); break; }
    case PV_SYM_GAUGE: { pv_gauge_symbol_t *g = (pv_gauge_symbol_t*)symbol; free(g->bands); break; }
    case PV_SYM_STATE_INDICATOR: { pv_state_indicator_t *si = (pv_state_indicator_t*)symbol; free(si->states); break; }
    case PV_SYM_KPI_INDICATOR: { pv_kpi_indicator_t *k = (pv_kpi_indicator_t*)symbol; free(k->sparkline_data); break; }
    case PV_SYM_ALARM_LIST: { pv_alarm_list_symbol_t *a = (pv_alarm_list_symbol_t*)symbol; free(a->alarms); break; }
    default: break;
    }
    free(symbol);
}

/* Gauge angle <-> value conversion.
 * Standard radial gauge: 270-degree sweep from 225 to 495 (or -45).
 * Semi-circular: 180-degree sweep from 180 to 360.
 * Linear: direct proportion with 90-degree representing full scale. */
double pv_gauge_angle_to_value(const pv_gauge_symbol_t *g, double angle_deg) {
    if (!g) return 0.0;
    double range = g->gauge_max - g->gauge_min;
    if (range <= 0.0) return g->gauge_min;
    double start_deg, sweep_deg;
    switch (g->type) {
    case PV_GAUGE_RADIAL:      start_deg = 225.0; sweep_deg = 270.0; break;
    case PV_GAUGE_SEMI_CIRCLE: start_deg = 180.0; sweep_deg = 180.0; break;
    case PV_GAUGE_LINEAR:
    case PV_GAUGE_HORIZONTAL:
    default:
        return g->gauge_min + (angle_deg / 90.0) * range;
    }
    double rel = angle_deg - start_deg;
    if (rel < 0.0) rel += 360.0;
    if (rel > sweep_deg) rel = sweep_deg;
    if (rel < 0.0) rel = 0.0;
    return g->gauge_min + (rel / sweep_deg) * range;
}

double pv_gauge_value_to_angle(const pv_gauge_symbol_t *g, double value) {
    if (!g) return 0.0;
    double range = g->gauge_max - g->gauge_min;
    if (range <= 0.0) return 225.0;
    double start_deg, sweep_deg;
    switch (g->type) {
    case PV_GAUGE_RADIAL:      start_deg = 225.0; sweep_deg = 270.0; break;
    case PV_GAUGE_SEMI_CIRCLE: start_deg = 180.0; sweep_deg = 180.0; break;
    case PV_GAUGE_LINEAR:
    case PV_GAUGE_HORIZONTAL:
    default:
        return ((value - g->gauge_min) / range) * 90.0;
    }
    double clamped = value;
    if (clamped < g->gauge_min) clamped = g->gauge_min;
    if (clamped > g->gauge_max) clamped = g->gauge_max;
    return start_deg + ((clamped - g->gauge_min) / range) * sweep_deg;
}

/* ISA-101 alarm color logic: Alarm priority > Warning > Normal */
pv_color_t pv_value_get_display_color(const pv_value_symbol_t *s, double value,
                                       pv_color_t normal, pv_color_t warn,
                                       pv_color_t alarm) {
    if (!s) return normal;
    if (s->alarm_high > s->alarm_low) {
        if (value > s->alarm_high || value < s->alarm_low) return alarm;
    }
    if (s->warn_high > s->warn_low) {
        if (value > s->warn_high || value < s->warn_low) return warn;
    }
    return normal;
}

/* Optimal bar width: bar_w = total_w / (N + (N+1)*gap_ratio)
 * where gap_ratio = 0.2 (20% of bar width) */
int pv_bar_get_optimal_bar_width(int total_width_px, int num_bars) {
    if (num_bars <= 0 || total_width_px <= 0) return 0;
    double gap_ratio = 0.2;
    double bar_w = (double)total_width_px / (num_bars + (num_bars + 1) * gap_ratio);
    int width = (int)(bar_w + 0.5);
    return (width < 2) ? 2 : width;
}

/* Displayable points = canvas_width / min_pixel_spacing (2px) */
int pv_trend_get_display_points(int canvas_width_px) {
    if (canvas_width_px <= 0) return 0;
    return canvas_width_px / 2;
}

/* =========================================================================
 * L5: Symbol Hit Testing
 *
 * Determines if a click at a given display coordinate hits a particular
 * symbol type. Different symbol types have different hit regions:
 * - Value/Gauge: rectangular bounds
 * - Trend: plot area only (exclude axis labels)
 * - Radial gauge: circular hit region
 * ========================================================================= */

int pv_symbol_hit_test(pv_symbol_type_t sym_type, pv_rect_t bounds,
                        pv_coord_t point, pv_coord_t canvas_size) {
    (void)canvas_size;  /* Reserved for future use */
    if (!pv_rect_contains_point(bounds, point)) return 0;

    switch (sym_type) {
    case PV_SYM_RADIAL_GAUGE:
    case PV_SYM_GAUGE: {
        /* Circular gauge: hit test within inscribed circle */
        double cx = bounds.origin.x + bounds.width / 2.0;
        double cy = bounds.origin.y + bounds.height / 2.0;
        double radius = (bounds.width < bounds.height) ?
            bounds.width / 2.0 : bounds.height / 2.0;
        double dx = point.x - cx;
        double dy = point.y - cy;
        return (dx * dx + dy * dy <= radius * radius) ? 1 : 0;
    }
    case PV_SYM_TREND: {
        /* Trend: exclude margin areas where axis labels are */
        double margin_pct = 5.0;
        pv_rect_t plot_area = {
            {bounds.origin.x + margin_pct, bounds.origin.y + margin_pct},
            bounds.width - 2.0 * margin_pct,
            bounds.height - 2.0 * margin_pct
        };
        return pv_rect_contains_point(plot_area, point);
    }
    default:
        /* Rectangular hit test for all other types */
        return 1;
    }
}

/* =========================================================================
 * L5: Symbol Value Scaling
 *
 * Scales a raw value (0-100% of instrument range) to engineering units
 * using the data binding's zero and span values.
 * Formula: eu_value = zero + (pct/100) * (span - zero)
 *
 * Knowledge: Instrument range conversion.
 * ========================================================================= */

double pv_symbol_scale_value(double raw_percent, double zero, double span) {
    return zero + (raw_percent / 100.0) * (span - zero);
}

/* =========================================================================
 * L5: Multi-State Value Lookup
 *
 * For state indicator symbols, finds the state definition matching
 * a given value. If no match, returns the default state.
 * This is O(num_states) - linear scan through state table.
 * ========================================================================= */

const pv_state_def_t* pv_symbol_find_state(const pv_state_indicator_t *si, int value) {
    if (!si) return NULL;
    for (int i = 0; i < si->num_states; i++) {
        if (si->states[i].state_value == value) {
            return &si->states[i];
        }
    }
    /* Return default state */
    for (int i = 0; i < si->num_states; i++) {
        if (si->states[i].state_value == si->default_state) {
            return &si->states[i];
        }
    }
    return (si->num_states > 0) ? &si->states[0] : NULL;
}

/* =========================================================================
 * L5: KPI Status Determination
 *
 * Determines KPI status (good/warning/bad) based on value relative
 * to target and acceptable range. Also determines trend direction
 * from previous value comparison.
 * ========================================================================= */

typedef enum { PV_KPI_GOOD = 0, PV_KPI_WARNING = 1, PV_KPI_BAD = 2 } pv_kpi_status_t;

int pv_symbol_kpi_determine_status(const pv_kpi_indicator_t *kpi,
                                                double current_value) {
    if (!kpi) return PV_KPI_BAD;
    if (current_value >= kpi->min_acceptable && current_value <= kpi->max_acceptable) {
        return PV_KPI_GOOD;
    }
    /* Check if within warning band (10% beyond acceptable) */
    double warn_margin = (kpi->max_acceptable - kpi->min_acceptable) * 0.1;
    if (current_value >= kpi->min_acceptable - warn_margin &&
        current_value <= kpi->max_acceptable + warn_margin) {
        return PV_KPI_WARNING;
    }
    return PV_KPI_BAD;
}

/* =========================================================================
 * L5: Gauge Tick Calculation
 *
 * Calculates the optimal number of major ticks for a gauge given
 * the range and available display space. Uses the "nice numbers"
 * algorithm: 1, 2, 5, 10, 20, 50, 100, ...
 * ========================================================================= */

double pv_gauge_calculate_nice_step(double range, int max_ticks) {
    if (range <= 0.0 || max_ticks <= 0) return range;
    double rough_step = range / max_ticks;
    /* Find the nearest nice number */
    double exponent = floor(log10(rough_step));
    double fraction = rough_step / pow(10.0, exponent);
    double nice;
    if (fraction <= 1.0) nice = 1.0;
    else if (fraction <= 2.0) nice = 2.0;
    else if (fraction <= 5.0) nice = 5.0;
    else nice = 10.0;
    return nice * pow(10.0, exponent);
}

/* =========================================================================
 * L5: Alarm Record Sorting
 *
 * Sorts alarm records by severity (descending) for display in
 * alarm summary lists. Critical alarms always appear first.
 * Uses bubble sort for small N (alarm lists are typically < 50 items).
 * ========================================================================= */

void pv_alarm_sort_by_severity(pv_alarm_record_t *alarms, int count) {
    if (!alarms || count <= 1) return;
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            /* Lower severity enum value = more severe */
            if (alarms[j].severity > alarms[j+1].severity) {
                pv_alarm_record_t tmp = alarms[j];
                alarms[j] = alarms[j+1];
                alarms[j+1] = tmp;
            }
        }
    }
}

/* =========================================================================
 * L5: Alarm Count by Severity
 *
 * Counts active alarms grouped by severity level.
 * ========================================================================= */

void pv_alarm_count_by_severity(const pv_alarm_record_t *alarms, int count,
                                 int severity_counts[5]) {
    if (severity_counts) {
        for (int i = 0; i < 5; i++) severity_counts[i] = 0;
    }
    if (!alarms || !severity_counts) return;
    for (int i = 0; i < count; i++) {
        int idx = (int)alarms[i].severity;
        if (idx >= 0 && idx < 5) severity_counts[idx]++;
    }
}
