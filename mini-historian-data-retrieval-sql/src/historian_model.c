/**
 * @file    historian_model.c
 * @brief   Implementation of core historian data model functions.
 *
 * Knowledge coverage:
 *   L1: Tag metadata management, timestamp parsing/formatting,
 *       quality flag bit operations, instrument scaling
 *   L3: Result set dynamic array management
 *   L4: ISO 8601 timestamp standard (ISO 8601-1:2019)
 */

#include "historian_model.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>

/* =========================================================================
 * L1: Tag Metadata Management
 * ========================================================================= */

void historian_tag_metadata_init(historian_tag_metadata_t *meta)
{
    if (!meta) return;
    memset(meta, 0, sizeof(*meta));
    meta->tag_id = -1;
    meta->data_type = HISTORIAN_TYPE_FLOAT64;
    meta->compressing = HISTORIAN_COMPRESS_ON;
    meta->scan_class = HISTORIAN_SCAN_PERIODIC;
    meta->inst_zero = 4.0;
    meta->inst_span = 20.0;
    meta->eu_zero = 0.0;
    meta->eu_span = 100.0;
    meta->exception_deviation = 0.5;
    meta->exception_min_time_s = 0.0;
    meta->exception_max_time_s = 28800.0;
    meta->compression_dev_pct = 0.5;
    meta->display_digits = 2;
    meta->archive_period_ms = 0;
    meta->retention_days = 365;
    meta->future_data_hours = 24;
    strncpy(meta->display_format, "%.2f", sizeof(meta->display_format) - 1);
}

int historian_tag_metadata_validate(const historian_tag_metadata_t *meta)
{
    if (!meta) return -1;

    /* Tag ID must be positive or sentinel */
    if (meta->tag_id < -1) return -2;

    /* Tag name must not be empty */
    if (meta->tag_name[0] == '\0') return -3;

    /* Engineering unit span must be positive (non-zero) */
    if (fabs(meta->eu_span - meta->eu_zero) < 1e-12) return -4;

    /* Instrument span must be positive */
    if (fabs(meta->inst_span - meta->inst_zero) < 1e-12) return -5;

    /* Data type must be in valid range */
    if (meta->data_type > HISTORIAN_TYPE_TIMESTAMP) return -6;

    /* Compression deviation must be non-negative */
    if (meta->exception_deviation < 0.0) return -7;

    /* Min time must not exceed max time (if max > 0) */
    if (meta->exception_max_time_s > 0.0 &&
        meta->exception_min_time_s > meta->exception_max_time_s) return -8;

    /* Retention must be positive */
    if (meta->retention_days <= 0) return -9;

    return 0;
}

void historian_tag_set_range(historian_tag_metadata_t *meta,
                              double eu_zero, double eu_span,
                              double inst_zero, double inst_span)
{
    if (!meta) return;
    meta->eu_zero = eu_zero;
    meta->eu_span = eu_span;
    meta->inst_zero = inst_zero;
    meta->inst_span = inst_span;
}

double historian_raw_to_eu(const historian_tag_metadata_t *meta, double raw_value)
{
    if (!meta) return NAN;

    double inst_range = meta->inst_span - meta->inst_zero;
    double eu_range   = meta->eu_span - meta->eu_zero;

    /* Guard against zero instrument range */
    if (fabs(inst_range) < 1e-12) return NAN;

    return meta->eu_zero + (raw_value - meta->inst_zero) * eu_range / inst_range;
}

double historian_eu_to_raw(const historian_tag_metadata_t *meta, double eu_value)
{
    if (!meta) return NAN;

    double inst_range = meta->inst_span - meta->inst_zero;
    double eu_range   = meta->eu_span - meta->eu_zero;

    if (fabs(eu_range) < 1e-12) return NAN;

    return meta->inst_zero + (eu_value - meta->eu_zero) * inst_range / eu_range;
}

/* =========================================================================
 * L1: Timestamp Representation and Manipulation
 * ========================================================================= */

/* Days in each month (non-leap year) */
static const int days_in_month[] = {31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};

static int is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

historian_timestamp_t historian_timestamp_from_time(time_t sec,
                                                      int32_t ms_offset,
                                                      int32_t tz_min,
                                                      int is_utc)
{
    historian_timestamp_t ts;
    ts.epoch_ms = (int64_t)sec * 1000LL + ms_offset;
    ts.tz_offset_min = tz_min;
    ts.is_dst = 0;
    ts.is_utc = (uint8_t)is_utc;
    return ts;
}

int historian_timestamp_from_iso8601(const char *iso_str,
                                       historian_timestamp_t *ts)
{
    if (!iso_str || !ts) return -1;

    int year, month, day, hour, minute, second, ms = 0;
    int tz_h = 0, tz_m = 0, is_utc = 1;
    char tz_sign = '+';

    /* Try parsing: YYYY-MM-DDTHH:MM:SS.mmmZ or +HH:MM */
    int parsed = sscanf(iso_str, "%d-%d-%dT%d:%d:%d.%d",
                        &year, &month, &day, &hour, &minute, &second, &ms);
    if (parsed < 6) {
        /* Try without milliseconds */
        parsed = sscanf(iso_str, "%d-%d-%dT%d:%d:%d",
                        &year, &month, &day, &hour, &minute, &second);
        if (parsed < 6) return -2;
    }

    /* Basic validation */
    if (year < 1970 || year > 3000) return -3;
    if (month < 1 || month > 12) return -4;
    int max_day = days_in_month[month - 1];
    if (month == 2 && is_leap_year(year)) max_day = 29;
    if (day < 1 || day > max_day) return -5;
    if (hour < 0 || hour > 23) return -6;
    if (minute < 0 || minute > 59) return -7;
    if (second < 0 || second > 60) return -8; /* 60 for leap second */

    /* Parse timezone suffix */
    const char *suffix = strchr(iso_str, 'T');
    if (suffix) {
        suffix = strpbrk(suffix, "Z+-");
        if (suffix) {
            if (*suffix == 'Z') {
                tz_h = 0; tz_m = 0; is_utc = 1;
            } else {
                tz_sign = *suffix;
                if (sscanf(suffix + 1, "%d:%d", &tz_h, &tz_m) < 1) {
                    tz_h = 0; tz_m = 0;
                }
                is_utc = 0;
            }
        }
    }

    /* Compute UNIX epoch seconds from civil date using a compact algorithm */
    /* Days from 1970-01-01 using Zeller-like formula */
    int y = year;
    int m = month;
    if (m <= 2) { y--; m += 12; }
    long long era_days = (long long)(y / 400) * 146097LL;
    int yoe = y % 400;
    int doy = (153 * (m - 3) + 2) / 5 + day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long total_days = era_days + (long long)doe - 719528LL;

    int64_t epoch_sec = (int64_t)(total_days * 86400LL +
                                   hour * 3600LL + minute * 60LL + second);

    /* Adjust for timezone: convert to UTC by subtracting offset */
    int tz_offset_minutes = (tz_sign == '-') ? -(tz_h * 60 + tz_m)
                                              : (tz_h * 60 + tz_m);
    epoch_sec -= tz_offset_minutes * 60LL;

    ts->epoch_ms = epoch_sec * 1000LL + ms;
    ts->tz_offset_min = tz_offset_minutes;
    ts->is_dst = 0;
    ts->is_utc = (uint8_t)is_utc;

    return 0;
}

int historian_timestamp_to_iso8601(const historian_timestamp_t *ts,
                                    char *buffer, size_t buffer_size)
{
    if (!ts || !buffer || buffer_size < 32) return -1;

    int64_t epoch_sec = ts->epoch_ms / 1000LL;
    int ms = (int)(ts->epoch_ms % 1000LL);
    if (ms < 0) { ms += 1000; epoch_sec--; }

    /* Convert epoch seconds to civil date */
    long long total_days = (long long)epoch_sec / 86400LL;
    int sec_of_day = (int)((long long)epoch_sec % 86400LL);
    if (sec_of_day < 0) { sec_of_day += 86400; total_days--; }

    long long era_days = total_days + 719528LL;
    int era = (int)(era_days / 146097LL);
    int doe = (int)(era_days % 146097LL);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = yoe + era * 400;
    int doy = doe - (yoe * 365 + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int d = doy - (153 * mp + 2) / 5 + 1;
    int m = mp + 3;
    if (m > 12) { m -= 12; y++; }
    int month = m;
    int day = d;
    int year = y;

    int hour = sec_of_day / 3600;
    int minute = (sec_of_day % 3600) / 60;
    int second = sec_of_day % 60;

    int written;
    if (ts->is_utc) {
        written = snprintf(buffer, buffer_size,
                           "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                           year, month, day, hour, minute, second, ms);
    } else {
        int tz_h = abs(ts->tz_offset_min) / 60;
        int tz_m = abs(ts->tz_offset_min) % 60;
        char sign = (ts->tz_offset_min >= 0) ? '+' : '-';
        written = snprintf(buffer, buffer_size,
                           "%04d-%02d-%02dT%02d:%02d:%02d.%03d%c%02d:%02d",
                           year, month, day, hour, minute, second, ms,
                           sign, tz_h, tz_m);
    }
    return written;
}

int historian_timestamp_compare(const historian_timestamp_t *a,
                                 const historian_timestamp_t *b)
{
    if (!a || !b) return 0;
    if (a->epoch_ms < b->epoch_ms) return -1;
    if (a->epoch_ms > b->epoch_ms) return 1;
    return 0;
}

int64_t historian_timestamp_diff_ms(const historian_timestamp_t *a,
                                      const historian_timestamp_t *b)
{
    if (!a || !b) return 0;
    return a->epoch_ms - b->epoch_ms;
}

/* =========================================================================
 * L1: Quality Flag Operations
 * ========================================================================= */

int historian_quality_is_good(historian_quality_t quality)
{
    /* Check the major quality bits (bits 6-7) */
    historian_quality_t major = quality & 0x00C0u;
    return (major == HISTORIAN_QUAL_GOOD) ? 1 : 0;
}

historian_quality_t historian_quality_set(historian_quality_t base,
                                            historian_quality_t bits)
{
    return base | bits;
}

/* =========================================================================
 * L1: Data Point Creation and Validation
 * ========================================================================= */

historian_data_point_t historian_make_point(int32_t tag_id,
                                              historian_timestamp_t ts,
                                              double value,
                                              historian_quality_t quality)
{
    historian_data_point_t point;
    point.tag_id = tag_id;
    point.timestamp = ts;
    point.value = value;
    point.quality = quality;
    return point;
}

int historian_point_has_value(const historian_data_point_t *point)
{
    if (!point) return 0;
    return isfinite(point->value) ? 1 : 0;
}

/* =========================================================================
 * L3: Result Set Dynamic Array
 * ========================================================================= */

#define HISTORIAN_RESULT_INITIAL_CAPACITY 64

void historian_result_set_init(historian_result_set_t *rs)
{
    if (!rs) return;
    rs->points = NULL;
    rs->count = 0;
    rs->capacity = 0;
}

int historian_result_set_append(historian_result_set_t *rs,
                                 historian_data_point_t point)
{
    if (!rs) return -1;

    /* Initial allocation */
    if (rs->capacity == 0) {
        rs->points = (historian_data_point_t *)malloc(
            HISTORIAN_RESULT_INITIAL_CAPACITY * sizeof(historian_data_point_t));
        if (!rs->points) return -1;
        rs->capacity = HISTORIAN_RESULT_INITIAL_CAPACITY;
    }

    /* Grow if full (double capacity, amortized O(1)) */
    if (rs->count >= rs->capacity) {
        size_t new_cap = rs->capacity * 2;
        /* Cap at maximum to prevent overflow */
        if (new_cap > HISTORIAN_MAX_QUERY_ROWS) {
            if (rs->capacity >= HISTORIAN_MAX_QUERY_ROWS) return -2;
            new_cap = HISTORIAN_MAX_QUERY_ROWS;
        }
        historian_data_point_t *new_points = (historian_data_point_t *)realloc(
            rs->points, new_cap * sizeof(historian_data_point_t));
        if (!new_points) return -1;
        rs->points = new_points;
        rs->capacity = new_cap;
    }

    rs->points[rs->count] = point;
    rs->count++;
    return 0;
}

static int compare_by_timestamp(const void *a, const void *b)
{
    const historian_data_point_t *pa = (const historian_data_point_t *)a;
    const historian_data_point_t *pb = (const historian_data_point_t *)b;
    if (pa->timestamp.epoch_ms < pb->timestamp.epoch_ms) return -1;
    if (pa->timestamp.epoch_ms > pb->timestamp.epoch_ms) return 1;
    return 0;
}

void historian_result_set_sort(historian_result_set_t *rs)
{
    if (!rs || rs->count <= 1) return;
    qsort(rs->points, rs->count, sizeof(historian_data_point_t),
          compare_by_timestamp);
}

void historian_result_set_destroy(historian_result_set_t *rs)
{
    if (!rs) return;
    free(rs->points);
    rs->points = NULL;
    rs->count = 0;
    rs->capacity = 0;
}
