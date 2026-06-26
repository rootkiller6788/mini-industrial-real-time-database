#ifndef HISTORIAN_WINDOWING_H
#define HISTORIAN_WINDOWING_H

/**
 * @file    historian_windowing.h
 * @brief   Time-based window functions for industrial historian queries.
 *
 * Windowing is the process of partitioning a continuous time series into
 * discrete, possibly overlapping intervals. It is fundamental to:
 *   - Batch process analysis (ISA-88 phases, operations)
 *   - Rolling KPI calculations (hourly average over last 24h)
 *   - Event-frame based analytics (PI Event Frames)
 *   - Stream processing (Kafka Streams / Flink tumbling windows)
 *
 * Knowledge Coverage:
 *   L2: Window types (tumbling, sliding, session, calendar)
 *   L3: SQL window functions (ROW_NUMBER, LAG, LEAD over time partitions)
 *   L5: Session window detection, gap-based windowing
 *   L7: PI Event Frames, PI Batch context
 *   L8: Stream-table duality in historian contexts
 *
 * References:
 *   - Arasu et al. "STREAM: The Stanford Stream Data Manager" (2003)
 *   - Akidau et al. "The Dataflow Model" (2015), Google
 *   - PI Event Frames User Guide, OSIsoft (2020)
 */

#include "historian_model.h"
#include <stdint.h>
#include <stddef.h>

/*---------------------------------------------------------------------------
 * L2: Window Types
 *
 * Four fundamental window types in industrial time-series processing:
 *
 * TUMBLING: Fixed-size, non-overlapping, contiguous.
 *   [0-10], [10-20], [20-30]...
 *   Use: Hourly averages, shift totals.
 *
 * SLIDING: Fixed-size, overlapping, defined by size + slide.
 *   size=10, slide=5: [0-10], [5-15], [10-20]...
 *   Use: Rolling 24-hour average updated every 5 minutes.
 *
 * SESSION: Variable-size, bounded by inactivity gaps.
 *   Windows close when gap > max_gap since last event.
 *   Use: Batch process phases (ISA-88), equipment run cycles.
 *
 * CALENDAR: Fixed-size, aligned to calendar boundaries.
 *   [00:00-24:00], [Mon-Sun], [Jan-Dec]...
 *   Use: Daily production reports, monthly KPIs (ISO 22400).
 *---------------------------------------------------------------------------*/

typedef enum {
    HISTORIAN_WINDOW_TUMBLING  = 0,
    HISTORIAN_WINDOW_SLIDING   = 1,
    HISTORIAN_WINDOW_SESSION   = 2,
    HISTORIAN_WINDOW_CALENDAR  = 3
} historian_window_type_t;

/**
 * @struct historian_window_spec_t
 * @brief  Specification for time-based windowing.
 */
typedef struct {
    historian_window_type_t type;

    /* Size: duration of each window in milliseconds */
    int64_t window_size_ms;

    /* Slide: how far the window advances per step (only for SLIDING) */
    int64_t window_slide_ms;

    /* Session gap: max inactivity within a session (only for SESSION) */
    int64_t session_max_gap_ms;

    /* Offset from natural boundary (only for CALENDAR) */
    int64_t calendar_offset_ms;

    /* Minimum number of points for a window to be considered active */
    size_t min_points_per_window;
} historian_window_spec_t;

/**
 * @struct historian_window_t
 * @brief  A single window instance covering a time interval.
 *
 * Each window has a start time, end time, and can carry aggregated
 * statistics over its data.
 */
typedef struct {
    int64_t               start_time_ms;
    int64_t               end_time_ms;
    size_t                point_count;       /**< Number of data points in window */
    double                aggregate_value;   /**< Pre-computed aggregate (mean by default) */
    historian_quality_t   quality;           /**< Aggregate quality (worst of contents) */
    int                   is_closed;         /**< 1 if window is complete (no new data) */
    char                  label[64];         /**< Human-readable window label */
} historian_window_t;

/**
 * @brief A collection of windows from a windowing operation.
 */
typedef struct {
    historian_window_t *windows;
    size_t              count;
    size_t              capacity;
} historian_window_set_t;

/*---------------------------------------------------------------------------
 * Window Specification API
 *---------------------------------------------------------------------------*/

/**
 * Initialize a window specification to sensible defaults.
 */
void historian_window_spec_init(historian_window_spec_t *spec);

/**
 * Convenience: create a tumbling window spec.
 */
void historian_window_spec_tumbling(historian_window_spec_t *spec,
                                     int64_t window_size_ms);

/**
 * Convenience: create a sliding window spec.
 */
void historian_window_spec_sliding(historian_window_spec_t *spec,
                                    int64_t window_size_ms,
                                    int64_t slide_ms);

/**
 * Convenience: create a session window spec.
 */
void historian_window_spec_session(historian_window_spec_t *spec,
                                    int64_t max_gap_ms);

/*---------------------------------------------------------------------------
 * Windowing API
 *---------------------------------------------------------------------------*/

/**
 * Partition a time series into windows according to the specification.
 *
 * For tumbling and sliding windows, windows are created at fixed intervals.
 * For session windows, windows are created based on data gaps.
 * For calendar windows, windows align to calendar boundaries.
 *
 * @param points      Sorted array of data points.
 * @param count       Number of data points.
 * @param spec        Window specification.
 * @param result      Output window set (caller must init first).
 * @return 0 on success, negative on error.
 */
int historian_window_partition(const historian_data_point_t *points,
                                size_t count,
                                const historian_window_spec_t *spec,
                                historian_window_set_t *result);

/**
 * Tumbling window partition.
 *
 * Divides the time range [first_timestamp, last_timestamp] into
 * non-overlapping windows of equal size.
 */
int historian_window_tumbling(const historian_data_point_t *points,
                               size_t count, int64_t window_size_ms,
                               historian_window_set_t *result);

/**
 * Sliding window partition.
 *
 * Creates overlapping windows that advance by slide_ms.
 * Each window size is window_size_ms.
 */
int historian_window_sliding(const historian_data_point_t *points,
                              size_t count,
                              int64_t window_size_ms, int64_t slide_ms,
                              historian_window_set_t *result);

/**
 * Session window partition.
 *
 * Groups data points into sessions based on inter-arrival gaps.
 * A new session starts whenever the gap between consecutive points
 * exceeds session_max_gap_ms.
 *
 * This is the fundamental operation behind:
 *   - Batch phase detection (ISA-88)
 *   - Equipment run/stop cycle identification
 *   - User session analytics
 */
int historian_window_session(const historian_data_point_t *points,
                              size_t count, int64_t max_gap_ms,
                              historian_window_set_t *result);

/**
 * Calendar window partition.
 *
 * Aligns windows to natural calendar boundaries:
 *   - Hourly:  [HH:00, HH:00+1h)
 *   - Daily:   [00:00, 24:00)
 *   - Weekly:  [Mon 00:00, next Mon)
 *   - Monthly: [1st 00:00, next 1st)
 *
 * @param points        Sorted array of data points.
 * @param count         Number of points.
 * @param window_size_ms Duration of each calendar window.
 * @param align_ms      Offset from midnight to start first window.
 * @param result        Output window set.
 * @return 0 on success.
 */
int historian_window_calendar(const historian_data_point_t *points,
                               size_t count,
                               int64_t window_size_ms, int64_t align_ms,
                               historian_window_set_t *result);

/**
 * Detect gaps in a time series.
 *
 * A gap is an interval where no data points exist for longer than
 * max_gap_ms. This function identifies gap start/end as windows.
 *
 * @param points      Sorted array of data points.
 * @param count       Number of points.
 * @param max_gap_ms  Minimum duration to consider as a gap.
 * @param result      Output windows representing gaps.
 * @return 0 on success.
 *
 * Knowledge: Data quality analysis. Gaps indicate communication loss
 * or sensor failure (L2: Core concept).
 */
int historian_detect_gaps(const historian_data_point_t *points,
                           size_t count, int64_t max_gap_ms,
                           historian_window_set_t *result);

/**
 * Compute the rate of change between consecutive points and
 * partition into windows where the rate exceeds a threshold.
 *
 * Useful for detecting process upsets, ramp events, or anomalies.
 *
 * @param points          Sorted array of data points.
 * @param count           Number of points.
 * @param roc_threshold   Rate-of-change threshold (units/second).
 * @param result          Output: windows with high ROC.
 * @return 0 on success.
 */
int historian_detect_rate_of_change(const historian_data_point_t *points,
                                     size_t count, double roc_threshold,
                                     historian_window_set_t *result);

/*---------------------------------------------------------------------------
 * L3: SQL Window Functions over Time-Series
 *
 * Standard SQL:2003 window functions map naturally to historian queries:
 *   ROW_NUMBER() OVER (PARTITION BY tag ORDER BY timestamp)
 *   LAG(value, 1) OVER (PARTITION BY tag ORDER BY timestamp)
 *   LEAD(value, 1) OVER (...)
 *---------------------------------------------------------------------------*/

/**
 * Compute LAG (previous value) for each point in a time series.
 *
 * For each point at index i, output[i] = value of point at index (i - lag_offset),
 * or NAN if i < lag_offset.
 *
 * @param points      Input data points (sorted by time).
 * @param count       Number of input points.
 * @param lag_offset  How many rows back to look.
 * @param lag_values  Output array (must have count elements).
 * @return 0 on success.
 *
 * Knowledge: SQL:2003 LAG() window function for time-series.
 * Used for computing deltas:  delta[i] = value[i] - LAG(value, 1)[i]
 */
int historian_sql_lag(const historian_data_point_t *points, size_t count,
                       int lag_offset, double *lag_values);

/**
 * Compute LEAD (next value) for each point.
 *
 * output[i] = value of point at index (i + lead_offset), or NAN if beyond end.
 */
int historian_sql_lead(const historian_data_point_t *points, size_t count,
                        int lead_offset, double *lead_values);

/*---------------------------------------------------------------------------
 * Window Set Memory Management
 *---------------------------------------------------------------------------*/

void historian_window_set_init(historian_window_set_t *ws);

int historian_window_set_append(historian_window_set_t *ws,
                                 historian_window_t window);

void historian_window_set_destroy(historian_window_set_t *ws);

#endif /* HISTORIAN_WINDOWING_H */
