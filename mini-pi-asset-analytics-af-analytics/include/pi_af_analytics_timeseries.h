/**
 * @file include/pi_af_analytics_timeseries.h
 * @brief PI AF Analytics — Time-Series Calculation Engine
 *
 * Implements the core time-series analytics functions used in PI AF Analytics
 * expressions. These functions operate on sequences of timestamped data points
 * and compute aggregates, statistics, and derived values over time windows.
 *
 * This is the computational heart of industrial real-time analytics:
 *   - Rolling/Moving window statistics (sum, avg, min, max, stddev, variance)
 *   - Time-weighted vs. event-weighted aggregation
 *   - Interpolation methods (linear, step, cubic)
 *   - Exponential smoothing (EMA, Holt-Winters components)
 *   - Rate of change and cumulative calculations
 *   - Data quality-weighted statistics
 *   - Cycle detection (peak/valley, zero-crossing)
 *
 * Knowledge Coverage: L1 (Time-Series Definitions), L2 (Windowing Concepts),
 *                     L3 (Sliding Window Buffer), L5 (Statistical Algorithms)
 *
 * References:
 *   - Box, G.E.P. & Jenkins, G.M. (1976) "Time Series Analysis"
 *   - Holt, C.C. (1957) "Forecasting seasonals and trends"
 *   - Winters, P.R. (1960) "Forecasting sales by exponentially weighted moving averages"
 *   - OPC UA Part 11 §6.4 — Aggregation semantics for historical data
 *   - OSIsoft PI AF SDK — Analytics function reference
 *
 * Stanford ENGR205 — Exponential smoothing for process variables
 * RWTH Aachen — Industrial time-series analysis methodology
 * Georgia Tech ECE 6550 — Signal processing for control systems
 */

#ifndef PI_AF_ANALYTICS_TIMESERIES_H
#define PI_AF_ANALYTICS_TIMESERIES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <math.h>

#include "pi_af_analytics_core.h"

/* --------------------------------------------------------------------------
 * L1 — Definitions: Aggregation Types and Window Specifications
 * ------------------------------------------------------------------------*/

/** Maximum points in a sliding window buffer */
#define PI_AF_MAX_WINDOW_POINTS 8192

/** Default maximum lookback for time-range queries (seconds) */
#define PI_AF_DEFAULT_LOOKBACK_SEC 86400  /* 24 hours */

/** Minimum points required for stddev/variance calculation */
#define PI_AF_MIN_POINTS_FOR_STATS 2

/**
 * @brief Summary/aggregation types for time-series data.
 *
 * These correspond to the PI AF Analytics function set and
 * OPC UA Part 11 Historical Access aggregation types.
 *
 * @see OPC UA Part 11 Table B.1 — Aggregate types
 */
typedef enum {
    PI_AF_AGG_NONE               = 0,
    PI_AF_AGG_SUM                = 1,   /**< Arithmetic sum */
    PI_AF_AGG_AVERAGE            = 2,   /**< Simple arithmetic mean */
    PI_AF_AGG_MIN                = 3,   /**< Minimum value */
    PI_AF_AGG_MAX                = 4,   /**< Maximum value */
    PI_AF_AGG_RANGE              = 5,   /**< Max − Min */
    PI_AF_AGG_COUNT              = 6,   /**< Point count */
    PI_AF_AGG_STDDEV_POP         = 7,   /**< Population standard deviation */
    PI_AF_AGG_STDDEV_SAMPLE      = 8,   /**< Sample standard deviation (n−1) */
    PI_AF_AGG_VARIANCE_POP       = 9,   /**< Population variance */
    PI_AF_AGG_VARIANCE_SAMPLE    = 10,  /**< Sample variance (n−1) */
    PI_AF_AGG_PERCENT_GOOD       = 11,  /**< Fraction of points with Good quality */
    PI_AF_AGG_TIME_WEIGHTED_AVG  = 12,  /**< Integral-based time-weighted average */
    PI_AF_AGG_FIRST              = 13,  /**< First value in window */
    PI_AF_AGG_LAST               = 14,  /**< Last value in window */
    PI_AF_AGG_MEDIAN             = 15,  /**< Median value */
    PI_AF_AGG_DELTA              = 16,  /**< Last − First */
    PI_AF_AGG_TOTAL_COUNT
} pi_af_aggregate_t;

/**
 * @brief Window boundary specification.
 *
 * PI AF Analytics supports flexible window boundaries:
 * - Absolute: specific start/end timestamps
 * - Relative: lookback duration from now (e.g., "last 5 minutes")
 * - Wide: from the beginning of time
 * - Count-based: last N points regardless of time
 */
typedef enum {
    PI_AF_WIN_TYPE_ABSOLUTE  = 0,  /**< [start_ts, end_ts] */
    PI_AF_WIN_TYPE_RELATIVE  = 1,  /**< [now - lookback, now] */
    PI_AF_WIN_TYPE_WIDE_OPEN = 2,  /**< [earliest point, end_ts] */
    PI_AF_WIN_TYPE_COUNT     = 3,  /**< Last N points */
} pi_af_window_type_t;

/**
 * @brief Time window specification.
 */
typedef struct {
    pi_af_window_type_t type;
    time_t   start_ts;         /**< For PI_AF_WIN_TYPE_ABSOLUTE */
    time_t   end_ts;           /**< For PI_AF_WIN_TYPE_ABSOLUTE / WIDE_OPEN */
    double   lookback_sec;     /**< For PI_AF_WIN_TYPE_RELATIVE */
    uint32_t point_count;      /**< For PI_AF_WIN_TYPE_COUNT */
} pi_af_time_window_t;

/**
 * @brief Interpolation methods for time-series gaps.
 *
 * When computing time-weighted averages, the calculation needs to
 * determine the value between known data points.
 */
typedef enum {
    PI_AF_INTERP_NONE       = 0,  /**< No interpolation — gap = 0 contribution */
    PI_AF_INTERP_STEP       = 1,  /**< Hold previous value (staircase) */
    PI_AF_INTERP_LINEAR     = 2,  /**< Linear between adjacent points */
    PI_AF_INTERP_SPLINE     = 3,  /**< Cubic spline (smoother but more complex) */
} pi_af_interp_method_t;

/**
 * @brief Exponential Moving Average (EMA) state.
 *
 * EMA applies a smoothing factor α where:
 *   EMA(t) = α × value(t) + (1-α) × EMA(t-1)
 *
 * α = 2/(N+1) for an N-period equivalent (Hunter, 1986).
 *
 * @see Hunter, J.S. (1986) "The exponentially weighted moving average"
 *      Journal of Quality Technology, 18(4), 203-210.
 */
typedef struct {
    double   alpha;            /**< Smoothing factor (0 < α ≤ 1) */
    double   current_ema;      /**< Current EMA value */
    bool     initialized;      /**< Whether we have received at least one value */
    uint64_t sample_count;     /**< Number of samples processed */
    time_t   last_sample_time; /**< Timestamp of last update */
} pi_af_ema_state_t;

/**
 * @brief Holt-Winters triple exponential smoothing state.
 *
 * Decomposes a time series into three components:
 *   Level (baseline): L(t) = α × Y(t)/S(t−m) + (1−α) × (L(t−1) + T(t−1))
 *   Trend:            T(t) = β × (L(t) − L(t−1)) + (1−β) × T(t−1)
 *   Seasonal:         S(t) = γ × Y(t)/L(t) + (1−γ) × S(t−m)
 *
 * Forecast for k steps ahead:
 *   F(t+k) = (L(t) + k × T(t)) × S(t−m+k)
 *
 * @see Winters, P.R. (1960) "Forecasting sales by exponentially weighted moving averages"
 *      Management Science, 6(3), 324-342.
 * @see Holt, C.C. (1957) "Forecasting seasonals and trends by exponentially weighted moving averages"
 *      ONR Research Memorandum, Carnegie Institute of Technology.
 */
typedef struct {
    double   alpha;            /**< Level smoothing factor */
    double   beta;             /**< Trend smoothing factor */
    double   gamma;            /**< Seasonal smoothing factor */
    uint32_t season_length;    /**< Number of periods per season */
    double   *level;           /**< Level component at each period */
    double   *trend;           /**< Trend component at each period */
    double   *seasonal;        /**< Seasonal factors (length = season_length) */
    double   current_level;
    double   current_trend;
    bool     initialized;
    uint64_t sample_count;
    time_t   last_sample_time;
} pi_af_holt_winters_state_t;

/* --------------------------------------------------------------------------
 * L3 — Engineering Structure: Sliding Window Buffer
 * ------------------------------------------------------------------------*/

/**
 * @brief Ring-buffer implementation of a sliding window.
 *
 * Maintains the most recent N data points for efficient window
 * statistics. Insertion is O(1) amortized; queries scan the window.
 *
 * This is the core data structure for all rolling analytics.
 */
typedef struct {
    pi_af_datapoint_t *buffer;      /**< Circular buffer storage */
    uint32_t capacity;              /**< Maximum points in buffer */
    uint32_t head;                  /**< Write position (oldest → newest) */
    uint32_t count;                 /**< Current number of points */
    bool     wrapped;               /**< Whether buffer has wrapped around */
} pi_af_sliding_window_t;

/* --------------------------------------------------------------------------
 * L5 — Algorithms: Time-Series Analytics Functions
 * ------------------------------------------------------------------------*/

/**
 * @brief Initialize a sliding window buffer.
 *
 * @param window    Uninitialized window struct
 * @param capacity  Maximum number of points to store
 * @return          PI_AF_OK on success
 *
 * Complexity: O(capacity) for allocation
 */
pi_af_error_t pi_af_sliding_window_init(pi_af_sliding_window_t *window,
                                         uint32_t capacity);

/**
 * @brief Free a sliding window buffer.
 *
 * Complexity: O(1)
 */
void pi_af_sliding_window_free(pi_af_sliding_window_t *window);

/**
 * @brief Push a new data point into the sliding window.
 *
 * If the window is full, the oldest point is overwritten (circular buffer).
 *
 * @param window  Sliding window
 * @param point   Data point to add
 *
 * Complexity: O(1)
 */
void pi_af_sliding_window_push(pi_af_sliding_window_t *window,
                                const pi_af_datapoint_t *point);

/**
 * @brief Get the number of points currently in the window.
 *
 * Complexity: O(1)
 */
uint32_t pi_af_sliding_window_count(const pi_af_sliding_window_t *window);

/**
 * @brief Get a point from the window by logical index (0 = oldest).
 *
 * @param window  Sliding window
 * @param index   0-based index (0 = oldest, count-1 = newest)
 * @return        Pointer to point, or NULL if out of range
 *
 * Complexity: O(1)
 */
const pi_af_datapoint_t *pi_af_sliding_window_get(
    const pi_af_sliding_window_t *window, uint32_t index);

/**
 * @brief Compute a summary statistic over all points in the window.
 *
 * @param window     Data window
 * @param aggregate  Which statistic to compute
 * @param result     Output: computed value
 * @return           PI_AF_OK on success
 *
 * Complexity: O(n) where n = window count (single pass)
 *
 * @note PI_AF_AGG_STDDEV* and PI_AF_AGG_VARIANCE* use Welford's
 *       online algorithm (single-pass, numerically stable).
 *
 * @see Welford, B.P. (1962) "Note on a method for calculating
 *      corrected sums of squares and products", Technometrics 4(3), 419-420.
 */
pi_af_error_t pi_af_window_aggregate(const pi_af_sliding_window_t *window,
                                      pi_af_aggregate_t aggregate,
                                      double *result);

/**
 * @brief Compute a summary statistic over a time-range subset of data points.
 *
 * Filters points within the specified time window before computing
 * the aggregate.
 *
 * @param data       Array of data points (must be time-sorted ascending)
 * @param count      Number of points
 * @param window     Time range specification
 * @param interp     Interpolation method for boundary points
 * @param aggregate  Which statistic to compute
 * @param result     Output: computed value
 * @return           PI_AF_OK on success
 *
 * Complexity: O(n) where n = count
 */
pi_af_error_t pi_af_timerange_aggregate(const pi_af_datapoint_t *data,
                                         uint32_t count,
                                         const pi_af_time_window_t *window,
                                         pi_af_interp_method_t interp,
                                         pi_af_aggregate_t aggregate,
                                         double *result);

/**
 * @brief Compute time-weighted average using the trapezoidal integration method.
 *
 * For each interval [tᵢ, tᵢ₊₁], the contribution is:
 *   (vᵢ + vᵢ₊₁) / 2 × (tᵢ₊₁ − tᵢ)
 *
 * The time-weighted average is total_contribution / total_duration.
 *
 * This is the standard PI Data Archive method for time-weighted averages
 * and corresponds to OPC UA Aggregate "TimeAverage".
 *
 * @param data     Sorted time-series data
 * @param count    Number of points
 * @param start_ts Window start timestamp
 * @param end_ts   Window end timestamp
 * @param interp   Interpolation for edge cases
 * @param result   Output: time-weighted average
 * @return         PI_AF_OK on success
 *
 * Complexity: O(n) where n = count
 *
 * @see OPC UA Part 11 §6.4.2 — TimeAverage aggregate definition
 */
pi_af_error_t pi_af_time_weighted_average(const pi_af_datapoint_t *data,
                                           uint32_t count,
                                           time_t start_ts, time_t end_ts,
                                           pi_af_interp_method_t interp,
                                           double *result);

/**
 * @brief Initialize an EMA (Exponentially Weighted Moving Average) state.
 *
 * @param state  Uninitialized state
 * @param alpha  Smoothing factor (0 < α ≤ 1)
 * @return       PI_AF_OK on success
 *
 * Complexity: O(1)
 */
pi_af_error_t pi_af_ema_init(pi_af_ema_state_t *state, double alpha);

/**
 * @brief Update EMA with a new value.
 *
 * EMA(t) = α × value + (1−α) × EMA(t−1)
 *
 * On first call (initialized == false), EMA is set directly to value.
 *
 * @param state     EMA state
 * @param value     New observation
 * @param timestamp Timestamp of observation
 * @param out_ema   Output: updated EMA value
 * @return          PI_AF_OK on success
 *
 * Complexity: O(1)
 */
pi_af_error_t pi_af_ema_update(pi_af_ema_state_t *state, double value,
                                time_t timestamp, double *out_ema);

/**
 * @brief Initialize Holt-Winters state.
 *
 * @param state         Uninitialized state
 * @param alpha         Level smoothing factor
 * @param beta          Trend smoothing factor
 * @param gamma         Seasonal smoothing factor
 * @param season_length Number of periods per cycle
 * @return              PI_AF_OK on success
 *
 * Complexity: O(season_length) for allocation
 */
pi_af_error_t pi_af_holt_winters_init(pi_af_holt_winters_state_t *state,
                                       double alpha, double beta, double gamma,
                                       uint32_t season_length);

/**
 * @brief Update Holt-Winters model with a new value.
 *
 * Returns the one-step-ahead forecast for diagnostic purposes.
 *
 * @param state     Holt-Winters state
 * @param value     New observation
 * @param timestamp Timestamp
 * @param out_level Output: current level estimate
 * @param out_trend Output: current trend estimate
 * @param out_forecast Output: one-step-ahead forecast
 * @return          PI_AF_OK on success
 *
 * Complexity: O(1)
 */
pi_af_error_t pi_af_holt_winters_update(pi_af_holt_winters_state_t *state,
                                         double value, time_t timestamp,
                                         double *out_level, double *out_trend,
                                         double *out_forecast);

/**
 * @brief Compute k-step-ahead Holt-Winters forecast.
 *
 * @param state  Initialized state
 * @param steps  Number of steps ahead to forecast
 * @return       Forecast value
 *
 * Complexity: O(1)
 */
double pi_af_holt_winters_forecast(const pi_af_holt_winters_state_t *state,
                                    uint32_t steps);

/**
 * @brief Free Holt-Winters state.
 *
 * Complexity: O(1)
 */
void pi_af_holt_winters_free(pi_af_holt_winters_state_t *state);

/**
 * @brief Compute rate of change (derivative estimate) over a time window.
 *
 * Uses the slope of a linear regression over the data points within
 * the time window:  slope = Σ((tᵢ−t̄)(vᵢ−v̄)) / Σ((tᵢ−t̄)²)
 *
 * This provides a noise-robust estimate compared to simple
 * (last − first) / (t_last − t_first).
 *
 * @param data     Time-series data (sorted)
 * @param count    Number of points
 * @param window   Time window
 * @param result   Output: rate of change (units/second)
 * @return         PI_AF_OK on success
 *
 * Complexity: O(count)
 *
 * @see Press, W.H. et al. (1992) "Numerical Recipes" §15.2
 *      — Fitting data to a straight line
 */
pi_af_error_t pi_af_rate_of_change(const pi_af_datapoint_t *data,
                                    uint32_t count,
                                    const pi_af_time_window_t *window,
                                    double *result);

/**
 * @brief Compute cumulative sum over time with overflow protection.
 *
 * CUSUM is widely used in statistical process control (SPC) and
 * change-point detection applications.
 *
 * The traditional CUSUM:
 *   Sᵢ = max(0, Sᵢ₋₁ + (xᵢ − μ₀ − k))
 *   alarms when Sᵢ > h (decision interval)
 *
 * This implementation computes the CUSUM diagnostic and also detects
 * when the cumulative sum exceeds specified upper/lower bounds.
 *
 * @param data         Time-series data (sorted)
 * @param count        Number of points
 * @param target_mean  Target process mean (μ₀)
 * @param slack        Allowable slack (k)
 * @param decision_interval Upper control limit (h) — alarms if > h
 * @param out_cusum    Output: final CUSUM value
 * @param out_alarmed  Output: true if decision interval was exceeded
 * @return             PI_AF_OK on success
 *
 * Complexity: O(count)
 *
 * @see Page, E.S. (1954) "Continuous inspection schemes", Biometrika 41, 100-115.
 * @see Hawkins, D.M. & Olwell, D.H. (1998) "Cumulative Sum Charts and Charting
 *      for Quality Improvement", Springer.
 */
pi_af_error_t pi_af_cusum_detect(const pi_af_datapoint_t *data,
                                  uint32_t count,
                                  double target_mean, double slack,
                                  double decision_interval,
                                  double *out_cusum, bool *out_alarmed);

/**
 * @brief Detect cycle crossings (peak/valley, zero-crossing, threshold).
 *
 * Identifies when a time series crosses through specified thresholds
 * or changes direction (local extrema).
 *
 * Cycle crossing types:
 *   0 = zero crossing (value crosses 0)
 *   1 = threshold_up (crosses above threshold)
 *   2 = threshold_down (crosses below threshold)
 *   3 = peak (local maximum)
 *   4 = valley (local minimum)
 *
 * @param data        Time-series data (sorted)
 * @param count       Number of points
 * @param cycle_type  Type of crossing to detect
 * @param threshold   Threshold value (for threshold_up/down)
 * @param out_count   Output: number of crossings found
 * @param out_timestamps Output: array of crossing timestamps
 * @param max_crossings  Capacity of out_timestamps array
 * @return            PI_AF_OK on success
 *
 * Complexity: O(count)
 */
pi_af_error_t pi_af_detect_cycles(const pi_af_datapoint_t *data,
                                   uint32_t count,
                                   int cycle_type, double threshold,
                                   uint32_t *out_count,
                                   time_t *out_timestamps,
                                   uint32_t max_crossings);

/**
 * @brief Linear interpolation between two data points.
 *
 * Computes the estimated value at a target timestamp given two
 * surrounding data points, using linear interpolation.
 *
 * v(t) = v1 + (v2 − v1) × (t − t1) / (t2 − t1)
 *
 * @param p1        Earlier data point
 * @param p2        Later data point
 * @param target_ts Desired timestamp (must be between p1.timestamp and p2.timestamp)
 * @return          Interpolated value
 *
 * Complexity: O(1)
 */
double pi_af_interpolate_linear(const pi_af_datapoint_t *p1,
                                 const pi_af_datapoint_t *p2,
                                 time_t target_ts);

/**
 * @brief Step interpolation (hold previous value).
 *
 * Returns the value of the most recent data point at or before target_ts.
 *
 * @param data     Time-series data (sorted ascending)
 * @param count    Number of points
 * @param target_ts Desired timestamp
 * @param out_val  Output: interpolated value
 * @return         true if a point was found, false otherwise
 *
 * Complexity: O(log count) using binary search
 */
bool pi_af_interpolate_step(const pi_af_datapoint_t *data, uint32_t count,
                             time_t target_ts, double *out_val);

/**
 * @brief Compute the percent of data points with "Good" quality.
 *
 * %Good = (count_good / total_count) × 100
 *
 * This is a critical metric for data reliability assessment.
 *
 * @param data     Time-series data
 * @param count    Number of points
 * @return         Percentage (0.0 to 100.0)
 *
 * Complexity: O(count)
 */
double pi_af_percent_good(const pi_af_datapoint_t *data, uint32_t count);

/**
 * @brief Compute Simple Moving Average over a fixed number of recent points.
 *
 * SMA(k) = (1/k) × Σ(x_{n−k+1}, ..., x_n)
 *
 * @param data       Time-series data (most recent last)
 * @param count      Total number of points
 * @param window_k   Number of points in the moving window
 * @param out_result Output: SMA value
 * @return           PI_AF_OK on success
 *
 * Complexity: O(window_k)
 */
pi_af_error_t pi_af_simple_moving_average(const pi_af_datapoint_t *data,
                                           uint32_t count, uint32_t window_k,
                                           double *out_result);

/**
 * @brief Get human-readable name for an aggregate type.
 *
 * Complexity: O(1)
 */
const char *pi_af_aggregate_name(pi_af_aggregate_t agg);

/**
 * @brief Get human-readable name for a window type.
 *
 * Complexity: O(1)
 */
const char *pi_af_window_type_name(pi_af_window_type_t t);

/**
 * @brief Get human-readable name for an interpolation method.
 *
 * Complexity: O(1)
 */
const char *pi_af_interp_method_name(pi_af_interp_method_t m);

#endif /* PI_AF_ANALYTICS_TIMESERIES_H */
