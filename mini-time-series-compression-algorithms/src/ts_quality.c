/**
 * @file ts_quality.c
 * @brief Data Quality, Exception Filtering, and Interpolation Implementation
 *
 * Module: mini-time-series-compression-algorithms
 * Knowledge: L1 Definitions, L2 Concepts, L3 Engineering Structures
 *
 * Implements OPC UA quality flag handling, exception filtering for
 * force-archive events, time-series interpolation methods (step, linear,
 * PCHIP-style), and data quality KPIs.
 *
 * Reference: OPC UA Part 8: Data Access, §6.4
 *            ISA-88 Batch Control
 *            IEC 62541 (OPC UA)
 * Curriculum: MIT 6.302, Stanford ENGR205, RWTH Aachen, ISA/IEC
 */

#include "ts_quality.h"
#include "ts_deadband.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------------
 * L1: Quality String Conversion
 * ------------------------------------------------------------------------- */

const char* ts_quality_to_string(uint8_t quality)
{
    switch (quality & 0xC0) {
    case 0xC0: return "GOOD";
    case 0x40: return "UNCERTAIN";
    case 0x00: return "BAD";
    default:   return "UNKNOWN";
    }
}

bool ts_quality_is_valid(uint8_t quality)
{
    uint8_t major = quality & 0xC0;
    return (major == 0xC0 || major == 0x40 || major == 0x00);
}

/* ---------------------------------------------------------------------------
 * L2: Exception Filter
 * ------------------------------------------------------------------------- */

int ts_exception_init(ts_exception_state_t *state,
                       const ts_exception_config_t *config)
{
    if (!state) return -1;

    memset(state, 0, sizeof(*state));

    if (config) {
        state->config = *config;
    } else {
        /* Sensible defaults */
        memset(&state->config, 0, sizeof(state->config));
    }

    state->initialized = false;
    return 0;
}

bool ts_exception_evaluate(ts_exception_state_t *state,
                            const ts_data_point_t *point,
                            ts_exception_type_t *exc_type)
{
    if (!state || !point || !exc_type) return false;

    *exc_type = TS_EXCEPTION_NONE;

    if (!state->initialized) {
        state->prev_quality = point->quality;
        state->prev_value = point->value;
        state->prev_timestamp_us = point->epoch_us;
        state->initialized = true;
        return true;  /* First point always archived */
    }

    /* Quality change */
    if (state->config.archive_on_quality_change &&
        point->quality != state->prev_quality) {
        *exc_type = TS_EXCEPTION_QUALITY_CHANGE;
        goto trigger;
    }

    /* Bad quality */
    if (state->config.archive_on_bad_quality &&
        TS_QUALITY_IS_BAD(point->quality)) {
        *exc_type = TS_EXCEPTION_QUALITY_CHANGE;
        goto trigger;
    }

    /* Return to good */
    if (state->config.archive_on_good_quality &&
        TS_QUALITY_IS_GOOD(point->quality) &&
        !TS_QUALITY_IS_GOOD(state->prev_quality)) {
        *exc_type = TS_EXCEPTION_QUALITY_CHANGE;
        goto trigger;
    }

    /* Step change */
    if (state->config.archive_on_step_change) {
        double delta = fabs(point->value - state->prev_value);
        if (delta >= state->config.step_threshold) {
            *exc_type = TS_EXCEPTION_STEP_CHANGE;
            goto trigger;
        }
    }

    /* Rate alarm */
    if (state->config.archive_on_rate_alarm) {
        int64_t dt = point->epoch_us - state->prev_timestamp_us;
        if (dt > 0) {
            double rate = fabs(point->value - state->prev_value)
                          / ((double)dt * 1e-6);
            if (rate >= state->config.rate_limit) {
                *exc_type = TS_EXCEPTION_RATE_ALARM;
                goto trigger;
            }
        }
    }

    /* Value limit */
    if (state->config.archive_on_value_limit) {
        if (point->value < state->config.value_lo ||
            point->value > state->config.value_hi) {
            *exc_type = TS_EXCEPTION_VALUE_LIMIT;
            goto trigger;
        }
    }

    /* Update state and return false (no exception) */
    state->prev_quality = point->quality;
    state->prev_value = point->value;
    state->prev_timestamp_us = point->epoch_us;
    return false;

trigger:
    state->prev_quality = point->quality;
    state->prev_value = point->value;
    state->prev_timestamp_us = point->epoch_us;
    return true;
}

/* ---------------------------------------------------------------------------
 * L3: Interpolation
 *
 * Time-series interpolation reconstructs values at query timestamps
 * from the archived (compressed) data points.
 *
 * Methods:
 *   STEP:    v(t) = value of most recent archived point at or before t
 *   LINEAR:  v(t) = linear interpolation between bracketing points
 *   PREVIOUS: v(t) = value of most recent previous archived point
 *   NEXT:    v(t) = value of next archived point after t
 *   NEAREST: v(t) = value of nearest archived point
 * ------------------------------------------------------------------------- */

/**
 * @brief Binary search for the last archived point with timestamp <= query_t.
 *
 * Returns the index, or -1 if query_t is before all points.
 */
static int find_previous(const ts_data_point_t *archived,
                          size_t num_archived, int64_t query_t)
{
    if (num_archived == 0) return -1;
    if (query_t < archived[0].epoch_us) return -1;
    if (query_t >= archived[num_archived - 1].epoch_us)
        return (int)(num_archived - 1);

    /* Binary search */
    size_t lo = 0, hi = num_archived - 1;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (archived[mid].epoch_us <= query_t) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return (int)lo;
}

int ts_find_bracket(const ts_data_point_t *archived,
                     size_t num_archived,
                     int64_t query_t,
                     int *idx)
{
    if (!archived || !idx || num_archived < 2) return -1;

    *idx = -1;

    /* Before first point */
    if (query_t < archived[0].epoch_us) {
        *idx = -1;
        return 0;
    }

    /* After last point */
    if (query_t >= archived[num_archived - 1].epoch_us) {
        *idx = (int)(num_archived - 1);
        return 0;
    }

    /* Find left bracket */
    *idx = find_previous(archived, num_archived, query_t);
    return 0;
}

double ts_lerp(int64_t t0, double v0, int64_t t1, double v1, int64_t t)
{
    if (t1 <= t0) return v0;  /* Degenerate interval */
    if (t <= t0) return v0;
    if (t >= t1) return v1;

    double frac = (double)(t - t0) / (double)(t1 - t0);
    return v0 + (v1 - v0) * frac;
}

double ts_interpolate(const ts_data_point_t *archived,
                       size_t num_archived,
                       int64_t query_epoch_us,
                       ts_interp_method_t method)
{
    if (!archived || num_archived == 0)
        return NAN;

    switch (method) {
    case TS_INTERP_STEP: {
        int idx = find_previous(archived, num_archived, query_epoch_us);
        if (idx < 0) return archived[0].value;  /* Before all: use first */
        return archived[idx].value;
    }

    case TS_INTERP_LINEAR: {
        int idx = find_previous(archived, num_archived, query_epoch_us);
        if (idx < 0) return archived[0].value;
        if ((size_t)idx >= num_archived - 1) return archived[idx].value;

        return ts_lerp(archived[idx].epoch_us, archived[idx].value,
                        archived[idx + 1].epoch_us, archived[idx + 1].value,
                        query_epoch_us);
    }

    case TS_INTERP_PREVIOUS: {
        int idx = find_previous(archived, num_archived, query_epoch_us);
        if (idx < 0) return archived[0].value;
        return archived[idx].value;
    }

    case TS_INTERP_NEXT: {
        int idx = find_previous(archived, num_archived, query_epoch_us);
        if (idx < 0) return archived[0].value;
        if ((size_t)idx >= num_archived - 1) return archived[idx].value;
        return archived[idx + 1].value;
    }

    case TS_INTERP_NEAREST: {
        int idx = find_previous(archived, num_archived, query_epoch_us);
        if (idx < 0) return archived[0].value;
        if ((size_t)idx >= num_archived - 1) return archived[num_archived - 1].value;

        int64_t dist_prev = query_epoch_us - archived[idx].epoch_us;
        int64_t dist_next = archived[idx + 1].epoch_us - query_epoch_us;
        if (dist_prev <= dist_next) return archived[idx].value;
        else return archived[idx + 1].value;
    }

    case TS_INTERP_PCHIP: {
        /* PCHIP simplified: use linear for now */
        int idx = find_previous(archived, num_archived, query_epoch_us);
        if (idx < 0) return archived[0].value;
        if ((size_t)idx >= num_archived - 1) return archived[idx].value;
        return ts_lerp(archived[idx].epoch_us, archived[idx].value,
                        archived[idx + 1].epoch_us, archived[idx + 1].value,
                        query_epoch_us);
    }

    case TS_INTERP_NONE:
    default:
        return NAN;
    }
}

int ts_interpolate_bulk(const ts_data_point_t *archived,
                         size_t num_archived,
                         const int64_t *query_times,
                         size_t num_queries,
                         ts_interp_method_t method,
                         double *results)
{
    if (!archived || !query_times || !results || num_archived < 2)
        return -1;

    for (size_t i = 0; i < num_queries; i++) {
        results[i] = ts_interpolate(archived, num_archived,
                                     query_times[i], method);
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L3: Quality Filtering
 * ------------------------------------------------------------------------- */

int ts_filter_bad_quality(const ts_data_point_t *input,
                           size_t num_input,
                           ts_data_point_t *output,
                           size_t *num_output)
{
    if (!input || !output || !num_output) return -1;

    *num_output = 0;
    for (size_t i = 0; i < num_input; i++) {
        if (!TS_QUALITY_IS_BAD(input[i].quality)) {
            output[*num_output] = input[i];
            (*num_output)++;
        }
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L6: Quality KPI — Good Fraction
 *
 * Computes the fraction of the time interval [t_start, t_end] during
 * which the data quality was GOOD. This is a key performance indicator
 * for data availability in industrial monitoring systems.
 *
 * Uses trapezoidal integration of the quality indicator over time.
 * For each consecutive pair of points:
 *   - If both GOOD: contribution = full dt
 *   - If one GOOD:  contribution = dt/2 (ambiguous boundary)
 *   - If neither GOOD: contribution = 0
 * ------------------------------------------------------------------------- */

double ts_quality_good_fraction(const ts_data_point_t *points,
                                 size_t num_points,
                                 int64_t t_start,
                                 int64_t t_end)
{
    if (!points || num_points < 2 || t_end <= t_start) return 0.0;

    double total_dt = (double)(t_end - t_start);
    double good_dt = 0.0;

    for (size_t i = 0; i < num_points - 1; i++) {
        int64_t t_a = points[i].epoch_us;
        int64_t t_b = points[i + 1].epoch_us;

        /* Clamp to [t_start, t_end] */
        if (t_b <= t_start || t_a >= t_end) continue;
        if (t_a < t_start) t_a = t_start;
        if (t_b > t_end)   t_b = t_end;

        double dt = (double)(t_b - t_a);

        bool good_a = TS_QUALITY_IS_GOOD(points[i].quality);
        bool good_b = TS_QUALITY_IS_GOOD(points[i + 1].quality);

        if (good_a && good_b) {
            good_dt += dt;
        } else if (good_a || good_b) {
            good_dt += dt * 0.5;  /* Ambiguous boundary */
        }
        /* Neither: no contribution */
    }

    if (total_dt <= 0.0) return 0.0;
    return good_dt / total_dt;
}
