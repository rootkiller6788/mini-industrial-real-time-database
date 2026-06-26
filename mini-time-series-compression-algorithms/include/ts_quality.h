/**
 * @file ts_quality.h
 * @brief Time-Series Data Quality, Exception Filtering, and Interpolation
 *
 * Module: mini-time-series-compression-algorithms
 * Knowledge Coverage: L1 Definitions, L2 Concepts, L3 Engineering Structures
 *
 * In industrial historians, not all data points are equal. Quality flags
 * (OPC UA, ISA-88) indicate the trustworthiness of each measurement.
 * Exception filtering determines which quality states force immediate
 * archiving (bypassing compression). Interpolation strategies define
 * how values are reconstructed between archived points.
 *
 * OPC Quality Model (OPC UA Part 8, §6.4):
 *   - Good: Data is reliable and within specification
 *   - Uncertain: Data may be questionable (sensor drift, calibration overdue)
 *   - Bad: Data should not be used (sensor failure, communication loss)
 *
 * Reference: OPC UA Part 8: Data Access, §6.4 Quality
 *            ISA-88 Batch Control, Part 1: Models and Terminology
 *            IEC 62541 (OPC UA) Standard
 * Curriculum: MIT 6.302, Stanford ENGR205, RWTH Aachen, ISA/IEC
 */

#ifndef TS_QUALITY_H
#define TS_QUALITY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ts_deadband.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * L1: Quality Flag Definitions
 * ------------------------------------------------------------------------- */

/** OPC UA quality status bit meanings */
#define TS_QUALITY_GOOD_MASK         0xC0
#define TS_QUALITY_UNCERTAIN_MASK    0x40
#define TS_QUALITY_BAD_MASK          0x00
#define TS_QUALITY_LIMIT_MASK        0x03

/** Quality utility macros */
#define TS_QUALITY_IS_GOOD(q)       (((q) & 0xC0) == 0xC0)
#define TS_QUALITY_IS_UNCERTAIN(q)  (((q) & 0xC0) == 0x40)
#define TS_QUALITY_IS_BAD(q)        (((q) & 0xC0) == 0x00)

/* ---------------------------------------------------------------------------
 * L1: Interpolation Method Enumeration
 * ------------------------------------------------------------------------- */

typedef enum {
    TS_INTERP_STEP        = 0,  /* Zero-order hold: hold last value */
    TS_INTERP_LINEAR      = 1,  /* Linear interpolation between archived points */
    TS_INTERP_PCHIP       = 2,  /* Piecewise Cubic Hermite (shape-preserving) */
    TS_INTERP_PREVIOUS    = 3,  /* Previous good value */
    TS_INTERP_NEXT        = 4,  /* Next good value */
    TS_INTERP_NEAREST     = 5,  /* Nearest archived point */
    TS_INTERP_NONE        = 6   /* Return NaN for gaps */
} ts_interp_method_t;

/* ---------------------------------------------------------------------------
 * L1: Exception Filter Configuration
 * ------------------------------------------------------------------------- */

/**
 * @brief Exception filter configuration — determines which events
 *        bypass normal compression to be force-archived.
 */
typedef struct {
    bool archive_on_quality_change;    /* Archive when quality flag changes */
    bool archive_on_bad_quality;       /* Archive when quality becomes BAD */
    bool archive_on_good_quality;      /* Archive on return to GOOD quality */
    bool archive_on_step_change;       /* Archive when |delta| > step_threshold */
    double step_threshold;             /* Threshold for step detection */
    bool archive_on_rate_alarm;        /* Archive on |dx/dt| > rate_limit */
    double rate_limit;                 /* Rate-of-change alarm limit */
    bool archive_on_value_limit;       /* Archive on process limit violation */
    double value_lo;                   /* Low process limit */
    double value_hi;                   /* High process limit */
    bool archive_on_value_lolo;        /* Low-low alarm limit */
    double value_lolo;
    bool archive_on_value_hihi;        /* High-high alarm limit */
    double value_hihi;
} ts_exception_config_t;

/** Exception event types that bypass compression */
typedef enum {
    TS_EXCEPTION_NONE           = 0,
    TS_EXCEPTION_QUALITY_CHANGE  = 1,
    TS_EXCEPTION_STEP_CHANGE    = 2,
    TS_EXCEPTION_RATE_ALARM     = 3,
    TS_EXCEPTION_VALUE_LIMIT    = 4,
    TS_EXCEPTION_SNAPSHOT       = 5  /* Periodic forced archive */
} ts_exception_type_t;

/** Exception evaluation state — keeps track of previous values */
typedef struct {
    ts_exception_config_t config;
    uint8_t              prev_quality;
    double               prev_value;
    int64_t              prev_timestamp_us;
    bool                 initialized;
} ts_exception_state_t;

/* ---------------------------------------------------------------------------
 * L2: Quality and Exception API
 * ------------------------------------------------------------------------- */

const char* ts_quality_to_string(uint8_t quality);

bool ts_quality_is_valid(uint8_t quality);

/**
 * @brief Initialize exception filter state.
 */
int ts_exception_init(ts_exception_state_t *state,
                       const ts_exception_config_t *config);

/**
 * @brief Evaluate whether an incoming data point triggers an exception
 *        that should bypass compression.
 *
 * Checks quality changes, step changes, rate alarms, and value limits
 * against the configured thresholds.
 *
 * @param state    Active exception state
 * @param point    Incoming data point
 * @param exc_type Output: type of exception triggered (TS_EXCEPTION_NONE if none)
 * @return         true if point should be force-archived
 */
bool ts_exception_evaluate(ts_exception_state_t *state,
                            const ts_data_point_t *point,
                            ts_exception_type_t *exc_type);

/* ---------------------------------------------------------------------------
 * L2: Interpolation API
 * ------------------------------------------------------------------------- */

/**
 * @brief Interpolate a value at a query timestamp from archived points.
 *
 * Uses the specified interpolation method to estimate the value at
 * the query time. Archived points must be sorted by timestamp.
 *
 * @param archived       Archived (compressed) data points
 * @param num_archived   Number of archived points
 * @param query_epoch_us Query timestamp
 * @param method         Interpolation method
 * @return               Interpolated value, or NaN if not possible
 */
double ts_interpolate(const ts_data_point_t *archived,
                       size_t num_archived,
                       int64_t query_epoch_us,
                       ts_interp_method_t method);

/**
 * @brief Bulk interpolation at multiple query timestamps.
 *
 * @param archived       Archived data points (sorted by timestamp)
 * @param num_archived   Number of archived points
 * @param query_times    Array of query timestamps
 * @param num_queries    Number of queries
 * @param method         Interpolation method
 * @param results        Output: interpolated values
 * @return               0 on success
 */
int ts_interpolate_bulk(const ts_data_point_t *archived,
                         size_t num_archived,
                         const int64_t *query_times,
                         size_t num_queries,
                         ts_interp_method_t method,
                         double *results);

/**
 * @brief Linear interpolation between two data points.
 *
 * v(t) = v0 + (v1-v0) * (t-t0)/(t1-t0)
 *
 * Asserts t0 <= t <= t1 and t0 < t1 (no division by zero).
 *
 * @param t0, v0  Start point
 * @param t1, v1  End point
 * @param t       Query timestamp
 * @return        Interpolated value, NaN on error
 */
double ts_lerp(int64_t t0, double v0, int64_t t1, double v1, int64_t t);

/**
 * @brief Find the bracketing archived points for a query timestamp.
 *
 * Binary search for the interval [idx, idx+1] that contains query_t.
 *
 * @param archived     Sorted archived points
 * @param num_archived Number of points
 * @param query_t      Query timestamp
 * @param idx          Output: left bracket index (0..num_archived-2),
 *                     or -1 if query_t is before all points,
 *                     or num_archived-1 if after all points.
 * @return             0 on success
 */
int ts_find_bracket(const ts_data_point_t *archived,
                     size_t num_archived,
                     int64_t query_t,
                     int *idx);

/**
 * @brief Remove points with BAD quality from a time series.
 *
 * Filters an array of data points, keeping only those with GOOD
 * or UNCERTAIN quality. BAD quality points are excluded.
 *
 * @param input       Input data points
 * @param num_input   Number of input points
 * @param output      Output: filtered points (caller allocates)
 * @param num_output  Output: number of good points
 * @return            0 on success
 */
int ts_filter_bad_quality(const ts_data_point_t *input,
                           size_t num_input,
                           ts_data_point_t *output,
                           size_t *num_output);

/**
 * @brief Compute the fraction of time during which the quality was GOOD.
 *
 * Useful for Key Performance Indicators (KPIs) like data availability.
 *
 * @param points      Data points (sorted by timestamp)
 * @param num_points  Number of points
 * @param t_start     Analysis start time
 * @param t_end       Analysis end time
 * @return            Fraction of time with GOOD quality [0.0, 1.0]
 */
double ts_quality_good_fraction(const ts_data_point_t *points,
                                 size_t num_points,
                                 int64_t t_start,
                                 int64_t t_end);

#ifdef __cplusplus
}
#endif

#endif /* TS_QUALITY_H */
