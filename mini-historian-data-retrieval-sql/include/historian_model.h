#ifndef HISTORIAN_MODEL_H
#define HISTORIAN_MODEL_H

/**
 * @file    historian_model.h
 * @brief   Core data structures for industrial time-series historian.
 *
 * Implements the foundational model for PI System, Honeywell PHD,
 * AspenTech IP.21, and OSIsoft PI Asset Framework data representation.
 *
 * Knowledge Coverage:
 *   L1: Tag metadata, timestamp representation, data quality flags
 *   L3: Historian schema design (wide vs narrow table mapping)
 *   L4: OPC HDA quality semantics, ISA-88 batch context fields
 *   L7: OSIsoft PI tag conventions, Honeywell PHD field mapping
 *
 * References:
 *   - OSIsoft PI Server Reference (2020)
 *   - OPC HDA Specification v1.20
 *   - ISA-88 Batch Control Standard
 */

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/*---------------------------------------------------------------------------
 * L1: Tag Metadata - Core definition of an industrial measurement point
 *
 * In industrial historians, every measurement is identified by a "tag".
 * Tags carry metadata: engineering units, data type, measuring range,
 * instrument range, and description. This is the fundamental entity.
 *
 * OSIsoft PI: Point Class
 * Honeywell PHD: Tag Definition
 * AspenTech IP.21: Record Definition
 *---------------------------------------------------------------------------*/

/** Data type of a historian tag value. */
typedef enum {
    HISTORIAN_TYPE_FLOAT64     = 0,
    HISTORIAN_TYPE_FLOAT32     = 1,
    HISTORIAN_TYPE_INT64       = 2,
    HISTORIAN_TYPE_INT32       = 3,
    HISTORIAN_TYPE_INT16       = 4,
    HISTORIAN_TYPE_DIGITAL     = 5,
    HISTORIAN_TYPE_STRING      = 6,
    HISTORIAN_TYPE_BLOB        = 7,
    HISTORIAN_TYPE_TIMESTAMP   = 8
} historian_dtype_t;

/** Compression control flags (L4: OPC HDA Archive Semantics). */
typedef enum {
    HISTORIAN_COMPRESS_OFF  = 0,
    HISTORIAN_COMPRESS_ON   = 1,
    HISTORIAN_COMPRESS_LOSSY = 2
} historian_compression_flag_t;

/** Scan class for data acquisition scheduling. */
typedef enum {
    HISTORIAN_SCAN_PERIODIC   = 0,
    HISTORIAN_SCAN_EVENT      = 1,
    HISTORIAN_SCAN_ADVISED    = 2,
    HISTORIAN_SCAN_MANUAL     = 3
} historian_scan_class_t;

/**
 * @struct historian_tag_metadata_t
 * @brief  Complete metadata descriptor for a single historian tag.
 */
typedef struct {
    int32_t     tag_id;
    char        tag_name[128];
    char        descriptor[256];
    char        eng_units[32];
    historian_dtype_t data_type;
    historian_compression_flag_t compressing;
    historian_scan_class_t scan_class;

    /* Range / scaling - L1: Instrument Range vs. Engineering Range */
    double      inst_zero;
    double      inst_span;
    double      eu_zero;
    double      eu_span;

    /* Exception reporting (deadband) */
    double      exception_deviation;
    double      exception_min_time_s;
    double      exception_max_time_s;
    double      compression_dev_pct;

    /* Display metadata (ISA-101 HMI mapping) */
    int32_t     display_digits;
    char        display_format[16];

    /* Batch context (ISA-88) */
    char        batch_id[64];
    char        unit_procedure[64];

    /* Archiving configuration */
    int32_t     archive_period_ms;
    int32_t     retention_days;
    int32_t     future_data_hours;
} historian_tag_metadata_t;

/*---------------------------------------------------------------------------
 * L1: Timestamp Representation
 *
 * Historians typically use UTC timestamps with sub-second resolution.
 *
 * OSIsoft PI:  Timestamp + subsecond offset (FILETIME internally)
 * Honeywell PHD: UNIX epoch milliseconds
 * OPC HDA:       FILETIME (100-ns intervals since 1601-01-01)
 *---------------------------------------------------------------------------*/

/** Timestamp with millisecond resolution. */
typedef struct {
    int64_t  epoch_ms;
    int32_t  tz_offset_min;
    uint8_t  is_dst;
    uint8_t  is_utc;
} historian_timestamp_t;

/*---------------------------------------------------------------------------
 * L1: Data Quality Flags - OPC HDA Quality semantics
 *
 * References:
 *   OPC Data Access Specification 3.0, Section 6.5
 *   OPC HDA Specification 1.20, Section 5.3
 *---------------------------------------------------------------------------*/

/** Quality status bitmask - OPC-compatible (16-bit). */
typedef uint16_t historian_quality_t;

#define HISTORIAN_QUAL_GOOD         0x00C0u
#define HISTORIAN_QUAL_UNCERTAIN    0x0040u
#define HISTORIAN_QUAL_BAD          0x0000u

#define HISTORIAN_QUAL_SUB_NORMAL           0x00u
#define HISTORIAN_QUAL_SUB_CLAMPED_LOW      0x01u
#define HISTORIAN_QUAL_SUB_CLAMPED_HIGH     0x02u
#define HISTORIAN_QUAL_SUB_CONSTANT         0x03u
#define HISTORIAN_QUAL_SUB_SENSOR_FAIL      0x04u
#define HISTORIAN_QUAL_SUB_COMM_FAIL        0x05u
#define HISTORIAN_QUAL_SUB_LAST_KNOWN       0x06u
#define HISTORIAN_QUAL_SUB_SUBSTITUTED      0x07u
#define HISTORIAN_QUAL_SUB_INTERPOLATED     0x08u
#define HISTORIAN_QUAL_SUB_CALCULATED       0x09u
#define HISTORIAN_QUAL_SUB_QUESTIONABLE     0x0Au
#define HISTORIAN_QUAL_SUB_INITIALIZING     0x0Bu

/**
 * @struct historian_data_point_t
 * @brief  Single time-stamped value with quality - the fundamental
 *         atomic unit of a process historian.
 */
typedef struct {
    int32_t               tag_id;
    historian_timestamp_t timestamp;
    double                value;
    historian_quality_t   quality;
} historian_data_point_t;

/*---------------------------------------------------------------------------
 * L3: Result Set - Query result container
 *---------------------------------------------------------------------------*/

typedef struct {
    historian_data_point_t *points;
    size_t                  count;
    size_t                  capacity;
} historian_result_set_t;

/*---------------------------------------------------------------------------
 * L3: Time Range Specification
 *---------------------------------------------------------------------------*/

typedef enum {
    HISTORIAN_BOUNDARY_INCLUSIVE = 0,
    HISTORIAN_BOUNDARY_EXCLUSIVE = 1,
    HISTORIAN_BOUNDARY_AUTO      = 2
} historian_boundary_mode_t;

typedef struct {
    historian_timestamp_t     start_time;
    historian_timestamp_t     end_time;
    historian_boundary_mode_t start_mode;
    historian_boundary_mode_t end_mode;
} historian_time_range_t;

/*---------------------------------------------------------------------------
 * L1: Snapshot - Current value of a tag
 *---------------------------------------------------------------------------*/

typedef struct {
    int32_t               tag_id;
    historian_timestamp_t timestamp;
    double                value;
    historian_quality_t   quality;
} historian_snapshot_t;

/*---------------------------------------------------------------------------
 * L1: Archive File Metadata
 *---------------------------------------------------------------------------*/

typedef struct {
    int32_t     archive_id;
    char        file_path[512];
    int64_t     start_time_ms;
    int64_t     end_time_ms;
    int64_t     record_count;
    int64_t     size_bytes;
    double      compression_ratio;
    uint8_t     is_primary;
    uint8_t     is_online;
} historian_archive_meta_t;

/*---------------------------------------------------------------------------
 * L8: Time-Series Distribution Statistics
 *---------------------------------------------------------------------------*/

typedef struct {
    double mean;
    double median;
    double stddev;
    double skewness;
    double kurtosis;
    double min_val;
    double max_val;
    double p10;
    double p90;
    size_t count;
    double duration_hr;
} historian_distribution_stats_t;

/*---------------------------------------------------------------------------
 * Core API functions for data model management
 *---------------------------------------------------------------------------*/

/**
 * Initialize tag metadata to safe defaults. O(1)
 */
void historian_tag_metadata_init(historian_tag_metadata_t *meta);

/**
 * Validate tag metadata. Returns 0 if valid, negative error code otherwise.
 */
int historian_tag_metadata_validate(const historian_tag_metadata_t *meta);

/**
 * Set tag engineering range with linear scaling parameters.
 */
void historian_tag_set_range(historian_tag_metadata_t *meta,
                              double eu_zero, double eu_span,
                              double inst_zero, double inst_span);

/**
 * Convert raw instrument value to engineering units.
 * Formula: eu = eu_0 + (raw - inst_0) * (eu_span - eu_0) / (inst_span - inst_0)
 */
double historian_raw_to_eu(const historian_tag_metadata_t *meta, double raw_value);

/**
 * Convert engineering units back to raw instrument value.
 */
double historian_eu_to_raw(const historian_tag_metadata_t *meta, double eu_value);

/**
 * Create a timestamp from a POSIX time_t with millisecond offset.
 */
historian_timestamp_t historian_timestamp_from_time(time_t sec, int32_t ms_offset,
                                                      int32_t tz_min, int is_utc);

/**
 * Parse an ISO 8601 string to timestamp.
 * Returns 0 on success, negative on parse error.
 */
int historian_timestamp_from_iso8601(const char *iso_str, historian_timestamp_t *ts);

/**
 * Format a timestamp as ISO 8601 string. Buffer must be at least 32 bytes.
 */
int historian_timestamp_to_iso8601(const historian_timestamp_t *ts,
                                    char *buffer, size_t buffer_size);

/**
 * Compare two timestamps. Returns: <0 if a<b, 0 if equal, >0 if a>b.
 */
int historian_timestamp_compare(const historian_timestamp_t *a,
                                 const historian_timestamp_t *b);

/**
 * Compute timestamp difference in milliseconds: a - b.
 */
int64_t historian_timestamp_diff_ms(const historian_timestamp_t *a,
                                      const historian_timestamp_t *b);

/**
 * Check if quality flags indicate good data.
 */
int historian_quality_is_good(historian_quality_t quality);

/**
 * Set quality flag bits. Returns new quality value.
 */
historian_quality_t historian_quality_set(historian_quality_t base,
                                            historian_quality_t bits);

/**
 * Create a data point.
 */
historian_data_point_t historian_make_point(int32_t tag_id,
                                              historian_timestamp_t ts,
                                              double value,
                                              historian_quality_t quality);

/**
 * Check if a data point has a valid numeric value.
 */
int historian_point_has_value(const historian_data_point_t *point);

/**
 * Initialize an empty result set.
 */
void historian_result_set_init(historian_result_set_t *rs);

/**
 * Append a data point. Amortized O(1).
 */
int historian_result_set_append(historian_result_set_t *rs,
                                 historian_data_point_t point);

/**
 * Sort result set by timestamp ascending. O(n log n).
 */
void historian_result_set_sort(historian_result_set_t *rs);

/**
 * Free all memory associated with the result set.
 */
void historian_result_set_destroy(historian_result_set_t *rs);

#define HISTORIAN_MAX_TAGS           2000000
#define HISTORIAN_MAX_QUERY_ROWS     5000000
#define HISTORIAN_MAX_TAG_NAME_LEN   255

#endif /* HISTORIAN_MODEL_H */
