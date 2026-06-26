/**
 * @file include/pi_af_analytics_kpi.h
 * @brief PI AF Analytics — KPI Calculation & Asset Rollup Engine
 *
 * Implements the Key Performance Indicator (KPI) calculation framework
 * used in industrial operations management. KPIs are derived metrics
 * that quantify business/operational performance against targets.
 *
 * This module handles:
 *   - KPI definition (target, thresholds, direction)
 *   - KPI status evaluation (single-value vs. time-series KPIs)
 *   - Rollup aggregation through asset hierarchy
 *   - KPI trend detection (deterioration, improvement)
 *   - Composite KPI scoring (weighted multi-indicator)
 *
 * Knowledge Coverage: L1 (KPI Definitions), L2 (Threshold Evaluation),
 *                     L5 (Rollup Algorithms), L7 (Industrial KPI Applications)
 *
 * Reference Standards:
 *   - ISO 22400-1:2014 — Automation systems and integration — KPIs for MOM,
 *     Part 1: Overview, concepts and terminology
 *   - ISO 22400-2:2014 — Part 2: Definitions and descriptions
 *   - ISA-95 Part 3 — Activity models of manufacturing operations management
 *   - OPC UA Part 13 — Aggregates (for KPI computation patterns)
 *
 * Purdue ME 575 — Industrial KPI dashboards and scorecards
 * Stanford ENGR205 — Process performance monitoring
 * RWTH Aachen — KPI-driven manufacturing execution systems
 */

#ifndef PI_AF_ANALYTICS_KPI_H
#define PI_AF_ANALYTICS_KPI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "pi_af_analytics_core.h"
#include "pi_af_analytics_timeseries.h"

/* --------------------------------------------------------------------------
 * L1 — Definitions: KPI Domain Model
 * ------------------------------------------------------------------------*/

/** Maximum KPI name length */
#define PI_AF_MAX_KPI_NAME      256

/** Maximum thresholds per KPI */
#define PI_AF_MAX_KPI_THRESHOLDS 8

/** Maximum child KPIs for rollup */
#define PI_AF_MAX_KPI_CHILDREN  64

/**
 * @brief KPI evaluation direction.
 *
 * Determines whether higher-is-better or lower-is-better for
 * threshold comparison. Critical for multi-KPI dashboards.
 *
 * Example: OEE = higher-is-better; Energy intensity = lower-is-better.
 */
typedef enum {
    PI_AF_KPI_DIR_HIGHER_BETTER = 0,  /**< Larger values are better */
    PI_AF_KPI_DIR_LOWER_BETTER  = 1,  /**< Smaller values are better */
    PI_AF_KPI_DIR_TARGET_ZONE   = 2,  /**< Good = within [target_min, target_max] */
} pi_af_kpi_direction_t;

/**
 * @brief KPI health status.
 *
 * Classic traffic-light dashboard indicators.
 */
typedef enum {
    PI_AF_KPI_STATUS_GOOD     = 0,  /**< Green — within acceptable limits */
    PI_AF_KPI_STATUS_WARNING  = 1,  /**< Yellow — approaching threshold */
    PI_AF_KPI_STATUS_CRITICAL = 2,  /**< Red — threshold exceeded */
    PI_AF_KPI_STATUS_UNKNOWN  = 3,  /**< Gray — insufficient data */
} pi_af_kpi_status_t;

/**
 * @brief Trend direction for KPI over time.
 */
typedef enum {
    PI_AF_KPI_TREND_STABLE       = 0,  /**< No significant change */
    PI_AF_KPI_TREND_IMPROVING    = 1,  /**< Moving toward target */
    PI_AF_KPI_TREND_DETERIORATING = 2, /**< Moving away from target */
    PI_AF_KPI_TREND_UNDEFINED    = 3,  /**< Insufficient data for trend */
} pi_af_kpi_trend_t;

/**
 * @brief KPI threshold definition.
 *
 * A KPI can have multiple thresholds creating bands:
 *   Good → Warning → Critical
 *
 * Example:
 *   Target = 100, Direction = HIGHER_BETTER
 *   Good ≥ 100, Warning ≥ 80, Critical < 80
 */
typedef struct {
    char     name[64];           /**< Threshold name (e.g., "Warning Low") */
    double   value;              /**< Threshold boundary value */
    pi_af_kpi_status_t status;   /**< Status when crossing this threshold */
    bool     inclusive;          /**< True if >= value, false if > value */
} pi_af_kpi_threshold_t;

/**
 * @brief Complete KPI definition.
 *
 * Maps to OSIsoft PI AF KPI elements.
 */
typedef struct {
    uint32_t  kpi_id;                               /**< Unique identifier */
    char      name[PI_AF_MAX_KPI_NAME];             /**< Display name */
    char      description[PI_AF_MAX_KPI_NAME];      /**< Long description */
    char      uom[32];                              /**< Unit of measure */
    pi_af_kpi_direction_t direction;                /**< Which direction is good */

    double    target;                               /**< Target value */
    double    target_min;                           /**< For TARGET_ZONE direction */
    double    target_max;                           /**< For TARGET_ZONE direction */

    uint32_t  threshold_count;                      /**< Number of thresholds */
    pi_af_kpi_threshold_t thresholds[PI_AF_MAX_KPI_THRESHOLDS];

    /* Data source */
    uint32_t  source_analytic_id;                   /**< Underlying analytic ID */
    char      source_expression[PI_AF_MAX_EXPRESSION_LEN]; /**< Direct expression */

    /* Rollup configuration */
    bool      is_rollup;                            /**< True if this KPI rolls up children */
    uint32_t  child_kpi_ids[PI_AF_MAX_KPI_CHILDREN];
    uint32_t  child_kpi_count;

    /* Trend detection */
    double    trend_window_sec;                     /**< Lookback window for trend */
    double    trend_sensitivity;                    /**< How much change triggers a trend */

    /* Current state */
    double    current_value;                        /**< Most recent KPI value */
    pi_af_kpi_status_t current_status;              /**< Current traffic light */
    pi_af_kpi_trend_t  current_trend;               /**< Current trend */
    time_t    last_updated;                         /**< Timestamp of last update */
    uint32_t  update_count;                         /**< How many times updated */

    /* Performance score (0–100, normalized) */
    double    perf_score;                           /**< Normalized 0–100 performance score */
} pi_af_kpi_t;

/* --------------------------------------------------------------------------
 * L3 — Engineering Structure: KPI Engine State
 * ------------------------------------------------------------------------*/

#define PI_AF_MAX_KPIS 64

/**
 * @brief KPI engine context — manages all KPIs and their relationships.
 */
typedef struct {
    pi_af_kpi_t kpis[PI_AF_MAX_KPIS];
    uint32_t    kpi_count;
    bool        kpi_in_use[PI_AF_MAX_KPIS];

    /* KPI hierarchy for rollup */
    uint32_t    kpi_children[PI_AF_MAX_KPIS][PI_AF_MAX_KPI_CHILDREN];
    uint32_t    kpi_child_count[PI_AF_MAX_KPIS];
    uint32_t    kpi_parent[PI_AF_MAX_KPIS];  /**< Parent KPI ID (0 = root) */

    /* Dependency ordering for calculation */
    uint32_t    calc_order[PI_AF_MAX_KPIS];
    uint32_t    calc_order_count;
    bool        order_valid;
} pi_af_kpi_context_t;

/* --------------------------------------------------------------------------
 * L5 — Algorithms: KPI Evaluation Engine
 * ------------------------------------------------------------------------*/

/**
 * @brief Initialize the KPI engine context.
 *
 * @param ctx  Uninitialized context
 * @return     PI_AF_OK on success
 */
pi_af_error_t pi_af_kpi_init(pi_af_kpi_context_t *ctx);

/**
 * @brief Register a KPI definition.
 *
 * @param ctx    KPI context
 * @param kpi    KPI definition (copied)
 * @param out_id Output: assigned KPI ID
 * @return       PI_AF_OK on success
 */
pi_af_error_t pi_af_kpi_register(pi_af_kpi_context_t *ctx,
                                  const pi_af_kpi_t *kpi, uint32_t *out_id);

/**
 * @brief Evaluate a KPI status from a current value.
 *
 * Compares the current value against the target and thresholds
 * according to the KPI's direction (higher-better / lower-better / target-zone).
 *
 * @param kpi            KPI definition
 * @param current_value  Latest measured value
 * @param out_status     Output: traffic light status
 * @param out_perf_score Output: normalized performance score (0–100)
 * @return               PI_AF_OK on success
 *
 * Complexity: O(t) where t = threshold count
 *
 * @see ISO 22400-2:2014 §6 — KPI evaluation semantics
 */
pi_af_error_t pi_af_kpi_evaluate(const pi_af_kpi_t *kpi, double current_value,
                                  pi_af_kpi_status_t *out_status,
                                  double *out_perf_score);

/**
 * @brief Detect the trend of a KPI over a time window.
 *
 * Uses linear regression slope over the lookback period to determine
 * whether the KPI is improving, deteriorating, or stable.
 *
 * @param data          Historical KPI values (sorted ascending by time)
 * @param count         Number of data points
 * @param direction     Which direction is "better"
 * @param sensitivity   Minimum slope magnitude to declare a trend
 * @param out_trend     Output: trend direction
 * @param out_slope     Output: regression slope (units/second)
 * @return              PI_AF_OK on success
 *
 * Complexity: O(count)
 *
 * @see Box, G.E.P. & Jenkins, G.M. (1976) — Linear trend estimation
 *      in time series analysis
 */
pi_af_error_t pi_af_kpi_detect_trend(const pi_af_datapoint_t *data,
                                      uint32_t count,
                                      pi_af_kpi_direction_t direction,
                                      double sensitivity,
                                      pi_af_kpi_trend_t *out_trend,
                                      double *out_slope);

/**
 * @brief Rollup child KPI values through the asset hierarchy.
 *
 * Aggregates child KPIs into a parent KPI using a specified
 * rollup method. This enables hierarchical scorecards.
 *
 * Rollup methods:
 *   0 = Simple average
 *   1 = Weighted average (using perf_score as weight)
 *   2 = Minimum (worst child)
 *   3 = Maximum (best child)
 *   4 = Count of children in critical state
 *
 * @param kpi_ctx      KPI context
 * @param parent_id    Parent KPI ID
 * @param method       Rollup method (0-4)
 * @param out_value    Output: rolled-up value
 * @param out_status   Output: worst status among children
 * @return             PI_AF_OK on success
 *
 * Complexity: O(c) where c = number of children
 *
 * @see ISO 22400-1 §5.4 — Aggregation of KPIs across hierarchy levels
 */
pi_af_error_t pi_af_kpi_rollup(pi_af_kpi_context_t *kpi_ctx,
                                uint32_t parent_id, int method,
                                double *out_value,
                                pi_af_kpi_status_t *out_status);

/**
 * @brief Update a KPI with a new value and re-evaluate.
 *
 * Sets current_value, updates status and trend, increments update_count.
 *
 * @param kpi_ctx   KPI context
 * @param kpi_id    KPI to update
 * @param value     New value
 * @param timestamp Measurement timestamp
 * @return          PI_AF_OK on success
 */
pi_af_error_t pi_af_kpi_update(pi_af_kpi_context_t *kpi_ctx,
                                uint32_t kpi_id, double value,
                                time_t timestamp);

/**
 * @brief Build the KPI calculation order (bottom-up for rollup KPIs).
 *
 * Uses topological sort to ensure child KPIs are calculated
 * before their parents.
 *
 * @param kpi_ctx  KPI context
 * @return         PI_AF_OK on success
 *
 * Complexity: O(V + E) — Kahn's algorithm
 */
pi_af_error_t pi_af_kpi_build_order(pi_af_kpi_context_t *kpi_ctx);

/**
 * @brief Look up a KPI by ID.
 *
 * @param kpi_ctx  KPI context
 * @param kpi_id   KPI ID
 * @return         Pointer to KPI, or NULL if not found
 *
 * Complexity: O(n) where n = registered KPIs
 */
pi_af_kpi_t *pi_af_kpi_get(pi_af_kpi_context_t *kpi_ctx, uint32_t kpi_id);

/**
 * @brief Get human-readable name for KPI status.
 *
 * Complexity: O(1)
 */
const char *pi_af_kpi_status_name(pi_af_kpi_status_t s);

/**
 * @brief Get human-readable name for KPI direction.
 *
 * Complexity: O(1)
 */
const char *pi_af_kpi_direction_name(pi_af_kpi_direction_t d);

/**
 * @brief Get human-readable name for KPI trend.
 *
 * Complexity: O(1)
 */
const char *pi_af_kpi_trend_name(pi_af_kpi_trend_t t);

/**
 * @brief Compute a composite score from multiple weighted KPIs.
 *
 * Composite = Σ(w_i × score_i) / Σ(w_i)
 *
 * @param scores         Array of KPI performance scores
 * @param weights        Array of corresponding weights
 * @param count          Number of KPIs in the composite
 * @param out_composite  Output: weighted composite score
 * @return               PI_AF_OK on success
 *
 * Complexity: O(count)
 *
 * @see Saaty, T.L. (1980) "The Analytic Hierarchy Process" — Weighted scoring
 */
pi_af_error_t pi_af_kpi_composite_score(const double *scores,
                                         const double *weights,
                                         uint32_t count,
                                         double *out_composite);

#endif /* PI_AF_ANALYTICS_KPI_H */
