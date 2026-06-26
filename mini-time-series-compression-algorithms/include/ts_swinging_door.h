/**
 * @file ts_swinging_door.h
 * @brief Swinging Door / Boxcar Trending Algorithm — OSIsoft PI Core
 *
 * Module: mini-time-series-compression-algorithms
 * Knowledge Coverage: L1 Definitions, L2 Core Concepts, L5 Algorithms
 *
 * The Swinging Door algorithm (also known as Boxcar/Back-Slope) is the
 * signature compression algorithm of the OSIsoft PI System. It was
 * originally developed by Bristol and refined for PI.
 *
 * Principle:
 *   Starting from the last archived point, draw a "door" that swings
 *   outward. Each new incoming point causes the door to narrow. When a
 *   point falls outside the door, the immediately preceding point is
 *   archived and a new door is opened.
 *
 * The parallelogram is defined by:
 *   Upper bound at time t: y_archived + max_u_slope * (t - t_archived) + compdev
 *   Lower bound at time t: y_archived + min_l_slope * (t - t_archived) - compdev
 *
 * Reference: J. Bristol, "Swinging Door Trending", U.S. Patent 4,669,097
 *            OSIsoft PI Server Reference Guide, Chapter 4: Compression
 * Curriculum: MIT 2.171, Stanford ENGR205, Purdue ME 575
 */

#ifndef TS_SWINGING_DOOR_H
#define TS_SWINGING_DOOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ts_deadband.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * L1: Swinging Door Configuration
 * ------------------------------------------------------------------------- */

/** Swinging door compression parameters */
typedef struct {
    double   compdev;                 /* Absolute compression deviation */
    double   compdev_percent;         /* Percent deviation (alternative) */
    double   span_min;                /* Engineering unit span min */
    double   span_max;                /* Engineering unit span max */
    int64_t  compmax_us;              /* Maximum time between archives */
    int64_t  compmin_us;              /* Minimum time between archives */
    bool     use_percent;             /* If true, use compdev_percent */
    bool     force_on_step;           /* Archive on detected step changes */
    double   step_detection_threshold;
} ts_swinging_door_config_t;

/* ---------------------------------------------------------------------------
 * L1: Swinging Door Runtime State
 * ------------------------------------------------------------------------- */

/** Pivot slopes defining the parallelogram corridor */
typedef struct {
    double   slope_max_u;             /* Current maximum of upper bounds */
    double   slope_min_l;             /* Current minimum of lower bounds */
    int64_t  t0_us;                   /* t0 of last archived point */
    double   y0;                      /* y0 of last archived point */
    double   y0_upper;                /* y0 + compdev (upper origin) */
    double   y0_lower;                /* y0 - compdev (lower origin) */
    int64_t  door_open_time_us;       /* When current door was opened */
    uint32_t points_since_open;       /* Count of points seen since door opened */
} ts_swinging_door_t;

/** Complete swinging door compressor state */
typedef struct {
    ts_swinging_door_config_t config;
    ts_swinging_door_t       door;
    ts_data_point_t          last_archived;
    ts_data_point_t          pending_point;
    double                   effective_compdev;
    bool                     initialized;
    bool                     has_pending;
    ts_compression_stats_t   stats;
} ts_swinging_door_state_t;

/* ---------------------------------------------------------------------------
 * L2: API — Swinging Door Compression
 * ------------------------------------------------------------------------- */

int ts_swinging_door_init(ts_swinging_door_state_t *state,
                           const ts_swinging_door_config_t *config);

int ts_swinging_door_reset(ts_swinging_door_state_t *state);

/**
 * @brief Feed one data point through the swinging door algorithm.
 *
 * Algorithm (Bristol, 1987):
 *   1. If not initialized: archive this point as starting anchor
 *   2. Compute upper and lower bounds at time t with compdev offset
 *   3. Update slope_max_u = min(slope_max_u, upper_slope)
 *   4. Update slope_min_l = max(slope_min_l, lower_slope)
 *   5. If slope_max_u < slope_min_l: door closed!
 *      Archive the PREVIOUS pending point
 *      Open new door from that point
 *   6. If compmax exceeded: force archive
 *
 * Complexity: O(1) per point
 */
int ts_swinging_door_filter(ts_swinging_door_state_t *state,
                             const ts_data_point_t *input,
                             bool *archived,
                             ts_data_point_t *output);

int ts_swinging_door_filter_batch(ts_swinging_door_state_t *state,
                                   const ts_data_point_t *inputs,
                                   size_t num_input,
                                   ts_data_point_t *outputs,
                                   size_t *num_output);

int ts_swinging_door_flush(ts_swinging_door_state_t *state,
                            ts_data_point_t *outputs,
                            size_t *num_output);

double ts_swinging_door_effective_compdev(const ts_swinging_door_state_t *state);

/**
 * @brief Compute the current door width in value-space at timestamp t.
 *
 * Door width = (slope_max_u - slope_min_l) * (t - t0)
 * When width <= 0, the door has closed.
 */
double ts_swinging_door_width(const ts_swinging_door_state_t *state,
                               int64_t t_us);

bool ts_swinging_door_is_open(const ts_swinging_door_state_t *state);

int ts_swinging_door_get_stats(const ts_swinging_door_state_t *state,
                                ts_compression_stats_t *stats);

int ts_swinging_door_reconstruction_error(const ts_data_point_t *original,
                                            size_t num_original,
                                            const ts_data_point_t *archived,
                                            size_t num_archived,
                                            double *rmse,
                                            double *max_err);

/**
 * @brief Auto-tune compdev using MAD-based noise estimation.
 *
 * compdev_recommended = k * sigma_noise
 * sigma_noise = 1.4826 * MAD(delta_signal)
 *
 * Reference: Rousseeuw & Croux (1993), JASA 88(424):1273-1283.
 */
double ts_swinging_door_autotune_compdev(const ts_data_point_t *signal,
                                           size_t num_points,
                                           double k_factor);

#ifdef __cplusplus
}
#endif

#endif /* TS_SWINGING_DOOR_H */
