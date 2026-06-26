/** pi_archive.h - PI Archive Storage Subsystem */
#ifndef PI_ARCHIVE_H
#define PI_ARCHIVE_H
#include "pi_da_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PI_ARCHIVE_MAX_EVENTS_PER_FILE 1000000
#define PI_ARCHIVE_MAX_FILES 256
#define PI_ARCHIVE_RECORD_SIZE 24
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
void pi_archive_init(pi_archive_t *archive);
void pi_archive_destroy(pi_archive_t *archive);
int pi_archive_store_event(pi_archive_t *archive, const pi_archive_event_t *event);
int pi_archive_store_events_batch(pi_archive_t *archive, const pi_archive_event_t *events, int n);
int pi_archive_get_events_range(const pi_archive_t *archive, int32_t point_id, pi_timestamp_t start, pi_timestamp_t end, int max_events, pi_archive_event_t *out);
int pi_archive_get_interpolated(const pi_archive_t *archive, int32_t point_id, pi_timestamp_t at_time, double *out_value);
double pi_archive_get_summary(const pi_archive_t *archive, int32_t point_id, pi_timestamp_t start, pi_timestamp_t end, pi_summary_type_t type);
int pi_archive_shift(pi_archive_t *archive);
int pi_archive_get_file_info(const pi_archive_t *archive, int file_index, pi_archive_file_t *info);
int64_t pi_archive_oldest_event(const pi_archive_t *archive);
int64_t pi_archive_newest_event(const pi_archive_t *archive);
int64_t pi_archive_total_events(const pi_archive_t *archive);
#ifdef __cplusplus
}
#endif
#endif
