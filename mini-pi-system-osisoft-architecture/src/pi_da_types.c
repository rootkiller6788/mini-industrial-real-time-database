/** pi_da_types.c - Core PI Data Archive Type Implementations */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include "../include/pi_da_types.h"

/* ─── Timestamp Operations ──────────────────────────────────────── */

const char* pi_timestamp_to_iso(const pi_timestamp_t *ts) {
    static char buf[36];
    if (!ts || ts->seconds == INT64_MAX) {
        strncpy(buf, "*NOW*", 31); buf[31] = 0; return buf;
    }
    if (ts->seconds == 0 && ts->subsec == 0) {
        strncpy(buf, "*EMPTY*", 31); buf[31] = 0; return buf;
    }
    time_t sec = (time_t)ts->seconds;
    struct tm tbuf;
#ifdef _WIN32
    gmtime_s(&tbuf, &sec);
#else
    gmtime_r(&sec, &tbuf);
#endif
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%07u",
             tbuf.tm_year + 1900, tbuf.tm_mon + 1, tbuf.tm_mday,
             tbuf.tm_hour, tbuf.tm_min, tbuf.tm_sec, ts->subsec);
    return buf;
}

int pi_timestamp_compare(const pi_timestamp_t *a, const pi_timestamp_t *b) {
    if (!a || !b) return 0;
    int a_now = (a->seconds == INT64_MAX);
    int b_now = (b->seconds == INT64_MAX);
    if (a_now && b_now) return 0;
    if (a_now) return 1;
    if (b_now) return -1;
    if (a->seconds < b->seconds) return -1;
    if (a->seconds > b->seconds) return 1;
    if (a->subsec < b->subsec) return -1;
    if (a->subsec > b->subsec) return 1;
    return 0;
}

double pi_timestamp_diff_seconds(const pi_timestamp_t *a, const pi_timestamp_t *b) {
    if (!a || !b) return 0.0;
    if (a->seconds == INT64_MAX || b->seconds == INT64_MAX) return 0.0;
    int64_t dsec = b->seconds - a->seconds;
    int32_t dsub = (int32_t)(b->subsec - a->subsec);
    return (double)dsec + (double)dsub / 10000000.0;
}

void pi_timestamp_now(pi_timestamp_t *ts) {
    if (!ts) return;
    ts->seconds = (int64_t)time(NULL);
    ts->subsec = 0;
}

/* ─── Point Type Metadata ──────────────────────────────────────── */

const char* pi_point_type_name(pi_point_type_t pt) {
    switch (pt) {
        case PI_POINT_DIGITAL:   return "Digital";
        case PI_POINT_INT16:     return "Int16";
        case PI_POINT_INT32:     return "Int32";
        case PI_POINT_FLOAT16:   return "Float16";
        case PI_POINT_FLOAT32:   return "Float32";
        case PI_POINT_FLOAT64:   return "Float64";
        case PI_POINT_STRING:    return "String";
        case PI_POINT_TIMESTAMP: return "Timestamp";
        case PI_POINT_BLOB:      return "Blob";
        default:                 return "Unknown";
    }
}

int pi_point_type_size(pi_point_type_t pt) {
    switch (pt) {
        case PI_POINT_DIGITAL:   return 2;
        case PI_POINT_INT16:     return 2;
        case PI_POINT_INT32:     return 4;
        case PI_POINT_FLOAT16:   return 2;
        case PI_POINT_FLOAT32:   return 4;
        case PI_POINT_FLOAT64:   return 8;
        case PI_POINT_STRING:    return 80;
        case PI_POINT_TIMESTAMP: return 8;
        case PI_POINT_BLOB:      return 0;
        default:                 return 0;
    }
}

int pi_point_type_is_numeric(pi_point_type_t pt) {
    return (pt == PI_POINT_FLOAT16 || pt == PI_POINT_FLOAT32 ||
            pt == PI_POINT_FLOAT64 || pt == PI_POINT_INT16 ||
            pt == PI_POINT_INT32) ? 1 : 0;
}

int pi_point_type_is_float(pi_point_type_t pt) {
    return (pt == PI_POINT_FLOAT16 || pt == PI_POINT_FLOAT32 ||
            pt == PI_POINT_FLOAT64) ? 1 : 0;
}

/* ─── Status Code Operations ───────────────────────────────────── */
const char* pi_status_name(pi_status_code_t code) {
    switch (code) {
        case PI_STATUS_GOOD:        return "Good";
        case PI_STATUS_BAD:         return "Bad";
        case PI_STATUS_UNCERTAIN:   return "Uncertain";
        case PI_STATUS_STALE:       return "Stale";
        case PI_STATUS_SUBSTITUTED: return "Substituted";
        case PI_STATUS_NO_DATA:     return "No Data";
        case PI_STATUS_PT_CREATED:  return "Pt Created";
        case PI_STATUS_SHUTDOWN:    return "Shutdown";
        default:                    return "Unknown";
    }
}

int pi_value_is_good(const pi_value_t *v) {
    if (!v) return 0;
    return v->status == PI_STATUS_GOOD ? 1 : 0;
}

/* ─── Value Manipulation ───────────────────────────────────────── */
void pi_value_init(pi_value_t *v, pi_point_type_t type) {
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->value_type = type; v->status = PI_STATUS_GOOD;
    v->timestamp = PI_TIME_NOW;
}
void pi_value_set_float64(pi_value_t *v, double val, pi_timestamp_t ts) {
    if (!v) return;
    v->value_type = PI_POINT_FLOAT64;
    v->value.as_float64 = val; v->timestamp = ts; v->status = PI_STATUS_GOOD;
}
void pi_value_set_digital(pi_value_t *v, int32_t state, pi_timestamp_t ts) {
    if (!v) return;
    v->value_type = PI_POINT_DIGITAL;
    v->value.as_digital = state; v->timestamp = ts; v->status = PI_STATUS_GOOD;
}
void pi_value_set_int32(pi_value_t *v, int32_t val, pi_timestamp_t ts) {
    if (!v) return;
    v->value_type = PI_POINT_INT32;
    v->value.as_int32 = val; v->timestamp = ts; v->status = PI_STATUS_GOOD;
}
void pi_value_set_string(pi_value_t *v, const char *str, pi_timestamp_t ts) {
    if (!v || !str) return;
    v->value_type = PI_POINT_STRING;
    strncpy(v->value.as_string, str, 79); v->value.as_string[79] = 0;
    v->timestamp = ts; v->status = PI_STATUS_GOOD;
}
double pi_value_get_float64(const pi_value_t *v) {
    if (!v) return 0.0;
    if (v->value_type == PI_POINT_FLOAT64) return v->value.as_float64;
    if (v->value_type == PI_POINT_FLOAT32) return (double)v->value.as_float32;
    return 0.0;
}
int32_t pi_value_get_int32(const pi_value_t *v) {
    if (!v) return 0;
    if (v->value_type == PI_POINT_INT32) return v->value.as_int32;
    if (v->value_type == PI_POINT_INT16) return (int32_t)v->value.as_int16;
    return 0;
}
int32_t pi_value_get_digital(const pi_value_t *v) {
    if (!v) return 0;
    return v->value_type == PI_POINT_DIGITAL ? v->value.as_digital : 0;
}
const char* pi_value_get_string(const pi_value_t *v) {
    if (!v) return "";
    return v->value_type == PI_POINT_STRING ? v->value.as_string : "";
}
void pi_value_set_status(pi_value_t *v, pi_status_code_t status) {
    if (!v) return;
    v->status = status;
}
int pi_value_copy(pi_value_t *dst, const pi_value_t *src) {
    if (!dst || !src) return -1;
    memcpy(dst, src, sizeof(pi_value_t));
    return 0;
}
int pi_value_compare_equal(const pi_value_t *a, const pi_value_t *b) {
    if (!a || !b) return 0;
    if (a->value_type != b->value_type || a->status != b->status) return 0;
    switch (a->value_type) {
        case PI_POINT_FLOAT64: return a->value.as_float64 == b->value.as_float64;
        case PI_POINT_FLOAT32: return a->value.as_float32 == b->value.as_float32;
        case PI_POINT_INT32:   return a->value.as_int32 == b->value.as_int32;
        case PI_POINT_INT16:   return a->value.as_int16 == b->value.as_int16;
        case PI_POINT_DIGITAL: return a->value.as_digital == b->value.as_digital;
        case PI_POINT_STRING:  return strcmp(a->value.as_string, b->value.as_string)==0;
        default: return memcmp(&a->value, &b->value, sizeof(a->value))==0;
    }
}
void pi_value_print(const pi_value_t *v) {
    if (!v) { printf("(null)\n"); return; }
    printf("[%s] %s: ", pi_timestamp_to_iso(&v->timestamp), pi_point_type_name(v->value_type));
    switch (v->value_type) {
        case PI_POINT_FLOAT64: printf("%g", v->value.as_float64); break;
        case PI_POINT_FLOAT32: printf("%g", (double)v->value.as_float32); break;
        case PI_POINT_INT32:   printf("%d", v->value.as_int32); break;
        case PI_POINT_INT16:   printf("%hd", v->value.as_int16); break;
        case PI_POINT_DIGITAL: printf("state=%d", v->value.as_digital); break;
        case PI_POINT_STRING:  printf("\"%s\"", v->value.as_string); break;
        default: printf("<unknown>"); break;
    }
    printf(" (%s)\n", pi_status_name(v->status));
}

/* ─── EU Scaling ────────────────────────────────────────────────── */
double pi_eu_to_percent(double eu_value, double zero, double span) {
    if (span <= 0.0) return 0.0;
    return 100.0 * (eu_value - zero) / span;
}
double pi_percent_to_eu(double pct, double zero, double span) {
    return zero + (pct / 100.0) * span;
}
double pi_eu_range(const pi_point_attributes_t *attrs) {
    if (!attrs) return 0.0;
    double r = attrs->span - attrs->zero;
    return r < 0.0 ? -r : r;
}
double pi_eu_midpoint(const pi_point_attributes_t *attrs) {
    if (!attrs) return 0.0;
    return (attrs->zero + attrs->span) / 2.0;
}
double pi_eu_deadband(const pi_point_attributes_t *attrs) {
    if (!attrs) return 0.0;
    double abs_db = attrs->exc_dev;
    double pct_db = attrs->exc_dev_percent * pi_eu_range(attrs) / 100.0;
    return (abs_db > pct_db) ? abs_db : pct_db;
}

/* ─── Point Attributes Init ─────────────────────────────────────── */
int pi_point_attributes_init_defaults(pi_point_attributes_t *attrs,
                                       const char *tag, pi_point_type_t pt) {
    if (!attrs || !tag) return -1;
    memset(attrs, 0, sizeof(*attrs));
    strncpy(attrs->tag, tag, PI_MAX_TAG_LEN - 1);
    attrs->tag[PI_MAX_TAG_LEN - 1] = 0;
    attrs->point_type = pt;
    attrs->point_class = PI_CLASS_CLASSIC;
    attrs->zero = 0.0; attrs->span = 100.0;
    attrs->exc_dev = 0.0; attrs->comp_dev = 0.0;
    attrs->comp_max = 0; attrs->comp_min = 0;
    attrs->scan = 1; attrs->compressing = 1;
    attrs->step = 0; attrs->archive = 1;
    attrs->display_digits = 2;
    strncpy(attrs->point_source, "R", PI_MAX_POINTSOURCE_LEN - 1);
    return 0;
}

/* ─── Additional Timestamp Utilities ────────────────────────────── */
int pi_timestamp_from_time_t(pi_timestamp_t *ts, time_t t) {
    if (!ts) return -1;
    ts->seconds = (int64_t)t;
    ts->subsec = 0;
    return 0;
}
int pi_timestamp_from_iso(pi_timestamp_t *ts, const char *iso_str) {
    if (!ts || !iso_str) return -1;
    int y=0,mo=0,d=0,h=0,mi=0,s=0; unsigned int sub=0;
    if (sscanf(iso_str, "%d-%d-%dT%d:%d:%d.%u", &y,&mo,&d,&h,&mi,&s,&sub) >= 6) {
        struct tm tbuf; memset(&tbuf, 0, sizeof(tbuf));
        tbuf.tm_year=y-1900; tbuf.tm_mon=mo-1; tbuf.tm_mday=d;
        tbuf.tm_hour=h; tbuf.tm_min=mi; tbuf.tm_sec=s; tbuf.tm_isdst=-1;
        ts->seconds = (int64_t)mktime(&tbuf); ts->subsec = sub; return 0;
    }
    return -1;
}

/* ─── Timestamp Arithmetic ───────────────────────────────────────── */
void pi_timestamp_add_seconds(pi_timestamp_t *ts, double delta_sec) {
    if (!ts) return;
    int64_t dsec = (int64_t)delta_sec;
    uint32_t dsub = (uint32_t)((delta_sec - dsec) * 10000000.0);
    ts->seconds += dsec;
    ts->subsec += dsub;
    if (ts->subsec >= 10000000) { ts->subsec -= 10000000; ts->seconds++; }
}

int pi_timestamp_is_now(const pi_timestamp_t *ts) {
    if (!ts) return 0;
    return (ts->seconds == INT64_MAX) ? 1 : 0;
}

int pi_timestamp_is_empty(const pi_timestamp_t *ts) {
    if (!ts) return 1;
    return (ts->seconds == 0 && ts->subsec == 0) ? 1 : 0;
}

/* ─── PI Point Span Check ─────────────────────────────────────────── */
int pi_point_is_in_span(const pi_point_attributes_t *attrs, double value) {
    if (!attrs) return 0;
    double lo = attrs->zero < attrs->span ? attrs->zero : attrs->span;
    double hi = attrs->zero > attrs->span ? attrs->zero : attrs->span;
    return (value >= lo && value <= hi) ? 1 : 0;
}

double pi_point_clamp_to_span(const pi_point_attributes_t *attrs, double value) {
    if (!attrs) return value;
    double lo = attrs->zero < attrs->span ? attrs->zero : attrs->span;
    double hi = attrs->zero > attrs->span ? attrs->zero : attrs->span;
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

/* ─── Status Propagation ──────────────────────────────────────────── */
pi_status_code_t pi_status_worst_of(pi_status_code_t a, pi_status_code_t b) {
    if (a == PI_STATUS_BAD || b == PI_STATUS_BAD) return PI_STATUS_BAD;
    if (a == PI_STATUS_UNCERTAIN || b == PI_STATUS_UNCERTAIN) return PI_STATUS_UNCERTAIN;
    if (a == PI_STATUS_STALE || b == PI_STATUS_STALE) return PI_STATUS_STALE;
    if (a == PI_STATUS_SUBSTITUTED || b == PI_STATUS_SUBSTITUTED) return PI_STATUS_SUBSTITUTED;
    return PI_STATUS_GOOD;
}
