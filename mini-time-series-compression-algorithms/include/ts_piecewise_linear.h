/**
 * @file ts_piecewise_linear.h
 * @brief Piecewise Linear Approximation (PLA) for Time-Series Compression
 *
 * Module: mini-time-series-compression-algorithms
 * Knowledge Coverage: L1 Definitions, L2 Concepts, L5 Algorithms
 *
 * PLA represents a time series as a sequence of connected line segments.
 * Each segment is defined by its start and end points; intermediate
 * points are reconstructed via linear interpolation.
 *
 * Segmentation Strategies:
 *   1. Sliding Window (SW): Grow segment until error exceeds tolerance
 *   2. Top-Down (TD): Recursively split at point of maximum error
 *   3. Bottom-Up (BU): Merge adjacent segments with least error increase
 *   4. SWAB: Sliding Window And Bottom-up hybrid (Keogh et al., 2001)
 *
 * Reference: Keogh, E. et al. (2001). ICDM 2001, pp. 289-296.
 *            Palpanas, T. et al. (2004). ICDE 2004.
 * Curriculum: Stanford AA272, CMU 24-677, Berkeley ME233
 */

#ifndef TS_PIECEWISE_LINEAR_H
#define TS_PIECEWISE_LINEAR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ts_deadband.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * L1: PLA Segment Representation
 * ------------------------------------------------------------------------- */

typedef struct {
    int64_t  t_start;
    int64_t  t_end;
    double   v_start;
    double   v_end;
    double   slope;
    double   max_error;
    uint32_t num_points;
} ts_pla_segment_t;

typedef struct {
    ts_pla_segment_t *segments;
    size_t    num_segments;
    size_t    capacity;
    double    error_tolerance;
    double    max_segment_error;
    double    rmse;
    uint64_t  original_points;
    uint64_t  compressed_points;
    double    compression_ratio;
} ts_pla_model_t;

typedef enum {
    TS_PLA_SLIDING_WINDOW = 0,
    TS_PLA_TOP_DOWN       = 1,
    TS_PLA_BOTTOM_UP      = 2,
    TS_PLA_SWAB           = 3
} ts_pla_algorithm_t;

typedef struct {
    ts_pla_algorithm_t algorithm;
    double   error_tolerance;
    uint32_t min_segment_points;
    uint32_t max_segment_points;
    int64_t  max_segment_duration_us;
    bool     adaptive_tolerance;
    double   adaptive_factor;
} ts_pla_config_t;

/* ---------------------------------------------------------------------------
 * L2: PLA Core API
 * ------------------------------------------------------------------------- */

int ts_pla_model_init(ts_pla_model_t *model, size_t capacity);
void ts_pla_model_free(ts_pla_model_t *model);

/**
 * @brief Compute PLA using sliding window algorithm.
 *
 * Algorithm (L5): Grow a segment from point i, adding points j=i+1,i+2,...
 * until the max error of the line from i to j exceeds tolerance.
 * Then close segment at j-1 and start new segment.
 *
 * Complexity: O(n*L) where L is average segment length.
 */
int ts_pla_sliding_window(ts_pla_model_t *model,
                           const ts_data_point_t *points,
                           size_t n,
                           const ts_pla_config_t *config);

/**
 * @brief Compute PLA using top-down (Douglas-Peucker) algorithm.
 *
 * Algorithm (L5): Start with segment [0, n-1], find point k with maximum
 * distance from line, if distance > tolerance: recursively split.
 *
 * Complexity: O(n^2) worst-case, O(n log n) average.
 */
int ts_pla_top_down(ts_pla_model_t *model,
                     const ts_data_point_t *points,
                     size_t n,
                     const ts_pla_config_t *config);

/**
 * @brief Compute PLA using bottom-up merge algorithm.
 *
 * Algorithm (L5): Create fine initial segmentation, then greedily merge
 * adjacent segments with least merge cost.
 *
 * Complexity: O(n log n) using priority queue.
 */
int ts_pla_bottom_up(ts_pla_model_t *model,
                      const ts_data_point_t *points,
                      size_t n,
                      const ts_pla_config_t *config);

/**
 * @brief Compute PLA using SWAB hybrid algorithm.
 *
 * Keogh et al. (2001): Combine sliding window for online processing
 * with bottom-up merging for buffer refinement.
 *
 * Complexity: O(n*B) bounded by buffer size B.
 */
int ts_pla_swab(ts_pla_model_t *model,
                 const ts_data_point_t *points,
                 size_t n,
                 const ts_pla_config_t *config,
                 size_t buffer_size);

int ts_pla_compute(ts_pla_model_t *model,
                    const ts_data_point_t *points,
                    size_t n,
                    const ts_pla_config_t *config);

/* ---------------------------------------------------------------------------
 * L2: PLA Reconstruction and Error Analysis
 * ------------------------------------------------------------------------- */

int ts_pla_reconstruct(const ts_pla_model_t *model,
                        const int64_t *timestamps,
                        size_t n,
                        double *values);

/**
 * @brief Perpendicular distance from a point to a line segment.
 *
 * Distance = |(x1-x0)*(y0-yp) - (x0-xp)*(y1-y0)| / sqrt((x1-x0)^2 + (y1-y0)^2)
 */
double ts_pla_point_segment_distance(int64_t x0, double y0,
                                       int64_t x1, double y1,
                                       int64_t xp, double yp);

int ts_pla_compute_error(ts_pla_model_t *model,
                          const ts_data_point_t *original,
                          size_t n);

/**
 * @brief Find optimal segment count using MDL principle.
 *
 * MDL_cost(K) = K*log2(n)*c_param + n*log2(RMSE(K))
 *
 * Reference: Rissanen, J. (1978). Automatica 14:465-471.
 */
double ts_pla_mdl_optimal_segments(const ts_data_point_t *points,
                                     size_t n,
                                     size_t max_segments,
                                     double c_param,
                                     size_t *optimal_k);

int ts_pla_compress(const ts_data_point_t *points, size_t n,
                     const ts_pla_config_t *config,
                     ts_data_point_t **compressed,
                     size_t *num_compressed);

#ifdef __cplusplus
}
#endif

#endif /* TS_PIECEWISE_LINEAR_H */
