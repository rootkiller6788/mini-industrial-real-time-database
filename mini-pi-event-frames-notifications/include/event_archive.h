/**
 * @file event_archive.h
 * @brief Event Frame archival and retrieval - time-indexed persistent storage
 *
 * Manages lifecycle beyond active set: archival to persistent storage,
 * time-range queries, retention policies, and statistical aggregation.
 *
 * Knowledge Levels:
 *   L1 Definitions:  Archive record, retention policy, query context
 *   L2 Core Concepts: Write-ahead logging, time-partitioned indexing
 *   L3 Engineering:   B-tree over time ranges, snapshot isolation
 *   L4 Standards:     ISA-106 section 6 Event data retention
 *   L5 Algorithms:    Binary search time index, compaction, statistics
 *   L6 Canonical:     Compliance audit trail, production loss aggregation
 *
 * CMU 24-677 - Time-series archival architecture
 * Purdue ME 575 - Industrial data retention
 */

#ifndef EVENT_ARCHIVE_H
#define EVENT_ARCHIVE_H

#include "event_frame.h"
#include <stdint.h>
#include <stddef.h>
#include <time.h>

typedef enum {
    ARCH_STATUS_ACTIVE      = 0,
    ARCH_STATUS_COMMITTED   = 1,
    ARCH_STATUS_COMPACTED   = 2,
    ARCH_STATUS_RETIRED     = 3,
    ARCH_STATUS_CORRUPT     = 4
} arch_status_t;

typedef struct {
    ef_guid_t    event_id;
    arch_status_t status;
    char         serialized_data[4096];
    size_t       data_size;
    uint32_t     checksum;
    time_t       archived_at;
    time_t       event_start_time;
    time_t       event_end_time;
    char         template_name[128];
    ef_severity_t severity;
    int          is_deleted;
} arch_record_t;

typedef enum {
    RETENTION_KEEP_FOREVER   = 0,
    RETENTION_AGE_DAYS       = 1,
    RETENTION_MAX_COUNT      = 2,
    RETENTION_UNTIL_COMPLIANCE = 3,
    RETENTION_CUSTOM         = 4
} retention_type_t;

typedef struct {
    retention_type_t type;
    int              value;
    int              min_severity;
    char             template_filter[128];
} retention_policy_t;

typedef struct {
    time_t         partition_start;
    time_t         partition_end;
    arch_record_t  *records;
    int            record_count;
    int            record_capacity;
    uint64_t       total_size_bytes;
    int            is_closed;
    int            is_compacted;
    char           filename[256];
} arch_partition_t;

#define ARCH_MAX_PARTITIONS    1024

typedef struct {
    arch_partition_t    partitions[ARCH_MAX_PARTITIONS];
    int                 partition_count;
    retention_policy_t  policy;
    uint64_t            total_events_archived;
    uint64_t            total_events_retired;
    uint64_t            total_bytes_stored;
    time_t              created_at;
    time_t              last_compaction_at;
    char                storage_path[512];
} event_archive_t;

#define ARCH_QUERY_MAX_RESULTS  1024

typedef struct {
    time_t         time_start;
    time_t         time_end;
    char           template_filter[128];
    ef_severity_t  min_severity;
    ef_severity_t  max_severity;
    int            include_deleted;
    int            max_results;
    int            offset;
} arch_query_t;

typedef struct {
    time_t         window_start;
    time_t         window_end;
    int            total_events;
    double         avg_duration_s;
    double         min_duration_s;
    double         max_duration_s;
    int            count_by_severity[7];
    int            unique_templates;
    double         events_per_hour;
} arch_stats_t;

int arch_init(event_archive_t *archive, const char *storage_path);
int arch_store(event_archive_t *archive, const event_frame_t *ef);
int arch_query(const event_archive_t *archive, const arch_query_t *query, event_frame_t **results);
arch_record_t *arch_find_by_id(const event_archive_t *archive, const ef_guid_t event_id);
int arch_set_retention_policy(event_archive_t *archive, const retention_policy_t *policy);
int arch_enforce_retention(event_archive_t *archive, time_t now);
int arch_compute_stats(const event_archive_t *archive, time_t t_start, time_t t_end, arch_stats_t *stats);
int arch_compact_partition(event_archive_t *archive, int partition_idx);
int arch_utilization(const event_archive_t *archive,
                     uint64_t *total_events, uint64_t *total_bytes,
                     time_t *oldest_event, time_t *newest_event);

#endif
