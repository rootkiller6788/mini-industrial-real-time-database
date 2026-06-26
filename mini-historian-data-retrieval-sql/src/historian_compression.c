/**
 * @file    historian_compression.c
 * @brief   Time-series compression algorithm implementations.
 *
 * Knowledge coverage:
 *   L1: Compression method implementations
 *   L2: Compression ratio, archive value reconstruction
 *   L3: Swinging door algorithm (Bristol 1990)
 *   L5: Deadband, boxcar/backfill, piecewise-linear compression
 */

#include "historian_compression.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* =========================================================================
 * L1: Compression Parameter Initialization
 * ========================================================================= */

void historian_compression_params_init(historian_compression_params_t *params)
{
    if (!params) return;
    memset(params, 0, sizeof(*params));
    params->method = HISTORIAN_COMPRESSION_SWINGING_DOOR;
    params->comp_deviation = 0.5;
    params->comp_min_time_ms = 0.0;
    params->comp_max_time_ms = 28800000.0; /* 8 hours */
    params->deadband_absolute = 0.0;
    params->deadband_percent = 0.0;
    params->boxcar_interval_ms = 60000; /* 1 minute */
    params->enable_backfill = 1;
    params->backfill_interp_type = 1; /* linear */
}

/* =========================================================================
 * L3 & L5: Swinging Door Compression (Bristol 1990)
 *
 * The Swinging Door algorithm is the most widely used compression method
 * in industrial historians. It was developed by Ed Bristol at The Foxboro
 * Company and published in ISA Transactions (1990).
 *
 * Algorithm:
 *   1. Store the first data point.
 *   2. From the last stored point (pivot at time t_last, value v_last),
 *      draw two lines: one through (t, v_last + deviation) and another
 *      through (t, v_last - deviation), for each incoming time t.
 *   3. These lines define the upper and lower slope bounds of a
 *      "parallelogram" (the "swinging door").
 *   4. For each new point (t_new, v_new):
 *      - Compute the slope from (t_last, v_last + deviation) to the new point.
 *        If this slope is lower than the current upper slope bound, tighten
 *        the upper bound to this slope.
 *      - Compute the slope from (t_last, v_last - deviation) to the new point.
 *        If this slope is higher than the current lower slope bound, tighten
 *        the lower bound to this slope.
 *   5. When upper_slope < lower_slope, the door has "closed":
 *      - Store the point that preceded the one that closed the door.
 *      - Start a new door from the just-stored point.
 *   6. Also store a point if max_time has elapsed since the last stored point
 *      (prevents infinite compression during steady-state).
 *
 * Result: The compressed data preserves the signal trend shape within
 * +/- deviation of the original values.
 *
 * Reference: Bristol, E.H. (1990). "Swinging Door Trending: Adaptive
 *            Trend Recording." ISA Transactions, 29(3), pp. 33-40.
 * ========================================================================= */

void historian_swinging_door_init(historian_swinging_door_state_t *state,
                                    double deviation,
                                    double first_value, int64_t first_time_ms)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->deviation = deviation;
    state->last_stored_value = first_value;
    state->last_stored_time_ms = first_time_ms;
    state->pivot_value = first_value;
    state->pivot_time_ms = first_time_ms;
    state->slope_upper = INFINITY;
    state->slope_lower = -INFINITY;
    state->initialized = 1;
    state->comp_min_time_ms = 0.0;
    state->comp_max_time_ms = 28800000.0; /* 8 hours, PI default */
}

int historian_swinging_door_feed(historian_swinging_door_state_t *state,
                                   double value, int64_t time_ms,
                                   int force_out,
                                   historian_data_point_t *stored_points,
                                   size_t stored_cap, size_t *stored_count)
{
    if (!state || !stored_points || !stored_count) return -1;
    *stored_count = 0;

    if (!state->initialized) {
        /* First point: store it */
        historian_swinging_door_init(state, state->deviation, value, time_ms);
        if (*stored_count < stored_cap) {
            historian_timestamp_t ts;
            ts.epoch_ms = time_ms;
            ts.tz_offset_min = 0;
            ts.is_dst = 0;
            ts.is_utc = 1;
            stored_points[0] = historian_make_point(0, ts, value,
                                                      HISTORIAN_QUAL_GOOD);
            (*stored_count)++;
        }
        return 0;
    }

    /* Check max time force-store */
    int64_t elapsed = time_ms - state->last_stored_time_ms;
    if (!force_out && state->deviation > 0 &&
        elapsed > 0 && state->comp_max_time_ms > 0 &&
        elapsed >= (int64_t)state->comp_max_time_ms) {
        force_out = 1;
    }

    /* Compute slopes from the last stored point through the door edges */
    int64_t dt = time_ms - state->last_stored_time_ms;
    if (dt <= 0) {
        /* Timestamp not advancing - ignore or handle equal timestamps */
        return 0;
    }

    double upper_edge = state->last_stored_value + state->deviation;
    double lower_edge = state->last_stored_value - state->deviation;

    /* Slope from pivot to current point through upper edge*/
    int64_t dt_pivot = time_ms - state->last_stored_time_ms;
    if (dt_pivot <= 0) dt_pivot = 1;
    double slope_upper_new = (value - upper_edge) / (double)dt_pivot;
    double slope_lower_new = (value - lower_edge) / (double)dt_pivot;

    /* Tighten bounds */
    if (slope_upper_new < state->slope_upper)
        state->slope_upper = slope_upper_new;
    if (slope_lower_new > state->slope_lower)
        state->slope_lower = slope_lower_new;

    /* Check if door has closed: upper_slope < lower_slope */
    int door_closed = (state->slope_upper < state->slope_lower);

    /* Check elapsed time for min/max time forcing */
    int min_time_elapsed = (elapsed >= (int64_t)state->comp_min_time_ms);
    int max_time_exceeded = (state->comp_max_time_ms > 0 &&
                              elapsed >= (int64_t)state->comp_max_time_ms);

    int should_store = door_closed || max_time_exceeded || force_out;

    if (should_store && min_time_elapsed && *stored_count < stored_cap) {
        /* Store the pivot point (the last point that was inside the door) */
        historian_timestamp_t ts;
        ts.epoch_ms = state->pivot_time_ms;
        ts.tz_offset_min = 0;
        ts.is_dst = 0;
        ts.is_utc = 1;
        stored_points[*stored_count] = historian_make_point(0, ts,
                                                              state->pivot_value,
                                                              HISTORIAN_QUAL_GOOD);
        (*stored_count)++;

        if (force_out && state->pivot_time_ms != time_ms &&
            *stored_count < stored_cap) {
            /* Also store the current point if forced */
            ts.epoch_ms = time_ms;
            stored_points[*stored_count] = historian_make_point(0, ts,
                                                                  value,
                                                                  HISTORIAN_QUAL_GOOD);
            (*stored_count)++;
        }

        /* Start new door from the stored point */
        state->last_stored_value = (force_out && state->pivot_time_ms != time_ms)
                                    ? value : state->pivot_value;
        state->last_stored_time_ms = (force_out && state->pivot_time_ms != time_ms)
                                      ? time_ms : state->pivot_time_ms;
        state->slope_upper = INFINITY;
        state->slope_lower = -INFINITY;
        state->pivot_value = value;
        state->pivot_time_ms = time_ms;

        /* Recompute bounds with new point */
        dt = time_ms - state->last_stored_time_ms;
        if (dt > 0) {
            double slope_u = (value - (state->last_stored_value + state->deviation))
                             / (double)dt;
            double slope_l = (value - (state->last_stored_value - state->deviation))
                             / (double)dt;
            if (slope_u < state->slope_upper) state->slope_upper = slope_u;
            if (slope_l > state->slope_lower) state->slope_lower = slope_l;
        }
    } else if (!should_store) {
        /* Update pivot to current point */
        state->pivot_value = value;
        state->pivot_time_ms = time_ms;
    }

    return 0;
}

/* =========================================================================
 * L5: Deadband Compression
 *
 * Deadband compression stores a new value only if the absolute difference
 * from the last stored value exceeds the deadband threshold.
 *
 * This is the simplest compression method:
 *   IF |v_new - v_last_stored| > deadband THEN store(v_new)
 *
 * Advantage: Simple, computational very cheap.
 * Disadvantage: Loses trend information during slow ramps.
 *
 * Often used for alarm state changes and digital/discrete tags where
 * only significant changes matter.
 * ========================================================================= */

int historian_deadband_compress(historian_data_point_t *points,
                                 size_t count, double deadband,
                                 size_t *new_count)
{
    if (!points || !new_count) return -1;

    if (count == 0) {
        *new_count = 0;
        return 0;
    }

    /* Always keep the first point */
    size_t write_idx = 0;
    /* points[0] stays in place */

    for (size_t read_idx = 1; read_idx < count; read_idx++) {
        /* Check if difference exceeds deadband */
        if (fabs(points[read_idx].value - points[write_idx].value) > deadband ||
            !isfinite(points[read_idx].value) ||
            !isfinite(points[write_idx].value)) {
            write_idx++;
            if (write_idx != read_idx) {
                points[write_idx] = points[read_idx];
            }
        }
    }

    *new_count = write_idx + 1;
    return 0;
}

/* =========================================================================
 * L5: Boxcar Compression
 *
 * Boxcar compression resamples a dense time series onto a coarser,
 * fixed-interval grid. At each interval boundary, the value is taken
 * from the nearest data point (step interpolation) or linearly
 * interpolated between two neighboring points.
 *
 * Use case: Reducing 10 Hz PLC data to 1-minute snapshots for long-term
 * storage. The historian stores a value every 60 seconds rather than
 * every 100 milliseconds - a 600:1 data reduction.
 * ========================================================================= */

int historian_boxcar_compress(const historian_data_point_t *input,
                               size_t input_count, int64_t interval_ms,
                               historian_data_point_t *output,
                               size_t output_cap, size_t *output_count)
{
    if (!input || !output || !output_count) return -1;
    *output_count = 0;

    if (input_count == 0) return 0;

    /* Start at the first timestamp, aligned to interval boundary */
    int64_t first_ts = input[0].timestamp.epoch_ms;
    int64_t last_ts = input[input_count - 1].timestamp.epoch_ms;

    /* Align to interval: ceil(first_ts / interval) * interval */
    int64_t aligned_start = ((first_ts + interval_ms - 1) / interval_ms) * interval_ms;

    size_t input_idx = 0;
    for (int64_t t = aligned_start; t <= last_ts && *output_count < output_cap;
         t += interval_ms) {

        /* Find the last point at or before time t, and first point after t */
        const historian_data_point_t *before = NULL;
        const historian_data_point_t *after = NULL;

        while (input_idx < input_count &&
               input[input_idx].timestamp.epoch_ms <= t) {
            before = &input[input_idx];
            input_idx++;
        }

        if (input_idx < input_count) {
            after = &input[input_idx];
        }

        if (before) {
            /* Use step interpolation by default (before->value) */
            double out_value = before->value;

            /* Linear interpolation if after exists and both are finite */
            if (after && isfinite(before->value) && isfinite(after->value)) {
                int64_t dt = after->timestamp.epoch_ms - before->timestamp.epoch_ms;
                if (dt > 0) {
                    double frac = (double)(t - before->timestamp.epoch_ms) / (double)dt;
                    out_value = before->value + frac * (after->value - before->value);
                }
            }

            historian_timestamp_t ts;
            ts.epoch_ms = t;
            ts.tz_offset_min = 0;
            ts.is_dst = 0;
            ts.is_utc = 1;

            output[*output_count] = historian_make_point(0, ts, out_value,
                                                           HISTORIAN_QUAL_GOOD |
                                                           HISTORIAN_QUAL_SUB_INTERPOLATED);
            (*output_count)++;
        }
    }

    return 0;
}

/* =========================================================================
 * L2: Compression Ratio
 * ========================================================================= */

double historian_compression_ratio(size_t raw_count, size_t compressed_count)
{
    if (compressed_count == 0) return (raw_count > 0) ? INFINITY : 1.0;
    return (double)raw_count / (double)compressed_count;
}

double historian_estimate_compression(double stddev, double comp_deviation)
{
    /* Heuristic based on Bristol (1990):
     *
     * When the signal's standard deviation is much larger than the
     * compression deviation, the swinging door closes frequently and
     * many points must be stored (low compression ratio).
     *
     * When stddev << comp_deviation, the door stays open and few points
     * are stored (high compression ratio).
     *
     * Empirical formula:
     *   compression_ratio ? a * (comp_deviation / stddev)^b
     *
     * With typical industrial tuning: a ? 5, b ? 0.7
     */

    if (comp_deviation <= 0.0) return 1.0; /* No compression */
    if (stddev <= 0.0) return 1000.0; /* Constant signal compresses extremely well */

    double ratio = stddev / comp_deviation;
    /* Approximate: more deviation relative to stddev means fewer stored points */
    double estimated = 5.0 * pow(ratio, -0.7) + 1.0;
    if (estimated < 1.0) estimated = 1.0;
    if (estimated > 10000.0) estimated = 10000.0;

    return estimated;
}

/* =========================================================================
 * L5: Archive Value Reconstruction
 *
 * Given a set of compressed (stored) data points, reconstruct the
 * approximate value at any arbitrary timestamp by interpolating
 * between the two nearest stored neighbors.
 *
 * This is the inverse operation of compression: the historian stores
 * compressed data and "reconstructs" on read.
 * ========================================================================= */

int historian_reconstruct_value(const historian_data_point_t *compressed,
                                 size_t comp_count, int64_t query_time_ms,
                                 int interp_type, double *value_out)
{
    if (!compressed || !value_out) return -1;
    if (comp_count == 0) { *value_out = NAN; return -1; }

    /* Binary search for the closest point */
    /* Find the last point at or before query_time */
    size_t lo = 0, hi = comp_count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (compressed[mid].timestamp.epoch_ms <= query_time_ms) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    /* lo is the index of the first point after query_time */
    /* before = lo - 1 (last point at or before query_time) */
    /* after  = lo     (first point after query_time) */

    if (lo == 0) {
        /* query_time is before all stored points */
        *value_out = NAN;
        return -2;
    }

    size_t before_idx = lo - 1;
    size_t after_idx = lo;

    if (interp_type == 0 || after_idx >= comp_count) {
        /* Step interpolation: return last known value */
        *value_out = compressed[before_idx].value;
        return 0;
    }

    /* Linear interpolation between before and after */
    double v1 = compressed[before_idx].value;
    double v2 = compressed[after_idx].value;
    int64_t t1 = compressed[before_idx].timestamp.epoch_ms;
    int64_t t2 = compressed[after_idx].timestamp.epoch_ms;

    if (t2 <= t1) {
        *value_out = v1;
        return 0;
    }

    if (!isfinite(v1) || !isfinite(v2)) {
        *value_out = isfinite(v1) ? v1 : (isfinite(v2) ? v2 : NAN);
        return 0;
    }

    double frac = (double)(query_time_ms - t1) / (double)(t2 - t1);
    *value_out = v1 + frac * (v2 - v1);
    return 0;
}
