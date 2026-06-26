/**
 * @file ts_deadband.h
 * @brief Time-Series Deadband Compression — Threshold-Based Data Reduction
 *
 * Module: mini-time-series-compression-algorithms
 * Knowledge Coverage: L1 Definitions, L2 Core Concepts, L4 Engineering Laws
 *
 * Deadband compression is the most fundamental technique in industrial
 * historian systems (OSIsoft PI, Honeywell PHD). It discards data points
 * whose change from the last archived value is below a configurable
 * threshold, thereby reducing storage while preserving signal fidelity
 * within tolerance bounds.
 *
 * Types of Deadband:
 *   1. Absolute Deadband: |x_new - x_archived| < epsilon
 *   2. Percent Deadband:  |x_new - x_archived| < pct * span
 *   3. Time-Based Deadband: Minimum time interval between archived points
 *   4. Rate-of-Change Deadband: |dx/dt| < threshold
 *
 * Reference: OSIsoft PI Server Administration Guide (Compression), 2022
 * Curriculum: MIT 6.302 (Sampling Theory), Stanford ENGR205,
 *             RWTH Aachen Industrial Data Management
 */

#ifndef TS_DEADBAND_H
#define TS_DEADBAND_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * L1: Core Type Definitions — Time-Series Data Point
 * ------------------------------------------------------------------------- */

/** OPC UA / ISA-88 style quality flags for industrial time-series data */
typedef enum {
    TS_QUALITY_GOOD           = 0xC0,
    TS_QUALITY_GOOD_LOCAL     = 0xD8,
    TS_QUALITY_UNCERTAIN      = 0x40,
    TS_QUALITY_UNCERTAIN_LAST = 0x58,
    TS_QUALITY_UNCERTAIN_SUB  = 0x50,
    TS_QUALITY_BAD            = 0x00,
    TS_QUALITY_BAD_CONFIG     = 0x10,
    TS_QUALITY_BAD_COMM       = 0x18,
    TS_QUALITY_BAD_DEVICE     = 0x1C
} ts_quality_t;

/**
 * @brief Single time-series data point — the atomic unit of all
 *        industrial historian databases.
 */
typedef struct {
    int64_t  epoch_us;
    double   value;
    uint8_t  quality;
    uint8_t  reserved[3];
} ts_data_point_t;

/** All deadband filter modes */
typedef enum {
    TS_DEADBAND_NONE           = 0,
    TS_DEADBAND_ABSOLUTE       = 1,
    TS_DEADBAND_PERCENT        = 2,
    TS_DEADBAND_TIME           = 3,
    TS_DEADBAND_COMBINED       = 4,
    TS_DEADBAND_RATE_OF_CHANGE  = 5,
    TS_DEADBAND_ADAPTIVE       = 6
} ts_deadband_mode_t;

/** Deadband filter configuration */
typedef struct {
    ts_deadband_mode_t mode;
    double threshold_abs;
    double threshold_pct;
    double span_min;
    double span_max;
    int64_t threshold_time_us;
    double rate_threshold;
    bool   enable_exception;
    bool   enable_snapshot;
    double adaptive_learning_rate;
} ts_deadband_config_t;

/** Compression performance metrics */
typedef struct {
    uint64_t points_received;
    uint64_t points_archived;
    uint64_t points_discarded;
    double   compression_ratio;
    double   max_error_abs;
    double   sum_sq_error;
    double   rmse;
    double   bytes_saved_pct;
    int64_t  elapsed_us;
} ts_compression_stats_t;

/** Runtime state for the deadband filter */
typedef struct {
    ts_deadband_config_t   config;
    ts_data_point_t        last_archived;
    ts_data_point_t        last_received;
    int64_t                last_archive_ts;
    ts_compression_stats_t stats;
    bool                   initialized;
    uint32_t               consecutive_discards;
} ts_deadband_state_t;

/* ---------------------------------------------------------------------------
 * L2: Deadband Filter Core API
 * ------------------------------------------------------------------------- */

int ts_deadband_init(ts_deadband_state_t *state,
                      const ts_deadband_config_t *config);

int ts_deadband_reset(ts_deadband_state_t *state);

/**
 * @brief Process a single time-series data point through the deadband filter.
 *
 * For ABSOLUTE mode:  archived = (|input.value - last.value| >= epsilon)
 * For PERCENT mode:   archived = (|delta| / span >= pct/100)
 * For TIME mode:      archived = (dt >= threshold_time_us)
 * For COMBINED mode:  archived = absolute_check AND time_check
 * For RATE mode:      archived = (|delta/dt| >= rate_threshold)
 *
 * Complexity: O(1)
 */
int ts_deadband_filter(ts_deadband_state_t *state,
                        const ts_data_point_t *input,
                        bool *archived);

int ts_deadband_filter_batch(ts_deadband_state_t *state,
                              const ts_data_point_t *inputs,
                              size_t num_input,
                              ts_data_point_t *outputs,
                              size_t *num_output);

double ts_deadband_effective_threshold(const ts_deadband_state_t *state,
                                         double value);

bool ts_deadband_would_archive(const ts_deadband_state_t *state,
                                const ts_data_point_t *input);

int ts_deadband_get_stats(const ts_deadband_state_t *state,
                           ts_compression_stats_t *stats);

/**
 * @brief Estimate theoretical compression ratio.
 *
 * For absolute deadband epsilon and Gaussian increments N(0, sigma^2):
 *   P(archive) = 2*Phi(-epsilon/sigma)
 *   Expected compression ratio = 1 / P(archive)
 */
double ts_deadband_estimate_compression(double threshold,
                                          double sigma_increment);

int ts_deadband_reconstruction_error(const ts_data_point_t *original,
                                       size_t num_original,
                                       const ts_data_point_t *archived,
                                       size_t num_archived,
                                       double *rmse,
                                       double *max_error);

int ts_deadband_set_config(ts_deadband_state_t *state,
                            const ts_deadband_config_t *config,
                            bool reset_stats);

uint8_t ts_deadband_quality_from_staleness(int64_t time_since_archive_us,
                                             double epsilon,
                                             int64_t max_staleness_us);

#ifdef __cplusplus
}
#endif

#endif /* TS_DEADBAND_H */
