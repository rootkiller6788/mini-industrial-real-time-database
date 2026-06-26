/**
 * @file ts_swinging_door.c
 * @brief Swinging Door / Boxcar Trending Algorithm Implementation
 *
 * Module: mini-time-series-compression-algorithms
 * Knowledge: L2 Core Concepts, L5 Algorithms, L6 Canonical Problems, L7 Industrial
 *
 * Implements the Bristol Swinging Door algorithm as used in OSIsoft PI.
 * This is arguably the most important compression algorithm in industrial
 * process control — billions of data points are compressed daily using
 * variants of this algorithm in PI, PHD, and other historian systems.
 *
 * Algorithm Origins:
 *   Developed by J. Bristol (Foxboro) and patented in 1987 (US 4,669,097).
 *   Licensed to OSIsoft as the core of the PI compression subsystem.
 *
 * The Core Idea:
 *   From the last archived point (t0, y0), draw a "door" with two leaves.
 *   The upper leaf swings down, the lower leaf swings up as new points
 *   arrive. When the door closes (upper leaf crosses below lower leaf),
 *   the point just before the crossing is archived and a new door opens.
 *
 * Formal Definition:
 *   For each incoming point (t_i, y_i):
 *     upper_bound_slope = (y_i + compdev - y0) / (t_i - t0)
 *     lower_bound_slope = (y_i - compdev - y0) / (t_i - t0)
 *     slope_max_u = min(slope_max_u, upper_bound_slope)  [door swings in]
 *     slope_min_l = max(slope_min_l, lower_bound_slope)
 *     If slope_max_u < slope_min_l: DOOR CLOSED
 *
 * Reference: Bristol, J. (1987). U.S. Patent 4,669,097.
 *            OSIsoft PI Server Reference Guide, Chapter 4.
 * Curriculum: MIT 2.171, Stanford ENGR205, Purdue ME 575
 */

#include "ts_swinging_door.h"
#include "ts_deadband.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------------
 * L5: Default Configuration
 *
 * Default compdev = 0.5 (absolute engineering units)
 * Default compmax = 3600 seconds (1 hour)
 * Default compmin = 0 (no minimum)
 * ------------------------------------------------------------------------- */

static const ts_swinging_door_config_t SWINGING_DOOR_DEFAULTS = {
    .compdev = 0.5,
    .compdev_percent = 0.0,
    .span_min = 0.0,
    .span_max = 100.0,
    .compmax_us = 3600LL * 1000000LL,
    .compmin_us = 0,
    .use_percent = false,
    .force_on_step = true,
    .step_detection_threshold = 5.0
};

/* ---------------------------------------------------------------------------
 * L2: Initialization
 * ------------------------------------------------------------------------- */

int ts_swinging_door_init(ts_swinging_door_state_t *state,
                           const ts_swinging_door_config_t *config)
{
    if (!state) return -1;

    memset(state, 0, sizeof(*state));

    if (config) {
        state->config = *config;
    } else {
        state->config = SWINGING_DOOR_DEFAULTS;
    }

    state->effective_compdev = state->config.compdev;
    if (state->config.use_percent) {
        double span = state->config.span_max - state->config.span_min;
        if (span > 0.0) {
            state->effective_compdev = state->config.compdev_percent * 0.01 * span;
        }
    }

    state->initialized = false;
    state->has_pending = false;

    return 0;
}

int ts_swinging_door_reset(ts_swinging_door_state_t *state)
{
    if (!state) return -1;

    state->initialized = false;
    state->has_pending = false;

    memset(&state->door, 0, sizeof(state->door));
    memset(&state->last_archived, 0, sizeof(state->last_archived));
    memset(&state->pending_point, 0, sizeof(state->pending_point));
    memset(&state->stats, 0, sizeof(state->stats));

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Door Opening — Initialize a new swinging door
 *
 * When a door is opened at (t0, y0), the initial bounds are:
 *   slope_max_u = +infinity  (upper leaf fully up)
 *   slope_min_l = -infinity  (lower leaf fully down)
 *
 * These extremes are represented by DBL_MAX and -DBL_MAX from <float.h>
 * via <math.h>.
 * ------------------------------------------------------------------------- */

static void door_open(ts_swinging_door_t *door,
                       int64_t t0, double y0, double compdev)
{
    door->slope_max_u =  INFINITY;
    door->slope_min_l = -INFINITY;
    door->t0_us = t0;
    door->y0 = y0;
    door->y0_upper = y0 + compdev;
    door->y0_lower = y0 - compdev;
    door->door_open_time_us = t0;
    door->points_since_open = 0;
}

/* ---------------------------------------------------------------------------
 * L5: Door Update — Process one point through the current door
 *
 * For incoming point (t, y):
 *   upper_slope = (y + compdev - y0) / (t - t0)   [line to upper corner]
 *   lower_slope = (y - compdev - y0) / (t - t0)   [line to lower corner]
 *
 * The door narrows:
 *   slope_max_u = min(slope_max_u, upper_slope)
 *   slope_min_l = max(slope_min_l, lower_slope)
 *
 * Returns: true if door is still open (slope_max_u >= slope_min_l)
 *          false if door has closed
 *
 * Note: Integer timestamp arithmetic before floating division to
 *       avoid precision issues. The denominator (t - t0) is positive
 *       for monotonically increasing timestamps.
 * ------------------------------------------------------------------------- */

static bool door_update(ts_swinging_door_t *door,
                         int64_t t, double y, double compdev)
{
    double dt = (double)(t - door->t0_us);
    if (dt <= 0.0) {
        /* Out-of-order or duplicate timestamp: force door to close */
        return false;
    }

    /* Upper pivot: line from (t0, y0+compdev) to (t, y+compdev) */
    double upper_slope = (y + compdev - door->y0_upper) / dt;

    /* Lower pivot: line from (t0, y0-compdev) to (t, y-compdev) */
    double lower_slope = (y - compdev - door->y0_lower) / dt;

    /* Narrow the door */
    if (upper_slope < door->slope_max_u) {
        door->slope_max_u = upper_slope;
    }
    if (lower_slope > door->slope_min_l) {
        door->slope_min_l = lower_slope;
    }

    door->points_since_open++;

    /* Door is open if slope_max_u >= slope_min_l */
    return (door->slope_max_u >= door->slope_min_l);
}

/* ---------------------------------------------------------------------------
 * L5: Core Swinging Door Filter
 *
 * This is the heart of the PI compression algorithm.
 *
 * State Machine:
 *   State INIT: First point → archive immediately, open new door
 *   State OPEN:  Door is open → keep buffering pending point
 *   State CLOSE: Door closed → archive PREVIOUS pending point,
 *                open new door from that archived point
 *
 * The key insight (Bristol, 1987): when the door closes, the point
 * that should be archived is NOT the point that closed the door, but
 * the PREVIOUS point. The closing point becomes the first candidate
 * in the new door.
 *
 * Complexity: O(1) per point.
 * Space: O(1) auxiliary.
 * ------------------------------------------------------------------------- */

int ts_swinging_door_filter(ts_swinging_door_state_t *state,
                             const ts_data_point_t *input,
                             bool *archived,
                             ts_data_point_t *output)
{
    if (!state || !input || !archived || !output) return -1;

    state->stats.points_received++;
    *archived = false;

    /* -----------------------------------------------------------------
     * INITIALIZATION: First point always archived
     * ----------------------------------------------------------------- */
    if (!state->initialized) {
        state->last_archived = *input;
        state->has_pending = false;
        state->initialized = true;
        state->stats.points_archived++;

        /* Open a new door from this anchor point */
        door_open(&state->door,
                   input->epoch_us, input->value,
                   state->effective_compdev);

        *archived = true;
        *output = state->last_archived;
        return 0;
    }

    /* -----------------------------------------------------------------
     * COMPMAX CHECK: Force archive if max time exceeded
     *
     * Even if the signal stays within the corridor, archive a point
     * after compmax to ensure some data timeliness.
     * ----------------------------------------------------------------- */
    int64_t dt_since_last = input->epoch_us - state->last_archived.epoch_us;
    if (state->config.compmax_us > 0 && dt_since_last >= state->config.compmax_us) {
        /* Archive pending point if exists, otherwise archive current */
        if (state->has_pending) {
            *output = state->pending_point;
        } else {
            *output = *input;
        }
        state->last_archived = *output;
        state->stats.points_archived++;
        state->has_pending = false;

        /* Open new door from this archive point */
        door_open(&state->door,
                   output->epoch_us, output->value,
                   state->effective_compdev);

        *archived = true;
        return 0;
    }

    /* -----------------------------------------------------------------
     * STEP CHANGE DETECTION
     *
     * If the value change from the last point exceeds the step
     * threshold, force-archive the pending point and open new door.
     * ----------------------------------------------------------------- */
    if (state->config.force_on_step && state->has_pending) {
        double prev_val = state->pending_point.value;
        double step = fabs(input->value - prev_val);
        if (step >= state->config.step_detection_threshold) {
            *output = state->pending_point;
            state->last_archived = *output;
            state->stats.points_archived++;
            state->has_pending = false;

            door_open(&state->door,
                       output->epoch_us, output->value,
                       state->effective_compdev);

            /* Current point becomes new pending point */
            state->pending_point = *input;
            state->has_pending = true;

            /* Update door with current point */
            door_update(&state->door, input->epoch_us, input->value,
                        state->effective_compdev);

            *archived = true;
            return 0;
        }
    }

    /* -----------------------------------------------------------------
     * DOOR FILTERING: Try to fit point through the current door
     * ----------------------------------------------------------------- */
    if (!state->has_pending) {
        /* First point after door opened: just set as pending */
        state->pending_point = *input;
        state->has_pending = true;
        return 0;
    }

    /* Try to update the door with the new point */
    bool still_open = door_update(&state->door,
                                   input->epoch_us, input->value,
                                   state->effective_compdev);

    if (still_open) {
        /* Door still open: current pending is still valid,
         * new point becomes the new pending */
        state->pending_point = *input;
        return 0;
    }

    /* -----------------------------------------------------------------
     * DOOR CLOSED: Archive the PREVIOUS pending point
     *
     * This is the critical insight from Bristol: the point that closed
     * the door is NOT archived — instead, the immediately preceding
     * point (which was inside the door) is archived.
     *
     * The closing point becomes the first pending point in a new door.
     * ----------------------------------------------------------------- */
    if (state->pending_point.epoch_us > state->last_archived.epoch_us) {
        *output = state->pending_point;
    } else {
        /* If pending is somehow before last_archived (out-of-order),
         * archive the closing point instead */
        *output = *input;
    }

    state->last_archived = *output;
    state->stats.points_archived++;
    state->stats.points_discarded += state->door.points_since_open - 1;
    *archived = true;

    /* Open new door from the newly archived point */
    door_open(&state->door,
               output->epoch_us, output->value,
               state->effective_compdev);

    /* The closing point becomes the first pending in the new door */
    state->pending_point = *input;
    state->has_pending = true;

    /* Update the new door with this point */
    door_update(&state->door, input->epoch_us, input->value,
                state->effective_compdev);

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Batch Processing
 * ------------------------------------------------------------------------- */

int ts_swinging_door_filter_batch(ts_swinging_door_state_t *state,
                                   const ts_data_point_t *inputs,
                                   size_t num_input,
                                   ts_data_point_t *outputs,
                                   size_t *num_output)
{
    if (!state || !inputs || !outputs || !num_output) return -1;

    *num_output = 0;

    for (size_t i = 0; i < num_input; i++) {
        bool archived = false;
        ts_data_point_t out_pt;
        int ret = ts_swinging_door_filter(state, &inputs[i], &archived, &out_pt);
        if (ret != 0) return ret;

        if (archived) {
            if (*num_output >= num_input) return -1;
            outputs[*num_output] = out_pt;
            (*num_output)++;
        }
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Flush — Archive any pending point at end of stream
 *
 * Must be called after processing all input to ensure the last
 * segment is represented in the archive.
 * ------------------------------------------------------------------------- */

int ts_swinging_door_flush(ts_swinging_door_state_t *state,
                            ts_data_point_t *outputs,
                            size_t *num_output)
{
    if (!state || !outputs || !num_output) return -1;
    *num_output = 0;

    if (state->has_pending) {
        outputs[0] = state->pending_point;
        *num_output = 1;
        state->has_pending = false;

        /* Also flush the pending as the last archived if it hasn't been */
        state->last_archived = state->pending_point;
        state->stats.points_archived++;
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L2: Effective Compdev
 * ------------------------------------------------------------------------- */

double ts_swinging_door_effective_compdev(const ts_swinging_door_state_t *state)
{
    if (!state) return 0.0;
    return state->effective_compdev;
}

/* ---------------------------------------------------------------------------
 * L5: Door Width Computation
 *
 * Door width at time t is the value-space distance between the
 * upper and lower bounds:
 *
 *   y_upper(t) = y0 + slope_max_u * (t - t0)
 *   y_lower(t) = y0 + slope_min_l * (t - t0)
 *   width(t) = y_upper(t) - y_lower(t)
 *            = (slope_max_u - slope_min_l) * (t - t0)
 *
 * When width <= 0, the door has closed.
 * ------------------------------------------------------------------------- */

double ts_swinging_door_width(const ts_swinging_door_state_t *state,
                               int64_t t_us)
{
    if (!state) return 0.0;

    double dt = (double)(t_us - state->door.t0_us);
    if (dt <= 0.0) return 0.0;

    return (state->door.slope_max_u - state->door.slope_min_l) * dt;
}

bool ts_swinging_door_is_open(const ts_swinging_door_state_t *state)
{
    if (!state) return false;
    return (state->door.slope_max_u >= state->door.slope_min_l);
}

int ts_swinging_door_get_stats(const ts_swinging_door_state_t *state,
                                ts_compression_stats_t *stats)
{
    if (!state || !stats) return -1;

    *stats = state->stats;

    if (stats->points_archived > 0) {
        stats->compression_ratio = (double)stats->points_received
                                   / (double)stats->points_archived;
    } else {
        stats->compression_ratio = 1.0;
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L6: Reconstruction Error for Swinging Door
 *
 * The swinging door produces archived points that, when linearly
 * interpolated, reconstruct the original signal. This function
 * computes the RMSE of that piecewise-linear reconstruction.
 *
 * For each original point at t_i:
 *   1. Find the two bracketing archived points (a_j, a_{j+1}) with
 *      a_j.t <= t_i <= a_{j+1}.t
 *   2. Linearly interpolate: v_recon = lerp(a_j, a_{j+1}, t_i)
 *   3. error = v_original - v_recon
 *
 * Complexity: O(n + m) using two-pointer scan.
 * ------------------------------------------------------------------------- */

int ts_swinging_door_reconstruction_error(const ts_data_point_t *original,
                                            size_t num_original,
                                            const ts_data_point_t *archived,
                                            size_t num_archived,
                                            double *rmse,
                                            double *max_err)
{
    if (!original || !archived || !rmse || !max_err) return -1;
    if (num_original < 2 || num_archived < 2) return -1;

    *max_err = 0.0;
    double sum_sq = 0.0;
    size_t arch_idx = 0;

    for (size_t i = 0; i < num_original; i++) {
        int64_t t = original[i].epoch_us;

        /* Find bracket: archived[arch_idx].t <= t <= archived[arch_idx+1].t */
        while (arch_idx + 1 < num_archived
               && archived[arch_idx + 1].epoch_us <= t) {
            arch_idx++;
        }

        if (arch_idx + 1 >= num_archived) break;  /* Past last bracket */

        /* Linear interpolation */
        int64_t t0 = archived[arch_idx].epoch_us;
        int64_t t1 = archived[arch_idx + 1].epoch_us;
        double v0  = archived[arch_idx].value;
        double v1  = archived[arch_idx + 1].value;

        double dt = (double)(t1 - t0);
        if (dt <= 0.0) continue;

        double v_recon = v0 + (v1 - v0) * (double)(t - t0) / dt;
        double error = original[i].value - v_recon;
        double abs_err = fabs(error);

        sum_sq += error * error;
        if (abs_err > *max_err) *max_err = abs_err;
    }

    *rmse = sqrt(sum_sq / (double)num_original);
    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Auto-Tune compdev using MAD-based Noise Estimation
 *
 * The median absolute deviation (MAD) is a robust estimator of scale:
 *
 *   MAD = median(|delta_i - median(deltas)|)
 *   sigma_hat = 1.4826 * MAD  (consistency factor for Gaussian)
 *
 * compdev = k * sigma_hat
 *
 * where k is typically 2.0-3.0 (95-99% confidence that noise will
 * be within the corridor).
 *
 * Reference: Rousseeuw, P.J. & Croux, C. (1993).
 *            "Alternatives to the Median Absolute Deviation."
 *            JASA 88(424):1273-1283.
 * ------------------------------------------------------------------------- */

/* Comparison function for qsort */
static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

double ts_swinging_door_autotune_compdev(const ts_data_point_t *signal,
                                           size_t num_points,
                                           double k_factor)
{
    if (!signal || num_points < 3) return 1.0;

    /* Compute first differences (deltas) */
    size_t num_deltas = num_points - 1;
    double *deltas = (double *)malloc(num_deltas * sizeof(double));
    if (!deltas) return 1.0;

    for (size_t i = 0; i < num_deltas; i++) {
        deltas[i] = signal[i + 1].value - signal[i].value;
    }

    /* Sort deltas to find median */
    qsort(deltas, num_deltas, sizeof(double), cmp_double);

    /* Median of deltas */
    double median_delta;
    if (num_deltas % 2 == 0) {
        median_delta = (deltas[num_deltas/2 - 1] + deltas[num_deltas/2]) * 0.5;
    } else {
        median_delta = deltas[num_deltas/2];
    }

    /* Compute absolute deviations from median */
    double *abs_devs = (double *)malloc(num_deltas * sizeof(double));
    if (!abs_devs) { free(deltas); return 1.0; }

    for (size_t i = 0; i < num_deltas; i++) {
        abs_devs[i] = fabs(deltas[i] - median_delta);
    }

    /* Sort absolute deviations to find MAD */
    qsort(abs_devs, num_deltas, sizeof(double), cmp_double);

    double mad;
    if (num_deltas % 2 == 0) {
        mad = (abs_devs[num_deltas/2 - 1] + abs_devs[num_deltas/2]) * 0.5;
    } else {
        mad = abs_devs[num_deltas/2];
    }

    free(deltas);
    free(abs_devs);

    /* Scale MAD to get sigma estimate (Gaussian consistency) */
    double sigma_hat = 1.4826 * mad;

    /* Recommended compdev */
    return k_factor * sigma_hat;
}
