/**
 * pi_snapshot.c - PI Snapshot Subsystem Implementation
 * In-memory current-value cache using direct PointID indexing.
 * Knowledge: L1-L5  Stanford ENGR205  Purdue ME 575  RWTH Aachen
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/pi_snapshot.h"

static pi_snapshot_entry_t g_entries[PI_SNAPSHOT_MAX_POINTS];
static int32_t g_entry_valid[PI_SNAPSHOT_MAX_POINTS];

void pi_snapshot_init(pi_snapshot_t *snap) {
    if (!snap) return;
    memset(snap, 0, sizeof(*snap));
    memset(g_entries, 0, sizeof(g_entries));
    memset(g_entry_valid, 0, sizeof(g_entry_valid));
    snap->last_update = PI_TIME_EMPTY;
}

void pi_snapshot_destroy(pi_snapshot_t *snap) {
    if (!snap) return;
    pi_snapshot_clear(snap);
}

int pi_snapshot_put(pi_snapshot_t *snap, int32_t point_id,
                    const pi_value_t *value) {
    if (!snap || !value || point_id <= 0 || point_id >= PI_SNAPSHOT_MAX_POINTS)
        return -1;
    pi_snapshot_entry_t *e = &g_entries[point_id];
    int is_new = !g_entry_valid[point_id];
    e->point_id = point_id;
    memcpy(&e->current_value, value, sizeof(pi_value_t));
    pi_timestamp_now(&e->snapshot_time);
    if (is_new) { e->event_count = 1; snap->total_entries++; g_entry_valid[point_id] = 1; }
    else { e->event_count++; }
    snap->update_count++;
    snap->last_update = e->snapshot_time;
    return 0;
}

const pi_snapshot_entry_t* pi_snapshot_get(const pi_snapshot_t *snap,
                                            int32_t point_id) {
    if (!snap || point_id <= 0 || point_id >= PI_SNAPSHOT_MAX_POINTS)
        return NULL;
    if (!g_entry_valid[point_id]) { return NULL; }
    return &g_entries[point_id];
}

int pi_snapshot_contains(const pi_snapshot_t *snap, int32_t point_id) {
    if (!snap || point_id <= 0 || point_id >= PI_SNAPSHOT_MAX_POINTS)
        return 0;
    return g_entry_valid[point_id];
}

int pi_snapshot_remove(pi_snapshot_t *snap, int32_t point_id) {
    if (!snap || point_id <= 0 || point_id >= PI_SNAPSHOT_MAX_POINTS)
        return -1;
    if (!g_entry_valid[point_id]) return -1;
    memset(&g_entries[point_id], 0, sizeof(pi_snapshot_entry_t));
    g_entry_valid[point_id] = 0;
    snap->total_entries--;
    return 0;
}

/* Exception test: compare new value against snapshot.
 * Returns 1 if value passes (should be archived), 0 if suppressed. */
int pi_snapshot_exception_test(const pi_snapshot_t *snap,
    int32_t point_id, const pi_value_t *new_value, double exc_dev) {
    if (!snap || !new_value || point_id <= 0)
        return 1;
    if (!g_entry_valid[point_id]) return 1;
    const pi_snapshot_entry_t *cur = &g_entries[point_id];
    const pi_value_t *cv = &cur->current_value;
    if (new_value->value_type != cv->value_type) return 1;
    if (exc_dev <= 0.0) return 1;
    int passed = 0;
    switch (new_value->value_type) {
        case PI_POINT_FLOAT64:
            passed = fabs(new_value->value.as_float64 - cv->value.as_float64) > exc_dev;
            break;
        case PI_POINT_FLOAT32:
            passed = fabs((double)new_value->value.as_float32 - (double)cv->value.as_float32) > exc_dev;
            break;
        case PI_POINT_INT32:
            passed = abs(new_value->value.as_int32 - cv->value.as_int32) > (int32_t)exc_dev;
            break;
        case PI_POINT_DIGITAL:
            passed = (new_value->value.as_digital != cv->value.as_digital);
            break;
        case PI_POINT_STRING:
            passed = (strcmp(new_value->value.as_string, cv->value.as_string) != 0);
            break;
        default: passed = 1; break;
    }
    /* Counters not incremented in const-context function */
    return passed;
}

int32_t pi_snapshot_size(const pi_snapshot_t *snap) {
    return snap ? snap->total_entries : 0;
}

double pi_snapshot_load_factor(const pi_snapshot_t *snap) {
    if (!snap) return 0.0;
    return (double)snap->total_entries / (double)PI_SNAPSHOT_MAX_POINTS;
}

int64_t pi_snapshot_hit_ratio(const pi_snapshot_t *snap) {
    if (!snap) return 0;
    int64_t total = snap->hit_count + snap->miss_count;
    return total > 0 ? (snap->hit_count * 100) / total : 0;
}

double pi_snapshot_exception_rate(const pi_snapshot_t *snap) {
    if (!snap) return 0.0;
    int64_t total = snap->exception_passed + snap->exception_rejected;
    return total > 0 ? (double)snap->exception_passed / (double)total : 0.0;
}

int pi_snapshot_foreach(const pi_snapshot_t *snap, pi_snapshot_iter_cb cb, void *ctx) {
    if (!snap || !cb) return 0;
    int i, count = 0;
    for (i = 1; i < PI_SNAPSHOT_MAX_POINTS && count < snap->total_entries; i++) {
        if (g_entry_valid[i]) {
            if (cb(&g_entries[i], ctx) != 0) break;
            count++;
        }
    }
    return count;
}

void pi_snapshot_clear(pi_snapshot_t *snap) {
    if (!snap) return;
    memset(g_entries, 0, sizeof(g_entries));
    memset(g_entry_valid, 0, sizeof(g_entry_valid));
    snap->total_entries = 0;
}

int64_t pi_snapshot_memory_estimate(const pi_snapshot_t *snap) {
    if (!snap) return 0;
    return (int64_t)snap->total_entries * (int64_t)sizeof(pi_snapshot_entry_t)
           + (int64_t)sizeof(pi_snapshot_t);
}

/* ─── Snapshot Differential ──────────────────────────────────── */
int pi_snapshot_diff(const pi_snapshot_t *snap, int32_t point_id1,
                      int32_t point_id2, double *out_diff) {
    if (!snap || !out_diff) return -1;
    const pi_snapshot_entry_t *e1 = pi_snapshot_get(snap, point_id1);
    const pi_snapshot_entry_t *e2 = pi_snapshot_get(snap, point_id2);
    if (!e1 || !e2) return -2;
    double v1 = pi_value_get_float64(&e1->current_value);
    double v2 = pi_value_get_float64(&e2->current_value);
    *out_diff = v1 - v2;
    return 0;
}

int pi_snapshot_get_value_as_float(const pi_snapshot_t *snap, int32_t point_id, double *out) {
    if (!snap || !out) return -1;
    const pi_snapshot_entry_t *e = pi_snapshot_get(snap, point_id);
    if (!e) return -2;
    *out = pi_value_get_float64(&e->current_value);
    return 0;
}

int pi_snapshot_get_value_as_int(const pi_snapshot_t *snap, int32_t point_id, int32_t *out) {
    if (!snap || !out) return -1;
    const pi_snapshot_entry_t *e = pi_snapshot_get(snap, point_id);
    if (!e) return -2;
    *out = pi_value_get_int32(&e->current_value);
    return 0;
}

int pi_snapshot_get_value_as_digital(const pi_snapshot_t *snap, int32_t point_id, int32_t *out) {
    if (!snap || !out) return -1;
    const pi_snapshot_entry_t *e = pi_snapshot_get(snap, point_id);
    if (!e) return -2;
    *out = pi_value_get_digital(&e->current_value);
    return 0;
}

/* ─── Snapshot Value Change Detection ─────────────────────────── */
int pi_snapshot_has_changed(const pi_snapshot_t *snap, int32_t point_id, const pi_value_t *compare) {
    if (!snap || !compare) return 0;
    const pi_snapshot_entry_t *e = pi_snapshot_get(snap, point_id);
    if (!e) return 0;
    /* Compare current value against reference using memcmp */
    return memcmp(&e->current_value, compare, sizeof(pi_value_t)) != 0 ? 1 : 0;
}
