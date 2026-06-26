/**
 * pi_da_types.h — Core PI Data Archive Type Definitions
 *
 * Domain: OSIsoft PI System Architecture (now AVEVA PI System)
 * Reference: PI Server 2023 R2 Documentation, PI SDK Programmer's Guide
 *
 * Foundational data structures: timestamps, point types, values, status codes.
 * PI Points are the fundamental addressable entities in the PI System.
 *
 * Knowledge: L1 Definitions, L3 Engineering Structures
 * MIT 6.302 · Stanford ENGR205 · Purdue ME 575 · RWTH Aachen Industrial Ctrl
 */
#ifndef PI_DA_TYPES_H
#define PI_DA_TYPES_H
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif

/* ─── PI Timestamp ──────────────────────────────────────────────── */
/** PI unified timestamp: seconds since UNIX epoch + subsec in 100ns ticks */
typedef struct { int64_t seconds; uint32_t subsec; } pi_timestamp_t;
#define PI_TIME_NOW   ((pi_timestamp_t){INT64_MAX, 0})
#define PI_TIME_EMPTY ((pi_timestamp_t){0, 0})

/* ─── PI Point Type ─────────────────────────────────────────────── */
/** PI DA point types (classic 0–8 codes) */
typedef enum {
    PI_POINT_DIGITAL=0, PI_POINT_INT16=1, PI_POINT_INT32=2,
    PI_POINT_FLOAT16=3, PI_POINT_FLOAT32=4, PI_POINT_FLOAT64=5,
    PI_POINT_STRING=6, PI_POINT_TIMESTAMP=7, PI_POINT_BLOB=8
} pi_point_type_t;

/* ─── PI Point Class ────────────────────────────────────────────── */
typedef enum { PI_CLASS_CLASSIC=0, PI_CLASS_BASE=1, PI_CLASS_FORMULA=2 } pi_point_class_t;

/* ─── PI Status Codes ───────────────────────────────────────────── */
typedef enum {
    PI_STATUS_GOOD=0, PI_STATUS_BAD=-1, PI_STATUS_UNCERTAIN=1,
    PI_STATUS_STALE=2, PI_STATUS_SUBSTITUTED=3, PI_STATUS_NO_DATA=-5,
    PI_STATUS_PT_CREATED=245, PI_STATUS_SHUTDOWN=248
} pi_status_code_t;

/* ─── PI Value ──────────────────────────────────────────────────── */
typedef struct {
    union { int64_t as_int64; double as_float64; float as_float32;
            int32_t as_int32; int16_t as_int16; char as_string[80];
            int32_t as_digital; } value;
    pi_timestamp_t timestamp; pi_status_code_t status; pi_point_type_t value_type;
} pi_value_t;

/* ─── Digital State ─────────────────────────────────────────────── */
#define PI_MAX_DIGITAL_STATE_NAME 32
typedef struct { int32_t code; char name[PI_MAX_DIGITAL_STATE_NAME]; } pi_digital_state_t;

/* ─── PI Point Attributes ───────────────────────────────────────── */
/** Core PI point configuration (~40 attributes, classic PI DA point definition) */
#define PI_MAX_TAG_LEN 64
#define PI_MAX_DESCRIPTOR_LEN 128
#define PI_MAX_ENGUNITS_LEN 16
#define PI_MAX_POINTSOURCE_LEN 8
#define PI_MAX_LOCATION_LEN 32

typedef struct {
    int32_t point_id; char tag[PI_MAX_TAG_LEN];
    char descriptor[PI_MAX_DESCRIPTOR_LEN];
    pi_point_type_t point_type; pi_point_class_t point_class;
    char point_source[PI_MAX_POINTSOURCE_LEN];
    char location1[PI_MAX_LOCATION_LEN], location2[PI_MAX_LOCATION_LEN];
    char location3[PI_MAX_LOCATION_LEN], location4[PI_MAX_LOCATION_LEN];
    char location5[PI_MAX_LOCATION_LEN];
    char eng_units[PI_MAX_ENGUNITS_LEN];
    double zero, span, exc_dev, exc_dev_percent, comp_dev, comp_dev_percent;
    int32_t comp_max, comp_min, scan, shutdown_timeout, compressing;
    int32_t step, archive, display_digits, data_owner_id, point_source_id, rec_no;
} pi_point_attributes_t;

/* ─── Archive Event ─────────────────────────────────────────────── */
typedef struct {
    pi_timestamp_t timestamp; pi_value_t value;
    int32_t annotated, questionable, archive_recno;
} pi_archive_event_t;

/* ─── Snapshot Entry ────────────────────────────────────────────── */
typedef struct {
    int32_t point_id; pi_value_t current_value;
    pi_timestamp_t snapshot_time; int32_t event_count, exception_test;
} pi_snapshot_entry_t;

/* ─── Subsystem State ───────────────────────────────────────────── */
typedef enum { PI_SUBSYSTEM_RUNNING=0, PI_SUBSYSTEM_STOPPED=1,
    PI_SUBSYSTEM_DEGRADED=2, PI_SUBSYSTEM_STARTING=3, PI_SUBSYSTEM_STOPPING=4
} pi_subsystem_state_t;

typedef struct {
    char name[32]; pi_subsystem_state_t state; int32_t thread_id;
    double cpu_usage_pct; int64_t bytes_allocated, operations_count, error_count;
    pi_timestamp_t last_heartbeat;
} pi_subsystem_info_t;

/* ─── Utility Declarations ─────────────────────────────────────── */
const char* pi_timestamp_to_iso(const pi_timestamp_t *ts);
int pi_timestamp_compare(const pi_timestamp_t *a, const pi_timestamp_t *b);
double pi_timestamp_diff_seconds(const pi_timestamp_t *a, const pi_timestamp_t *b);
void pi_timestamp_now(pi_timestamp_t *ts);
const char* pi_point_type_name(pi_point_type_t pt);
int pi_point_type_size(pi_point_type_t pt);
const char* pi_status_name(pi_status_code_t code);
int pi_value_is_good(const pi_value_t *v);
void pi_value_init(pi_value_t *v, pi_point_type_t type);
void pi_value_set_float64(pi_value_t *v, double val, pi_timestamp_t ts);
void pi_value_set_digital(pi_value_t *v, int32_t state, pi_timestamp_t ts);
double pi_value_get_float64(const pi_value_t *v);
int32_t pi_value_get_int32(const pi_value_t *v);
int32_t pi_value_get_digital(const pi_value_t *v);
double pi_eu_to_percent(double eu_value, double zero, double span);
double pi_percent_to_eu(double pct, double zero, double span);
/* Additional utility declarations */
int pi_point_type_is_numeric(pi_point_type_t pt);
int pi_point_type_is_float(pi_point_type_t pt);
void pi_value_set_int32(pi_value_t *v, int32_t val, pi_timestamp_t ts);
void pi_value_set_string(pi_value_t *v, const char *str, pi_timestamp_t ts);
void pi_value_set_status(pi_value_t *v, pi_status_code_t status);
int pi_value_copy(pi_value_t *dst, const pi_value_t *src);
int pi_value_compare_equal(const pi_value_t *a, const pi_value_t *b);
void pi_value_print(const pi_value_t *v);
const char* pi_value_get_string(const pi_value_t *v);
int pi_point_attributes_init_defaults(pi_point_attributes_t *attrs, const char *tag, pi_point_type_t pt);
double pi_eu_range(const pi_point_attributes_t *attrs);
double pi_eu_midpoint(const pi_point_attributes_t *attrs);
double pi_eu_deadband(const pi_point_attributes_t *attrs);
int pi_timestamp_from_time_t(pi_timestamp_t *ts, time_t t);
int pi_timestamp_from_iso(pi_timestamp_t *ts, const char *iso_str);
void pi_timestamp_add_seconds(pi_timestamp_t *ts, double delta_sec);
int pi_timestamp_is_now(const pi_timestamp_t *ts);
int pi_timestamp_is_empty(const pi_timestamp_t *ts);

#ifdef __cplusplus
}
#endif
#endif /* PI_DA_TYPES_H */
