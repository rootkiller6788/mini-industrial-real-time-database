/**
 * pi_archive.c - PI Data Archive Storage Subsystem
 * Implements historical time-series storage with swinging door compression.
 * Knowledge: L1-L5  MIT 6.302  Stanford ENGR205  RWTH Aachen
 * Key: Swinging Door Compression (Bristol, 1990)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "../include/pi_archive.h"

#define ARCHIVE_STORE_MAX 100000
static pi_archive_event_t g_events[ARCHIVE_STORE_MAX];
static int32_t g_event_count = 0;

void pi_archive_init(pi_archive_t *archive) {
    if (!archive) return;
    memset(archive, 0, sizeof(*archive));
    archive->num_files = 1;
    archive->primary_file_index = 0;
    archive->files[0].file_index = 0;
    archive->files[0].primary = 1;
    archive->initialized = 1;
    g_event_count = 0;
    memset(g_events, 0, sizeof(g_events));
}

void pi_archive_destroy(pi_archive_t *archive) {
    if (!archive) return;
    memset(archive, 0, sizeof(*archive));
    g_event_count = 0;
}

int pi_archive_store_event(pi_archive_t *archive, const pi_archive_event_t *event) {
    if (!archive || !event) return -1;
    if (g_event_count >= ARCHIVE_STORE_MAX) return -1;
    memcpy(&g_events[g_event_count], event, sizeof(pi_archive_event_t));
    g_events[g_event_count].archive_recno = g_event_count;
    g_event_count++;
    archive->total_events_stored++;
    int64_t ts_raw = event->timestamp.seconds;
    if (ts_raw < archive->oldest_event_time || archive->oldest_event_time == 0)
        archive->oldest_event_time = ts_raw;
    if (ts_raw > archive->newest_event_time)
        archive->newest_event_time = ts_raw;
    return 0;
}

int pi_archive_store_events_batch(pi_archive_t *archive,
                                   const pi_archive_event_t *events, int n) {
    if (!archive || !events || n <= 0) return 0;
    int i, stored = 0;
    for (i = 0; i < n; i++) {
        if (pi_archive_store_event(archive, &events[i]) == 0) stored++;
        else break;
    }
    return stored;
}

int pi_archive_get_events_range(const pi_archive_t *archive, int32_t point_id,
    pi_timestamp_t start_time, pi_timestamp_t end_time,
    int max_events, pi_archive_event_t *out_events) {
    if (!archive || !out_events || max_events <= 0) return 0;
    int count = 0, i;
    (void)point_id;
    for (i = 0; i < g_event_count && count < max_events; i++) {
        if (pi_timestamp_compare(&g_events[i].timestamp, &start_time) >= 0 &&
            pi_timestamp_compare(&g_events[i].timestamp, &end_time) <= 0) {
            memcpy(&out_events[count], &g_events[i], sizeof(pi_archive_event_t));
            count++;
        }
    }
    return count;
}

int pi_archive_get_interpolated(const pi_archive_t *archive, int32_t point_id,
    pi_timestamp_t at_time, double *out_value) {
    if (!archive || !out_value) return 0;
    (void)point_id;
    pi_archive_event_t before, after;
    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));
    int found_before = 0, found_after = 0, i;
    for (i = 0; i < g_event_count; i++) {
        int cmp = pi_timestamp_compare(&g_events[i].timestamp, &at_time);
        if (cmp <= 0) {
            memcpy(&before, &g_events[i], sizeof(before));
            found_before = 1;
        }
        if (cmp >= 0 && !found_after) {
            memcpy(&after, &g_events[i], sizeof(after));
            found_after = 1;
        }
    }
    if (found_before && found_after) {
        double dt = pi_timestamp_diff_seconds(&before.timestamp, &after.timestamp);
        double dt_at = pi_timestamp_diff_seconds(&before.timestamp, &at_time);
        if (dt > 0.0) {
            double v0 = pi_value_get_float64(&before.value);
            double v1 = pi_value_get_float64(&after.value);
            *out_value = v0 + (v1 - v0) * dt_at / dt;
            return 1;
        }
    }
    if (found_before) {
        *out_value = pi_value_get_float64(&before.value);
        return 1;
    }
    return 0;
}

/* ─── Summary Statistics ──────────────────────────────────────────── */
double pi_archive_get_summary(const pi_archive_t *archive, int32_t point_id,
    pi_timestamp_t start_time, pi_timestamp_t end_time, pi_summary_type_t summary_type) {
    if (!archive) return 0.0;
    (void)point_id;
    double sum = 0.0, min_val = 1e308, max_val = -1e308, sum_sq = 0.0;
    int count = 0, i;
    for (i = 0; i < g_event_count; i++) {
        if (pi_timestamp_compare(&g_events[i].timestamp, &start_time) < 0) continue;
        if (pi_timestamp_compare(&g_events[i].timestamp, &end_time) > 0) break;
        double v = pi_value_get_float64(&g_events[i].value);
        sum += v; sum_sq += v * v; count++;
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
    }
    if (count == 0) return 0.0;
    switch (summary_type) {
        case PI_SUMMARY_AVERAGE: return sum / (double)count;
        case PI_SUMMARY_MIN:     return min_val;
        case PI_SUMMARY_MAX:     return max_val;
        case PI_SUMMARY_COUNT:   return (double)count;
        case PI_SUMMARY_TOTAL:   return sum;
        case PI_SUMMARY_RANGE:   return max_val - min_val;
        case PI_SUMMARY_STDDEV:  return sqrt((sum_sq - sum*sum/(double)count)/(double)(count>1?count-1:1));
        default: return 0.0;
    }
}

/* ─── Swinging Door Compression (Bristol, 1990) ─────────────────────── */
/** Classic swinging door trending algorithm used by OSIsoft PI.
 *  Determines if an incoming value should be archived.
 *  The algorithm maintains upper/lower "doors" (slope bounds) from the last
 *  archived point. When doors cross, the intermediate value is archived.
 *  Reference: E.H. Bristol, ISA National Conference, 1990. */
static int swinging_door_test(double last_t, double last_v,
                               double curr_t, double curr_v,
                               double new_t, double new_v,
                               double comp_dev,
                               double *slope_upper, double *slope_lower) {
    (void)curr_v; /* curr_v is not used in slope test */
    if (fabs(new_t - last_t) < 1e-9)
        return (fabs(new_v - last_v) > comp_dev) ? 1 : 0;
    double dt = new_t - last_t;
    double s_up = (new_v + comp_dev - last_v) / dt;
    double s_lo = (new_v - comp_dev - last_v) / dt;
    if (curr_t > last_t) {
        if (s_up < *slope_upper) *slope_upper = s_up;
        if (s_lo > *slope_lower) *slope_lower = s_lo;
        if (*slope_upper < *slope_lower) {
            *slope_upper = 1e308;
            *slope_lower = -1e308;
            return 1;
        }
        return 0;
    }
    *slope_upper = s_up;
    *slope_lower = s_lo;
    return 0;
}

/* Full compression pipeline: exception test + swinging door */
int pi_archive_compression_test(double last_t, double last_v,
    double curr_t, double curr_v, double new_t, double new_v,
    double exc_dev, double comp_dev,
    double *slope_upper, double *slope_lower) {
    if (exc_dev > 0.0 && fabs(new_v - curr_v) <= exc_dev) return 0;
    if (comp_dev > 0.0)
        return swinging_door_test(last_t, last_v, curr_t, curr_v,
            new_t, new_v, comp_dev, slope_upper, slope_lower);
    return 1;
}

/* ─── Archive Management ──────────────────────────────────────────── */
int pi_archive_shift(pi_archive_t *archive) {
    if (!archive || archive->num_files >= PI_ARCHIVE_MAX_FILES) return -1;
    int ni = archive->num_files;
    archive->files[ni].file_index = ni;
    archive->files[ni].primary = 1;
    archive->files[archive->primary_file_index].primary = 0;
    archive->primary_file_index = ni;
    archive->num_files++;
    return 0;
}

int pi_archive_get_file_info(const pi_archive_t *archive, int fi, pi_archive_file_t *info) {
    if (!archive || !info || fi < 0 || fi >= archive->num_files) return -1;
    memcpy(info, &archive->files[fi], sizeof(pi_archive_file_t));
    return 0;
}
int64_t pi_archive_oldest_event(const pi_archive_t *archive) {
    return archive ? archive->oldest_event_time : 0;
}
int64_t pi_archive_newest_event(const pi_archive_t *archive) {
    return archive ? archive->newest_event_time : 0;
}
int64_t pi_archive_total_events(const pi_archive_t *archive) {
    return archive ? archive->total_events_stored : 0;
}

/* ─── Archive Record Count per Point ─────────────────────────────── */
int pi_archive_point_event_count(const pi_archive_t *archive, int32_t point_id) {
    if (!archive) return 0;
    (void)point_id;
    /* Return total event count - per-point index not maintained */
    return g_event_count;
}

/* ─── Archive File Size Estimation ───────────────────────────────── */
/**(PI3.4+ uses ~24 bytes/record. With annotations: ~32 bytes.) */
int64_t pi_archive_estimated_file_size(const pi_archive_t *archive) {
    if (!archive) return 0;
    return archive->total_events_stored * PI_ARCHIVE_RECORD_SIZE;
}

/* ─── Insert events for testing ──────────────────────────────────── */
int pi_archive_insert_test_events(pi_archive_t *archive, int32_t count) {
    if (!archive || count <= 0) return 0;
    int i;
    for (i = 0; i < count && g_event_count < ARCHIVE_STORE_MAX; i++) {
        pi_archive_event_t ev; memset(&ev, 0, sizeof(ev));
        ev.timestamp.seconds = (int64_t)(time(NULL) - (count - i));
        ev.timestamp.subsec = 0;
        pi_value_set_float64(&ev.value, (double)i * 1.5, ev.timestamp);
        ev.annotated = 0; ev.questionable = 0;
        pi_archive_store_event(archive, &ev);
    }
    return i;
}

/* ─── Archive Search by Value ─────────────────────────────────── */
int pi_archive_find_value(const pi_archive_t *archive, double target, double tolerance, pi_archive_event_t *out, int max_out) {
    if (!archive || !out || max_out <= 0) return 0;
    int i, count = 0;
    for (i = 0; i < g_event_count && count < max_out; i++) {
        double v = pi_value_get_float64(&g_events[i].value);
        if (fabs(v - target) <= tolerance) memcpy(&out[count++], &g_events[i], sizeof(pi_archive_event_t));
    }
    return count;
}
