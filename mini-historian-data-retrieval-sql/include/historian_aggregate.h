#ifndef HISTORIAN_AGGREGATE_H
#define HISTORIAN_AGGREGATE_H

/**
 * @file    historian_aggregate.h
 * @brief   Time-series aggregate computation for industrial historian data.
 *
 * Implements statistical aggregates over time-bucketed data: average,
 * min, max, sum, count, time-weighted average, standard deviation,
 * variance, range, percentile, and duration-weighted metrics.
 *
 * Knowledge Coverage:
 *   L1: Aggregate type definitions, bucket specification
 *   L2: Time-weighted vs. arithmetic aggregation
 *   L4: ISO 22400 KPI aggregation rules
 *   L5: Welford online stddev, duration-weighted averages
 *   L7: OSIsoft PI Performance Equation (PE) function equivalents
 *
 * References:
 *   - ISO 22400-2:2014 Automation systems - KPIs for MES
 *   - Welford, B.P. "Note on a Method for Calculating Corrected Sums" (1962)
 *   - OSIsoft PI Performance Equations Reference
 */

#include "historian_model.h"
#include <stdint.h>
#include <stddef.h>

/*---------------------------------------------------------------------------
 * L1: Aggregate Types
 *
 * Industrial historians offer a standard set of aggregate functions,
 * analogous to SQL GROUP BY aggregates but optimized for time-series.
 * PI Data Archive supports 30+ aggregate types; we implement the core set.
 *---------------------------------------------------------------------------*/

/** Supported aggregate function types. */
typedef enum {
    HISTORIAN_AGG_AVERAGE         = 0,   /**< Arithmetic mean (PI: Average) */
    HISTORIAN_AGG_TIME_AVERAGE    = 1,   /**< Time-weighted average (PI: TimeAvg) */
    HISTORIAN_AGG_MINIMUM         = 2,   /**< Minimum value (PI: Minimum) */
    HISTORIAN_AGG_MAXIMUM         = 3,   /**< Maximum value (PI: Maximum) */
    HISTORIAN_AGG_RANGE           = 4,   /**< Max - Min (PI: Range) */
    HISTORIAN_AGG_SUM             = 5,   /**< Sum of values (PI: Total) */
    HISTORIAN_AGG_COUNT           = 6,   /**< Count of data points (PI: Count) */
    HISTORIAN_AGG_STDDEV          = 7,   /**< Population standard deviation (PI: StdDev) */
    HISTORIAN_AGG_STDDEV_SAMPLE   = 8,   /**< Sample standard deviation */
    HISTORIAN_AGG_VARIANCE        = 9,   /**< Population variance */
    HISTORIAN_AGG_VARIANCE_SAMPLE = 10,  /**< Sample variance */
    HISTORIAN_AGG_PERCENTILE      = 11,  /**< Nth percentile (PI: PctGood) */
    HISTORIAN_AGG_MEDIAN          = 12,  /**< 50th percentile */
    HISTORIAN_AGG_DURATION_GOOD   = 13,  /**< Time duration of good quality data */
    HISTORIAN_AGG_DURATION_BAD    = 14,  /**< Time duration of bad quality data */
    HISTORIAN_AGG_FIRST           = 15,  /**< First value in bucket */
    HISTORIAN_AGG_LAST            = 16,  /**< Last value in bucket */
    HISTORIAN_AGG_DELTA           = 17,  /**< Last - First (net change) */
    HISTORIAN_AGG_RATE            = 18,  /**< (Last-First)/Duration (rate of change) */
    HISTORIAN_AGG_INTEGRAL        = 19   /**< Time integral (area under curve) */
} historian_aggregate_type_t;

/*---------------------------------------------------------------------------
 * L4: ISO 22400-2 KPI Time Bucket Conventions
 *
 * ISO 22400 defines standard time bucket periods for KPI calculation:
 *   - Shift: 8 or 12 hours
 *   - Day:   24 hours (calendar day or rolling 24h)
 *   - Week:  7 days (ISO week or calendar week)
 *   - Month: Calendar month
 *   - Quarter: Calendar quarter
 *   - Year:  Calendar year
 *
 * Each bucket has specific boundary rules (inclusive/exclusive, timezone-aware).
 *---------------------------------------------------------------------------*/

/** Standard time bucket periods per ISO 22400-2. */
typedef enum {
    HISTORIAN_BUCKET_SECOND    = 0,
    HISTORIAN_BUCKET_MINUTE    = 1,
    HISTORIAN_BUCKET_HOUR      = 2,
    HISTORIAN_BUCKET_SHIFT     = 3,
    HISTORIAN_BUCKET_DAY       = 4,
    HISTORIAN_BUCKET_WEEK      = 5,
    HISTORIAN_BUCKET_MONTH     = 6,
    HISTORIAN_BUCKET_QUARTER   = 7,
    HISTORIAN_BUCKET_YEAR      = 8,
    HISTORIAN_BUCKET_CUSTOM_MS = 9    /**< Custom interval in milliseconds */
} historian_bucket_period_t;

/** Bucket alignment mode (where does the first bucket start?). */
typedef enum {
    HISTORIAN_ALIGN_QUERY_START    = 0,  /**< First bucket starts at query start */
    HISTORIAN_ALIGN_NATURAL        = 1,  /**< Natural calendar alignment (midnight, Mon) */
    HISTORIAN_ALIGN_PRODUCTION_DAY = 2,  /**< Production day start (e.g., 06:00) */
    HISTORIAN_ALIGN_CUSTOM         = 3   /**< Custom alignment offset */
} historian_bucket_alignment_t;

/**
 * @struct historian_bucket_spec_t
 * @brief  Specification for time-bucketed aggregation.
 */
typedef struct {
    historian_bucket_period_t    period;       /**< Bucket period type */
    int64_t                      custom_ms;    /**< Custom period in ms (for CUSTOM_MS) */
    historian_bucket_alignment_t alignment;    /**< First bucket alignment */
    int64_t                      align_offset_ms; /**< Offset from natural boundary */
    int                          exclude_partial; /**< 1 = exclude incomplete buckets */
} historian_bucket_spec_t;

/*---------------------------------------------------------------------------
 * L5: Aggregate Specification
 *---------------------------------------------------------------------------*/

/**
 * @struct historian_aggregate_spec_t
 * @brief  Complete specification for an aggregation operation.
 */
typedef struct {
    historian_aggregate_type_t  agg_type;      /**< Aggregate function */
    historian_bucket_spec_t     bucket;        /**< Time bucketing */
    historian_time_range_t      time_range;    /**< Aggregation time window */
    int32_t                     tag_id;        /**< Tag to aggregate */
    double                      percentile;    /**< Percentile value (0-100) for PERCENTILE */
    int                         quality_aware; /**< 1 = exclude bad quality from calc */
    double                      double_val;    /**< Generic double parameter */
} historian_aggregate_spec_t;

/*---------------------------------------------------------------------------
 * L8: Advanced aggregate result with metadata
 *---------------------------------------------------------------------------*/

/**
 * @struct historian_bucket_result_t
 * @brief  Result for a single time bucket aggregation.
 */
typedef struct {
    historian_timestamp_t bucket_start;
    historian_timestamp_t bucket_end;
    double                agg_value;
    size_t                sample_count;    /**< Number of raw samples in bucket */
    double                percent_good;    /**< Percentage of good-quality samples */
    int                   is_partial;      /**< 1 if bucket is incomplete */
} historian_bucket_result_t;

/**
 * @brief Array of bucket results from an aggregation query.
 */
typedef struct {
    historian_bucket_result_t *buckets;
    size_t                     count;
    size_t                     capacity;
} historian_bucket_result_set_t;

/*---------------------------------------------------------------------------
 * Core Aggregation API
 *---------------------------------------------------------------------------*/

/**
 * Initialize an aggregate specification to safe defaults.
 */
void historian_aggregate_spec_init(historian_aggregate_spec_t *spec);

/**
 * Initialize a bucket specification to safe defaults.
 */
void historian_bucket_spec_init(historian_bucket_spec_t *spec);

/**
 * Compute a single aggregate value over a set of data points.
 *
 * @param agg_type  Type of aggregate to compute.
 * @param points    Array of data points (must be sorted by timestamp).
 * @param count     Number of points in array.
 * @param result    Output: the computed aggregate value.
 * @return 0 on success, negative on error (e.g., empty array for average).
 *
 * Knowledge: Implements all 20 aggregate functions with proper edge cases.
 */
int historian_compute_aggregate(historian_aggregate_type_t agg_type,
                                 const historian_data_point_t *points,
                                 size_t count, double *result);

/**
 * Compute time-bucketed aggregates.
 *
 * Divides the time range into buckets per the spec, then computes
 * the specified aggregate for each bucket.
 *
 * @param spec    Bucket and aggregate specification.
 * @param points  Raw data points (must be sorted by timestamp).
 * @param count   Number of raw data points.
 * @param results Output bucket results (caller must init).
 * @return 0 on success, negative on error.
 *
 * Knowledge: ISO 22400-2 time-bucketing algorithm.
 */
int historian_compute_bucketed_aggregate(const historian_aggregate_spec_t *spec,
                                          const historian_data_point_t *points,
                                          size_t count,
                                          historian_bucket_result_set_t *results);

/**
 * Compute the time-weighted average of a time series.
 * Uses trapezoidal integration: each interval contributes
 * avg(value_i, value_{i+1}) * (t_{i+1} - t_i).
 *
 * Formula: TWAvg = SUM( (v_i + v_{i+1})/2 * dt_i ) / total_duration
 *
 * @param points  Array of data points sorted by timestamp.
 * @param count   Number of points.
 * @param result  Output: time-weighted average value.
 * @return 0 on success.
 *
 * Knowledge: PI System TimeAvg function implementation.
 * Reference: OSIsoft PI Performance Equations Reference (2018), Chapter 4.
 */
int historian_compute_time_weighted_avg(const historian_data_point_t *points,
                                         size_t count, double *result);

/**
 * Compute a running aggregate efficiently using online algorithms.
 *
 * Maintains aggregate state incrementally, enabling O(1) per-point
 * updates instead of O(n) recomputation. Uses Welford's algorithm
 * for mean and variance.
 *
 * @param agg_type    Type of running aggregate.
 * @param new_point   New data point to incorporate.
 * @param state       Opaque state pointer (caller-managed, 256 bytes).
 *                    Initialize to zeros before first call.
 * @return 0 on success, negative if type unsupported for online mode.
 *
 * Knowledge: Welford's online variance algorithm (1962).
 */
int historian_running_aggregate_update(historian_aggregate_type_t agg_type,
                                        const historian_data_point_t *new_point,
                                        void *state);

/**
 * Finalize a running aggregate and retrieve the result.
 * After this call, the state is reset for the next sequence.
 *
 * @param state  Running aggregate state from historian_running_aggregate_update.
 * @param result Output: the final aggregate value.
 * @return 0 on success.
 */
int historian_running_aggregate_finalize(historian_aggregate_type_t agg_type,
                                          const void *state, double *result);

/*---------------------------------------------------------------------------
 * L8: Distribution Statistics
 *---------------------------------------------------------------------------*/

/**
 * Compute full distribution statistics over a time series.
 *
 * @param points  Array of data points.
 * @param count   Number of points.
 * @param stats   Output: computed distribution statistics.
 * @return 0 on success.
 *
 * Knowledge: Moment-based statistics (skewness, kurtosis).
 * Skewness: E[(X-mu)^3] / sigma^3
 * Kurtosis: E[(X-mu)^4] / sigma^4 - 3 (excess kurtosis)
 */
int historian_compute_distribution_stats(const historian_data_point_t *points,
                                          size_t count,
                                          historian_distribution_stats_t *stats);

/**
 * Initialize a bucket result set to empty.
 */
void historian_bucket_result_set_init(historian_bucket_result_set_t *brs);

/**
 * Append a bucket result. Amortized O(1).
 */
int historian_bucket_result_set_append(historian_bucket_result_set_t *brs,
                                        historian_bucket_result_t result);

/**
 * Free memory used by a bucket result set.
 */
void historian_bucket_result_set_destroy(historian_bucket_result_set_t *brs);

#endif /* HISTORIAN_AGGREGATE_H */
