/** pi_snapshot.h - PI Snapshot Subsystem */
#ifndef PI_SNAPSHOT_H
#define PI_SNAPSHOT_H
#include "pi_da_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PI_SNAPSHOT_MAX_POINTS 1000000
#define PI_SNAPSHOT_HASH_BUCKETS 65537
typedef struct {
    pi_snapshot_entry_t *buckets[PI_SNAPSHOT_HASH_BUCKETS];
    int32_t bucket_counts[PI_SNAPSHOT_HASH_BUCKETS];
    int32_t total_entries;
    int64_t update_count, hit_count, miss_count;
    int64_t exception_passed, exception_rejected;
    pi_timestamp_t last_update;
} pi_snapshot_t;
void pi_snapshot_init(pi_snapshot_t *snap);
void pi_snapshot_destroy(pi_snapshot_t *snap);
int pi_snapshot_put(pi_snapshot_t *snap, int32_t point_id, const pi_value_t *value);
const pi_snapshot_entry_t* pi_snapshot_get(const pi_snapshot_t *snap, int32_t point_id);
int pi_snapshot_contains(const pi_snapshot_t *snap, int32_t point_id);
int pi_snapshot_remove(pi_snapshot_t *snap, int32_t point_id);
int pi_snapshot_exception_test(const pi_snapshot_t *snap, int32_t point_id, const pi_value_t *new_value, double exc_dev);
int32_t pi_snapshot_size(const pi_snapshot_t *snap);
double pi_snapshot_load_factor(const pi_snapshot_t *snap);
int64_t pi_snapshot_hit_ratio(const pi_snapshot_t *snap);
double pi_snapshot_exception_rate(const pi_snapshot_t *snap);
typedef int (*pi_snapshot_iter_cb)(const pi_snapshot_entry_t *entry, void *ctx);
int pi_snapshot_foreach(const pi_snapshot_t *snap, pi_snapshot_iter_cb cb, void *ctx);
void pi_snapshot_clear(pi_snapshot_t *snap);
int64_t pi_snapshot_memory_estimate(const pi_snapshot_t *snap);
#ifdef __cplusplus
}
#endif
#endif
