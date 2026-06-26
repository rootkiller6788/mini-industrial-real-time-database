import os
base = os.path.dirname(os.path.abspath(__file__))
files = {}


files["include/pi_snapshot.h"] = """/**
 * pi_snapshot.h - PI Snapshot Subsystem
 * In-memory current-value cache. O(1) lookup per point.
 */
#ifndef PI_SNAPSHOT_H
#define PI_SNAPSHOT_H
#include "pi_da_types.h"
#ifdef __cplusplus
extern "C" {
#endif

#define PI_SNAPSHOT_MAX_POINTS   1000000
#define PI_SNAPSHOT_HASH_BUCKETS 65537

typedef struct {
    pi_snapshot_entry_t *buckets[PI_SNAPSHOT_HASH_BUCKETS];
    int32_t bucket_counts[PI_SNAPSHOT_HASH_BUCKETS];
    int32_t total_entries;
    int64_t update_count, hit_count, miss_count;
    int64_t exception_passed, exception_rejected;
    pi_timestamp_t last_update;
} pi_snapshot_t;

void  pi_snapshot_init(pi_snapshot_t *snap);
void  pi_snapshot_destroy(pi_snapshot_t *snap);
int   pi_snapshot_put(pi_snapshot_t *snap, int32_t point_id, const pi_value_t *value);
const pi_snapshot_entry_t* pi_snapshot_get(const pi_snapshot_t *snap, int32_t point_id);
int   pi_snapshot_contains(const pi_snapshot_t *snap, int32_t point_id);
int   pi_snapshot_remove(pi_snapshot_t *snap, int32_t point_id);
int   pi_snapshot_exception_test(const pi_snapshot_t *snap, int32_t point_id, const pi_value_t *new_value, double exc_dev);
int32_t pi_snapshot_size(const pi_snapshot_t *snap);
double  pi_snapshot_load_factor(const pi_snapshot_t *snap);
int64_t pi_snapshot_hit_ratio(const pi_snapshot_t *snap);
double  pi_snapshot_exception_rate(const pi_snapshot_t *snap);
typedef int (*pi_snapshot_iter_cb)(const pi_snapshot_entry_t *entry, void *ctx);
int   pi_snapshot_foreach(const pi_snapshot_t *snap, pi_snapshot_iter_cb cb, void *ctx);
void  pi_snapshot_clear(pi_snapshot_t *snap);
int64_t pi_snapshot_memory_estimate(const pi_snapshot_t *snap);
#ifdef __cplusplus
}
#endif
#endif
"""

files["include/pi_archive.h"] = """/** pi_archive.h - PI Archive Storage Subsystem */
#ifndef PI_ARCHIVE_H
#define PI_ARCHIVE_H
#include "pi_da_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PI_ARCHIVE_MAX_EVENTS_PER_FILE 1000000
#define PI_ARCHIVE_MAX_FILES  256
#define PI_ARCHIVE_CACHE_SIZE 10000
typedef struct __attribute__((packed)) {
    int32_t point_id; int64_t timestamp_raw; int16_t status; int16_t flags; int64_t value_raw;
} pi_archive_record_t;
typedef struct {
    int32_t point_id; int64_t start_time; int64_t end_time;
    int32_t file_index; int64_t file_offset; int32_t record_count;
} pi_archive_index_entry_t;
typedef struct {
    char filename[256]; int32_t file_index; int64_t start_time, end_time;
    int32_t record_count; int64_t file_size_bytes; int32_t primary; double compression_ratio;
} pi_archive_file_t;
typedef struct {
    pi_archive_record_t entries[PI_ARCHIVE_CACHE_SIZE];
    int64_t access_time[PI_ARCHIVE_CACHE_SIZE];
    int32_t cache_size; int64_t hits, misses;
} pi_archive_cache_t;
typedef struct {
    pi_archive_file_t files[PI_ARCHIVE_MAX_FILES]; int32_t num_files, primary_file_index;
    pi_archive_cache_t cache;
    int64_t total_events_stored, total_events_read, total_bytes_written;
    int64_t oldest_event_time, newest_event_time;
    double overall_compression_ratio; int32_t initialized;
} pi_archive_t;
typedef enum { PI_SUMMARY_AVERAGE=0, PI_SUMMARY_MIN=1, PI_SUMMARY_MAX=2,
    PI_SUMMARY_STDDEV=3, PI_SUMMARY_COUNT=4, PI_SUMMARY_TOTAL=5, PI_SUMMARY_RANGE=6
} pi_summary_type_t;
void  pi_archive_init(pi_archive_t *archive);
void  pi_archive_destroy(pi_archive_t *archive);
int   pi_archive_store_event(pi_archive_t *archive, const pi_archive_event_t *event);
int   pi_archive_store_events_batch(pi_archive_t *archive, const pi_archive_event_t *events, int n);
int   pi_archive_get_events_range(const pi_archive_t *archive, int32_t point_id,
    pi_timestamp_t start, pi_timestamp_t end, int max_events, pi_archive_event_t *out);
int   pi_archive_get_interpolated(const pi_archive_t *archive, int32_t point_id,
    pi_timestamp_t at_time, double *out_value);
double pi_archive_get_summary(const pi_archive_t *archive, int32_t point_id,
    pi_timestamp_t start, pi_timestamp_t end, pi_summary_type_t type);
int   pi_archive_shift(pi_archive_t *archive);
int   pi_archive_get_file_info(const pi_archive_t *archive, int file_index, pi_archive_file_t *info);
int64_t pi_archive_oldest_event(const pi_archive_t *archive);
int64_t pi_archive_newest_event(const pi_archive_t *archive);
int64_t pi_archive_total_events(const pi_archive_t *archive);
#ifdef __cplusplus
}
#endif
#endif
"""


files["include/pi_point_db.h"] = """/** pi_point_db.h - PI Point Database Management */
#ifndef PI_POINT_DB_H
#define PI_POINT_DB_H
#include "pi_da_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PI_POINT_DB_MAX_POINTS 1000000
#define PI_POINT_DB_MAX_DIGITAL_SETS 256
#define PI_POINT_DB_MAX_STATES_PER_SET 32
typedef struct {
    char set_name[32]; int32_t num_states;
    pi_digital_state_t states[PI_POINT_DB_MAX_STATES_PER_SET];
} pi_digital_state_set_t;
typedef struct {
    pi_point_attributes_t points[PI_POINT_DB_MAX_POINTS];
    int32_t point_count, next_point_id;
    int32_t tag_index[PI_POINT_DB_MAX_POINTS]; int32_t tag_index_count;
    pi_digital_state_set_t digital_sets[PI_POINT_DB_MAX_DIGITAL_SETS]; int32_t digital_set_count;
    int64_t point_creates, point_deletes, attribute_edits;
    pi_timestamp_t last_modified;
} pi_point_db_t;
void  pi_point_db_init(pi_point_db_t *db);
void  pi_point_db_destroy(pi_point_db_t *db);
int   pi_point_db_create(pi_point_db_t *db, const pi_point_attributes_t *attrs, int32_t *out_id);
int   pi_point_db_delete(pi_point_db_t *db, int32_t point_id);
int   pi_point_db_update(pi_point_db_t *db, int32_t point_id, const pi_point_attributes_t *attrs);
const pi_point_attributes_t* pi_point_db_get_by_id(const pi_point_db_t *db, int32_t point_id);
const pi_point_attributes_t* pi_point_db_get_by_tag(const pi_point_db_t *db, const char *tag);
int32_t pi_point_db_get_id_by_tag(const pi_point_db_t *db, const char *tag);
void  pi_point_db_rebuild_tag_index(pi_point_db_t *db);
int   pi_point_db_find_by_location(const pi_point_db_t *db, const char *loc, int32_t *ids, int max);
int   pi_point_db_find_by_source(const pi_point_db_t *db, const char *src, int32_t *ids, int max);
int   pi_point_db_list_all(const pi_point_db_t *db, int32_t *ids, int max);
int   pi_point_db_add_digital_set(pi_point_db_t *db, const pi_digital_state_set_t *set);
const pi_digital_state_set_t* pi_point_db_get_digital_set(const pi_point_db_t *db, const char *name);
int   pi_point_db_find_digital_state(const pi_point_db_t *db, const char *set, int32_t code, char *out, int nmax);
int   pi_point_db_validate_attrs(const pi_point_attributes_t *attrs);
int   pi_point_db_validate_tag(const char *tag);
int32_t pi_point_db_count(const pi_point_db_t *db);
int32_t pi_point_db_next_available_id(const pi_point_db_t *db);
#ifdef __cplusplus
}
#endif
#endif
"""

