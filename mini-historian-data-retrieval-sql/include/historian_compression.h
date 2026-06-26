#ifndef HISTORIAN_COMPRESSION_H
#define HISTORIAN_COMPRESSION_H

/**
 * @file    historian_compression.h
 * @brief   Time-series data compression algorithms for industrial historians.
 *
 * Industrial historians are measured by compression ratio - the ability
 * to store years of sub-second data in limited disk space. Compression
 * is mathematically lossy but bounded by a user-specified deviation.
 *
 * Knowledge Coverage:
 *   L1: Compression methods, deviation parameters
 *   L2: Compression ratio, archive reconstruction
 *   L3: Swinging door algorithm (Bristol 1990)
 *   L5: Deadband, boxcar/backfill, piecewise-linear fitting
 *   L7: OSIsoft PI exception/compression tuning
 *   L8: Wavelet-based compression concepts
 *
 * References:
 *   - Bristol, E.H. "Swinging Door Trending" (1990), ISA Transactions
 *   - OSIsoft PI Server Administration Guide (Compression Tuning)
 *   - Hale & Sellars "Historical Data Compression" (1981)
 */

#include "historian_model.h"
#include <stdint.h>
#include <stddef.h>

/*---------------------------------------------------------------------------
 * L1: Compression Methods
 *
 * Three fundamental approaches to historian compression:
 *
 * 1. SWINGING DOOR (Bristol 1990):
 *    Draws a parallelogram of width 2*deviation from the last stored point.
 *    As long as new points stay within the parallelogram, discard them.
 *    When a point falls outside, store the previous point and start a new
 *    parallelogram. This preserves the trend shape within deviation bounds.
 *
 * 2. DEADBAND:
 *    Store a new value only if it differs from the last stored value by
 *    more than a threshold. Simple but loses slope information.
 *
 * 3. BOXCAR / BACKFILL:
 *    Store values at a coarse interval, optionally backfilling with
 *    linear interpolation. Used for snapshot-based collection systems.
 *---------------------------------------------------------------------------*/

/** Supported compression methods. */
typedef enum {
    HISTORIAN_COMPRESSION_SWINGING_DOOR = 0,
    HISTORIAN_COMPRESSION_DEADBAND       = 1,
    HISTORIAN_COMPRESSION_BOXCAR         = 2,
    HISTORIAN_COMPRESSION_NONE           = 3
} historian_compression_method_t;

/**
 * @struct historian_compression_params_t
 * @brief  Parameters controlling the compression algorithm.
 *
 * The key concept: "Compression Deviation" (CompDev) is the maximum
 * allowed error between the actual value and the value reconstructed
 * from the compressed archive.
 *
 * For OSIsoft PI:
 *   CompDev = exception_deviation (default: 0.5% of span)
 *   CompMin  = exception_min_time_ms (default: 0)
 *   CompMax  = exception_max_time_ms (default: 28800 = 8 hours)
 */
typedef struct {
    historian_compression_method_t method;

    /* Swinging door parameters */
    double      comp_deviation;        /**< Compression deviation (half-width of door) */
    double      comp_min_time_ms;      /**< Minimum time between stored points */
    double      comp_max_time_ms;      /**< Maximum time between stored points */

    /* Deadband parameters */
    double      deadband_absolute;     /**< Absolute deadband threshold */
    double      deadband_percent;      /**< Percentage deadband (of span) */

    /* Boxcar parameters */
    int64_t     boxcar_interval_ms;    /**< Boxcar storage interval */

    /* Replay / backfill */
    int         enable_backfill;       /**< 1 = backfill missing values on read */
    int         backfill_interp_type;  /**< Interpolation type for backfill (0=step,1=linear) */
} historian_compression_params_t;

/*---------------------------------------------------------------------------
 * L3: Swinging Door State
 *
 * The swinging door algorithm maintains a running state:
 *   - Upper slope bound: the steepest line from the last stored point
 *                        that passes within +comp_deviation of all points since.
 *   - Lower slope bound: the shallowest line from the last stored point
 *                        that passes within -comp_deviation of all points since.
 *
 * As new data arrives, the bounds narrow. When upper < lower, the door
 * has closed and we must store a point.
 *---------------------------------------------------------------------------*/

typedef struct {
    double slope_upper;         /**< Upper compression slope bound */
    double slope_lower;         /**< Lower compression slope bound */
    int64_t last_stored_time_ms; /**< Timestamp of the last stored point */
    double last_stored_value;   /**< Value of the last stored point */
    int64_t pivot_time_ms;      /**< First point after last stored (the pivot) */
    double pivot_value;
    int     initialized;        /**< 1 if state has been initialized */
    double deviation;           /**< Compression deviation in use */
    double comp_min_time_ms;    /**< Minimum time between stored points */
    double comp_max_time_ms;    /**< Maximum time between stored points */
} historian_swinging_door_state_t;

/*---------------------------------------------------------------------------
 * Compression API
 *---------------------------------------------------------------------------*/

/**
 * Initialize compression parameters to sensible defaults.
 *
 * Default: swinging door with 0.5% deviation, 0 min time, 8h max time.
 */
void historian_compression_params_init(historian_compression_params_t *params);

/**
 * Initialize the swinging door state machine.
 *
 * Call this before processing a new stream of data points.
 */
void historian_swinging_door_init(historian_swinging_door_state_t *state,
                                    double deviation,
                                    double first_value, int64_t first_time_ms);

/**
 * Feed a data point through the swinging door compressor.
 *
 * @param state     Compressor state (updated in place).
 * @param value     New incoming value.
 * @param time_ms   New incoming timestamp in milliseconds.
 * @param force_out 1 = force output regardless of compression.
 * @param stored_points Output array for points that should be stored.
 * @param stored_cap Capacity of stored_points array.
 * @param stored_count Output: number of points stored.
 * @return 0 on success.
 *
 * This function implements the core Bristol (1990) swinging door
 * algorithm. For each incoming point, it updates the upper/lower
 * slope bounds of the parallelogram. When a point falls outside,
 * the previous point is emitted as a "stored" point.
 */
int historian_swinging_door_feed(historian_swinging_door_state_t *state,
                                   double value, int64_t time_ms,
                                   int force_out,
                                   historian_data_point_t *stored_points,
                                   size_t stored_cap, size_t *stored_count);

/**
 * Apply deadband compression to a buffer of data points in-place.
 *
 * Filters the buffer, keeping only points that differ by more than
 * the deadband from the previous kept point.
 *
 * @param points   Array of data points (modified in place: kept points moved to front).
 * @param count    Number of input points.
 * @param deadband Absolute deadband threshold.
 * @param new_count Output: number of points remaining after compression.
 * @return 0 on success.
 */
int historian_deadband_compress(historian_data_point_t *points,
                                 size_t count, double deadband,
                                 size_t *new_count);

/**
 * Apply boxcar compression: resample a dense time series to a coarser
 * fixed interval. Values are taken at the boxcar interval boundaries.
 *
 * @param input      Input data points (sorted by timestamp).
 * @param input_count Number of input points.
 * @param interval_ms Boxcar interval in milliseconds.
 * @param output     Output array for compressed points.
 * @param output_cap Capacity of output array.
 * @param output_count Output: number of compressed points.
 * @return 0 on success.
 */
int historian_boxcar_compress(const historian_data_point_t *input,
                               size_t input_count, int64_t interval_ms,
                               historian_data_point_t *output,
                               size_t output_cap, size_t *output_count);

/**
 * Compute the compression ratio of a data set.
 *
 * Compression Ratio = raw_byte_count / compressed_byte_count
 * >1.0 means effective compression.
 *
 * @param raw_count        Number of raw data points.
 * @param compressed_count Number of compressed data points.
 * @return Compression ratio (>= 1.0).
 */
double historian_compression_ratio(size_t raw_count, size_t compressed_count);

/**
 * Estimate the compression ratio given the signal characteristics.
 *
 * @param stddev         Signal standard deviation.
 * @param comp_deviation Compression deviation parameter.
 * @return Estimated compression ratio.
 *
 * Heuristic from Bristol (1990): higher stddev/deviation ratio yields
 * lower compression (more points stored).
 */
double historian_estimate_compression(double stddev, double comp_deviation);

/**
 * Reconstruct (interpolate) a value at an arbitrary timestamp from
 * compressed archive data, using backfill/step/linear interpolation.
 *
 * @param compressed    Array of stored (compressed) data points sorted by time.
 * @param comp_count    Number of compressed points.
 * @param query_time_ms Timestamp to reconstruct at.
 * @param interp_type   0 = step (last known value), 1 = linear.
 * @param value_out     Output: reconstructed value.
 * @return 0 on success, -1 if query_time is outside data range.
 */
int historian_reconstruct_value(const historian_data_point_t *compressed,
                                 size_t comp_count, int64_t query_time_ms,
                                 int interp_type, double *value_out);

#endif /* HISTORIAN_COMPRESSION_H */
