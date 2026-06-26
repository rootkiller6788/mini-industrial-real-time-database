/**
 * @file    historian_windowing.c
 * @brief   Time-based window function implementations.
 *
 * Knowledge coverage:
 *   L2: Tumbling, sliding, session, calendar windows
 *   L3: SQL window function analogs (LAG, LEAD)
 *   L5: Gap detection, rate-of-change detection
 */

#include "historian_windowing.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* =========================================================================
 * L2: Window Specification Management
 * ========================================================================= */

void historian_window_spec_init(historian_window_spec_t *spec)
{
    if (!spec) return;
    memset(spec, 0, sizeof(*spec));
    spec->type = HISTORIAN_WINDOW_TUMBLING;
    spec->window_size_ms = 3600000LL;  /* 1 hour */
    spec->window_slide_ms = 600000LL;  /* 10 minutes */
    spec->session_max_gap_ms = 300000LL; /* 5 minutes */
    spec->calendar_offset_ms = 0;
    spec->min_points_per_window = 1;
}

void historian_window_spec_tumbling(historian_window_spec_t *spec,
                                     int64_t window_size_ms)
{
    historian_window_spec_init(spec);
    spec->type = HISTORIAN_WINDOW_TUMBLING;
    spec->window_size_ms = window_size_ms;
}

void historian_window_spec_sliding(historian_window_spec_t *spec,
                                    int64_t window_size_ms,
                                    int64_t slide_ms)
{
    historian_window_spec_init(spec);
    spec->type = HISTORIAN_WINDOW_SLIDING;
    spec->window_size_ms = window_size_ms;
    spec->window_slide_ms = slide_ms;
}

void historian_window_spec_session(historian_window_spec_t *spec,
                                    int64_t max_gap_ms)
{
    historian_window_spec_init(spec);
    spec->type = HISTORIAN_WINDOW_SESSION;
    spec->session_max_gap_ms = max_gap_ms;
}

/* =========================================================================
 * Window Set Memory Management
 * ========================================================================= */

void historian_window_set_init(historian_window_set_t *ws)
{
    if (!ws) return;
    ws->windows = NULL;
    ws->count = 0;
    ws->capacity = 0;
}

int historian_window_set_append(historian_window_set_t *ws,
                                 historian_window_t window)
{
    if (!ws) return -1;
    if (ws->capacity == 0) {
        ws->windows = (historian_window_t *)malloc(
            64 * sizeof(historian_window_t));
        if (!ws->windows) return -1;
        ws->capacity = 64;
    }
    if (ws->count >= ws->capacity) {
        size_t new_cap = ws->capacity * 2;
        historian_window_t *new_w = (historian_window_t *)realloc(
            ws->windows, new_cap * sizeof(historian_window_t));
        if (!new_w) return -1;
        ws->windows = new_w;
        ws->capacity = new_cap;
    }
    ws->windows[ws->count++] = window;
    return 0;
}

void historian_window_set_destroy(historian_window_set_t *ws)
{
    if (!ws) return;
    free(ws->windows);
    ws->windows = NULL;
    ws->count = 0;
    ws->capacity = 0;
}

/* =========================================================================
 * L2: Tumbling Window
 *
 * Divides the time range into non-overlapping, contiguous windows of
 * equal size. Each data point belongs to exactly one window.
 *
 * Usage: hourly production totals, shift summaries.
 * ========================================================================= */

/**
 * Compute the aggregate value for points in a window.
 * Simple arithmetic mean by default.
 */
static double window_aggregate(const historian_data_point_t *points,
                                size_t start_idx, size_t end_idx)
{
    if (end_idx <= start_idx) return NAN;

    double sum = 0.0;
    size_t valid_count = 0;
    for (size_t i = start_idx; i < end_idx; i++) {
        if (isfinite(points[i].value)) {
            sum += points[i].value;
            valid_count++;
        }
    }
    if (valid_count == 0) return NAN;
    return sum / (double)valid_count;
}

/**
 * Determine the worst quality among points in a window.
 */
static historian_quality_t window_worst_quality(
    const historian_data_point_t *points, size_t start_idx, size_t end_idx)
{
    historian_quality_t worst = HISTORIAN_QUAL_GOOD;
    for (size_t i = start_idx; i < end_idx; i++) {
        if (!historian_quality_is_good(points[i].quality)) {
            worst = points[i].quality;
        }
    }
    return worst;
}

int historian_window_tumbling(const historian_data_point_t *points,
                               size_t count, int64_t window_size_ms,
                               historian_window_set_t *result)
{
    if (!points || !result) return -1;
    if (count == 0 || window_size_ms <= 0) return -2;

    int64_t start_time = points[0].timestamp.epoch_ms;
    int64_t end_time = points[count - 1].timestamp.epoch_ms;

    size_t point_idx = 0;
    int win_num = 0;

    for (int64_t win_start = start_time; win_start <= end_time;
         win_start += window_size_ms, win_num++) {

        int64_t win_end = win_start + window_size_ms;

        /* Gather points in this window */
        size_t win_point_start = point_idx;
        while (point_idx < count &&
               points[point_idx].timestamp.epoch_ms < win_end) {
            point_idx++;
        }
        size_t win_point_end = point_idx;

        size_t win_point_count = win_point_end - win_point_start;
        if (win_point_count == 0) continue;

        historian_window_t win;
        memset(&win, 0, sizeof(win));
        win.start_time_ms = win_start;
        win.end_time_ms = win_end;
        win.point_count = win_point_count;
        win.aggregate_value = window_aggregate(points, win_point_start, win_point_end);
        win.quality = window_worst_quality(points, win_point_start, win_point_end);
        win.is_closed = (win_end < end_time) ? 1 : 0;
        snprintf(win.label, sizeof(win.label), "Tumbling_%d", win_num);

        historian_window_set_append(result, win);

        if (point_idx >= count) break;
    }

    return 0;
}

/* =========================================================================
 * L2: Sliding Window
 *
 * Creates overlapping windows that advance by slide_ms.
 * Each window has the same size but a different starting point.
 *
 * Usage: Rolling 24-hour average updated every 15 minutes.
 *
 * Note: A sliding window with slide = 0 degenerates to a single window
 * spanning the entire data range.
 * ========================================================================= */

int historian_window_sliding(const historian_data_point_t *points,
                              size_t count,
                              int64_t window_size_ms, int64_t slide_ms,
                              historian_window_set_t *result)
{
    if (!points || !result) return -1;
    if (count == 0 || window_size_ms <= 0) return -2;
    if (slide_ms <= 0) slide_ms = window_size_ms;

    int64_t start_time = points[0].timestamp.epoch_ms;
    int64_t end_time = points[count - 1].timestamp.epoch_ms;

    int win_num = 0;

    for (int64_t win_start = start_time;
         win_start + window_size_ms <= end_time + slide_ms;
         win_start += slide_ms, win_num++) {

        int64_t win_end = win_start + window_size_ms;

        /* Find points in this window using binary search for efficiency */
        /* Find first point >= win_start */
        size_t si = 0;
        {
            size_t lo = 0, hi = count;
            while (lo < hi) {
                size_t mid = lo + (hi - lo) / 2;
                if (points[mid].timestamp.epoch_ms < win_start) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            si = lo;
        }

        /* Find first point >= win_end */
        size_t ei = count;
        {
            size_t lo = 0, hi = count;
            while (lo < hi) {
                size_t mid = lo + (hi - lo) / 2;
                if (points[mid].timestamp.epoch_ms < win_end) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            ei = lo;
        }

        size_t win_point_count = ei - si;
        if (win_point_count == 0) continue;

        historian_window_t win;
        memset(&win, 0, sizeof(win));
        win.start_time_ms = win_start;
        win.end_time_ms = win_end;
        win.point_count = win_point_count;
        win.aggregate_value = window_aggregate(points, si, ei);
        win.quality = window_worst_quality(points, si, ei);
        win.is_closed = (win_end < end_time) ? 1 : 0;
        snprintf(win.label, sizeof(win.label), "Sliding_%d", win_num);

        historian_window_set_append(result, win);
    }

    return 0;
}

/* =========================================================================
 * L2: Session Window (Gap-Based Windowing)
 *
 * Groups data points into sessions by detecting inactivity gaps.
 * A new session starts whenever the time gap between consecutive data
 * points exceeds session_max_gap_ms.
 *
 * This is the fundamental windowing mechanism for:
 *   - Batch process phases (ISA-88)
 *   - Equipment run/stop cycles (OEE calculation)
 *   - User interaction sessions
 *
 * Each session = [first_point_time, last_point_time + max_gap]
 * ========================================================================= */

int historian_window_session(const historian_data_point_t *points,
                              size_t count, int64_t max_gap_ms,
                              historian_window_set_t *result)
{
    if (!points || !result) return -1;
    if (count == 0) return 0;

    size_t session_start_idx = 0;
    int session_num = 0;

    for (size_t i = 1; i <= count; i++) {
        /* Check if this point starts a new session */
        int new_session = 0;

        if (i == count) {
            /* End of data - close the last session */
            new_session = 1;
        } else {
            int64_t gap = points[i].timestamp.epoch_ms -
                           points[i-1].timestamp.epoch_ms;
            if (gap > max_gap_ms) {
                new_session = 1;
            }
        }

        if (new_session) {
            /* Close the current session (points [session_start_idx, i)) */
            historian_window_t win;
            memset(&win, 0, sizeof(win));
            win.start_time_ms = points[session_start_idx].timestamp.epoch_ms;
            win.end_time_ms = points[i-1].timestamp.epoch_ms;
            win.point_count = i - session_start_idx;
            win.aggregate_value = window_aggregate(points, session_start_idx, i);
            win.quality = window_worst_quality(points, session_start_idx, i);
            win.is_closed = 1;
            snprintf(win.label, sizeof(win.label), "Session_%d", session_num);

            historian_window_set_append(result, win);

            session_start_idx = i;
            session_num++;
        }
    }

    return 0;
}

/* =========================================================================
 * L2: Calendar Window
 *
 * Aligns windows to natural calendar boundaries.
 *
 * The alignment parameter shifts the boundary from midnight. For example,
 * align_ms = 6*3600*1000 (6 hours) means daily windows run from 06:00 to
 * 06:00 next day, which is common for production day alignment.
 * ========================================================================= */

int historian_window_calendar(const historian_data_point_t *points,
                               size_t count,
                               int64_t window_size_ms, int64_t align_ms,
                               historian_window_set_t *result)
{
    if (!points || !result) return -1;
    if (count == 0 || window_size_ms <= 0) return -2;

    int64_t first_ts = points[0].timestamp.epoch_ms;
    int64_t last_ts = points[count-1].timestamp.epoch_ms;

    /* Align first window start */
    int64_t aligned_start = ((first_ts - align_ms) / window_size_ms)
                             * window_size_ms + align_ms;
    if (aligned_start > first_ts) {
        aligned_start -= window_size_ms;
    }

    size_t point_idx = 0;
    int win_num = 0;

    for (int64_t win_start = aligned_start;
         win_start <= last_ts;
         win_start += window_size_ms, win_num++) {

        int64_t win_end = win_start + window_size_ms;
        if (win_end <= first_ts) continue;

        /* Gather points in this window */
        size_t win_point_start = point_idx;

        /* Advance point_idx to end of window */
        while (point_idx < count &&
               points[point_idx].timestamp.epoch_ms < win_end) {
            point_idx++;
        }
        size_t win_point_end = point_idx;

        size_t win_point_count = win_point_end - win_point_start;
        if (win_point_count == 0) continue;

        historian_window_t win;
        memset(&win, 0, sizeof(win));
        win.start_time_ms = win_start;
        win.end_time_ms = win_end;
        win.point_count = win_point_count;
        win.aggregate_value = window_aggregate(points, win_point_start, win_point_end);
        win.quality = window_worst_quality(points, win_point_start, win_point_end);
        win.is_closed = (win_end < last_ts) ? 1 : 0;
        snprintf(win.label, sizeof(win.label), "Calendar_%d", win_num);

        historian_window_set_append(result, win);
    }

    return 0;
}

/* =========================================================================
 * Gap Detection
 *
 * Detects intervals where no data was recorded for longer than max_gap_ms.
 * Each gap is returned as a window with start/end times and 0 point count.
 *
 * Gaps indicate: communication failure, sensor outage, system downtime,
 * data collection stoppage.
 * ========================================================================= */

int historian_detect_gaps(const historian_data_point_t *points,
                           size_t count, int64_t max_gap_ms,
                           historian_window_set_t *result)
{
    if (!points || !result) return -1;
    if (count < 2) return 0;

    int gap_num = 0;

    for (size_t i = 1; i < count; i++) {
        int64_t gap = points[i].timestamp.epoch_ms -
                       points[i-1].timestamp.epoch_ms;

        if (gap > max_gap_ms) {
            historian_window_t win;
            memset(&win, 0, sizeof(win));
            win.start_time_ms = points[i-1].timestamp.epoch_ms;
            win.end_time_ms = points[i].timestamp.epoch_ms;
            win.point_count = 0;
            win.aggregate_value = NAN;
            win.quality = HISTORIAN_QUAL_BAD | HISTORIAN_QUAL_SUB_COMM_FAIL;
            win.is_closed = 1;
            snprintf(win.label, sizeof(win.label), "Gap_%d_%lldms",
                     gap_num, (long long)gap);

            historian_window_set_append(result, win);
            gap_num++;
        }
    }

    return 0;
}

/* =========================================================================
 * Rate of Change Detection
 *
 * Detects intervals where the rate of change (derivative) exceeds a
 * threshold. Signals process upsets, ramp events, or anomalous behavior.
 *
 * ROC = |value_{i+1} - value_i| / (time_{i+1} - time_i) in seconds
 * ========================================================================= */

int historian_detect_rate_of_change(const historian_data_point_t *points,
                                     size_t count, double roc_threshold,
                                     historian_window_set_t *result)
{
    if (!points || !result) return -1;
    if (count < 2) return 0;

    int in_event = 0;
    size_t event_start = 0;
    int event_num = 0;

    for (size_t i = 0; i < count - 1; i++) {
        double dt_s = (double)(points[i+1].timestamp.epoch_ms -
                                points[i].timestamp.epoch_ms) / 1000.0;
        if (dt_s <= 0.0) continue;

        double roc = fabs(points[i+1].value - points[i].value) / dt_s;

        if (roc > roc_threshold) {
            if (!in_event) {
                /* Start new ROC event */
                in_event = 1;
                event_start = i;
            }
        } else {
            if (in_event) {
                /* Close ROC event */
                in_event = 0;
                historian_window_t win;
                memset(&win, 0, sizeof(win));
                win.start_time_ms = points[event_start].timestamp.epoch_ms;
                win.end_time_ms = points[i].timestamp.epoch_ms;
                win.point_count = i - event_start + 1;
                win.aggregate_value = roc; /* Max ROC in window */
                win.quality = HISTORIAN_QUAL_GOOD;
                win.is_closed = 1;
                snprintf(win.label, sizeof(win.label), "ROC_Event_%d", event_num);

                historian_window_set_append(result, win);
                event_num++;
            }
        }
    }

    /* Close any open event at end of data */
    if (in_event) {
        historian_window_t win;
        memset(&win, 0, sizeof(win));
        win.start_time_ms = points[event_start].timestamp.epoch_ms;
        win.end_time_ms = points[count-1].timestamp.epoch_ms;
        win.point_count = count - event_start;
        win.aggregate_value = roc_threshold;
        win.quality = HISTORIAN_QUAL_GOOD;
        win.is_closed = 1;
        snprintf(win.label, sizeof(win.label), "ROC_Event_%d", event_num);
        historian_window_set_append(result, win);
    }

    return 0;
}

/* =========================================================================
 * L3: SQL Window Function Analogs
 *
 * LAG(value, offset): For each row, returns the value of the row that is
 *   'offset' rows before the current row in the partition.
 *
 * LEAD(value, offset): For each row, returns the value of the row that is
 *   'offset' rows after the current row.
 *
 * These are fundamental building blocks for:
 *   - Computing value deltas: delta[i] = value[i] - LAG(value,1)[i]
 *   - Filling gaps: IFNULL(value[i], LAG(value,1)[i])
 *   - Detecting state changes: value[i] != LAG(value,1)[i]
 * ========================================================================= */

int historian_sql_lag(const historian_data_point_t *points, size_t count,
                       int lag_offset, double *lag_values)
{
    if (!points || !lag_values) return -1;
    if (lag_offset < 0) return -2;

    for (size_t i = 0; i < count; i++) {
        if (i < (size_t)lag_offset) {
            lag_values[i] = NAN;
        } else {
            lag_values[i] = points[i - lag_offset].value;
        }
    }
    return 0;
}

int historian_sql_lead(const historian_data_point_t *points, size_t count,
                        int lead_offset, double *lead_values)
{
    if (!points || !lead_values) return -1;
    if (lead_offset < 0) return -2;

    for (size_t i = 0; i < count; i++) {
        if (i + (size_t)lead_offset >= count) {
            lead_values[i] = NAN;
        } else {
            lead_values[i] = points[i + lead_offset].value;
        }
    }
    return 0;
}

/* =========================================================================
 * Generic Window Partition Dispatcher
 * ========================================================================= */

int historian_window_partition(const historian_data_point_t *points,
                                size_t count,
                                const historian_window_spec_t *spec,
                                historian_window_set_t *result)
{
    if (!points || !spec || !result) return -1;

    switch (spec->type) {
    case HISTORIAN_WINDOW_TUMBLING:
        return historian_window_tumbling(points, count,
                                          spec->window_size_ms, result);
    case HISTORIAN_WINDOW_SLIDING:
        return historian_window_sliding(points, count,
                                         spec->window_size_ms,
                                         spec->window_slide_ms, result);
    case HISTORIAN_WINDOW_SESSION:
        return historian_window_session(points, count,
                                         spec->session_max_gap_ms, result);
    case HISTORIAN_WINDOW_CALENDAR:
        return historian_window_calendar(points, count,
                                          spec->window_size_ms,
                                          spec->calendar_offset_ms, result);
    default:
        return -99;
    }
}
