/**
 * @file    historian_retrieval.c
 * @brief   SQL generation and data retrieval for industrial historians.
 *
 * Knowledge coverage:
 *   L2: Raw, interpolated, aggregated, snapshot retrieval modes
 *   L3: SQL AST construction, dialect-specific query generation
 *   L4: SQL injection prevention, ANSI SQL-92 standard
 *   L5: Query optimization, pagination, cost estimation
 *   L7: PI OLEDB, Honeywell PHD, AspenTech SQLplus patterns
 */

#include "historian_retrieval.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* =========================================================================
 * L2: Query Specification Management
 * ========================================================================= */

void historian_query_spec_init(historian_query_spec_t *spec)
{
    if (!spec) return;
    memset(spec, 0, sizeof(*spec));
    spec->tag_id = -1;
    spec->tag_filter = NULL;
    spec->tag_ids = NULL;
    spec->tag_count = 0;
    spec->direction = HISTORIAN_DIR_FORWARD;
    spec->query_mode = HISTORIAN_QUERY_RAW;
    spec->sampling_mode = HISTORIAN_SAMPLING_ALL;
    spec->sample_interval_ms = 60000; /* Default: 1 minute */
    spec->max_points = 0;
    spec->filter_good_only = 0;
    spec->filter_apply_range = 0;
    spec->sql_dialect = HISTORIAN_SQL_STANDARD;
    spec->page_offset = 0;
    spec->page_size = 0;
    spec->output_tz_offset_min = 0;
    spec->convert_to_output_tz = 0;
    /* Initialize time range to safe defaults */
    spec->time_range.start_time.epoch_ms = 0;
    spec->time_range.end_time.epoch_ms = 0;
    spec->time_range.start_mode = HISTORIAN_BOUNDARY_INCLUSIVE;
    spec->time_range.end_mode = HISTORIAN_BOUNDARY_INCLUSIVE;
}

int historian_query_spec_validate(const historian_query_spec_t *spec)
{
    if (!spec) return -1;

    /* Must have at least one tag identifier */
    if (spec->tag_id < 0 && spec->tag_filter == NULL &&
        (spec->tag_ids == NULL || spec->tag_count == 0))
        return -2;

    /* Time range: end must be after start */
    if (spec->time_range.end_time.epoch_ms <=
        spec->time_range.start_time.epoch_ms)
        return -3;

    /* Sample interval must be positive for INTERPOLATED/AGGREGATED */
    if ((spec->query_mode == HISTORIAN_QUERY_INTERPOLATED ||
         spec->query_mode == HISTORIAN_QUERY_AGGREGATED) &&
        spec->sample_interval_ms <= 0)
        return -4;

    /* Max points must be reasonable */
    if (spec->max_points > HISTORIAN_MAX_QUERY_ROWS)
        return -5;

    /* Filter value range sanity */
    if (spec->filter_apply_range &&
        spec->filter_value_min > spec->filter_value_max)
        return -6;

    return 0;
}

/* =========================================================================
 * L3 & L4: SQL Query Generation
 * ========================================================================= */

/**
 * Append a quoted string suitable for SQL (basic SQL injection prevention).
 * Escapes single quotes by doubling them per SQL-92 standard.
 */
static int append_sql_string(char *buf, size_t buf_size, size_t *pos,
                              const char *str)
{
    if (!buf || !str) return -1;
    size_t p = *pos;
    /* Opening quote */
    if (p + 1 >= buf_size) return -1;
    buf[p++] = '\'';
    while (*str && p + 2 < buf_size) {
        if (*str == '\'') {
            buf[p++] = '\''; /* Double the quote */
            buf[p++] = '\'';
        } else {
            buf[p++] = *str;
        }
        str++;
    }
    if (p + 1 >= buf_size) return -1;
    buf[p++] = '\'';
    buf[p] = '\0';
    *pos = p;
    return 0;
}

static int append_str(char *buf, size_t buf_size, size_t *pos, const char *s)
{
    if (!buf || !s) return -1;
    size_t len = strlen(s);
    if (*pos + len >= buf_size) return -1;
    memcpy(buf + *pos, s, len);
    *pos += len;
    buf[*pos] = '\0';
    return 0;
}

static int append_int(char *buf, size_t buf_size, size_t *pos, int64_t val)
{
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%lld", (long long)val);
    if (n < 0 || (size_t)n >= sizeof(tmp)) return -1;
    return append_str(buf, buf_size, pos, tmp);
}

/**
 * Generate SQL SELECT for PI OLEDB dialect.
 *
 * PI OLEDB uses a 4-part naming convention:
 *   SELECT tag, time, value FROM [piarchive]..[picomp2]
 *   WHERE tag IN ('t1','t2') AND time BETWEEN 't1' AND 't2'
 */
static int generate_pi_oledb_sql(const historian_query_spec_t *spec,
                                   char *sql_out, size_t buf_size)
{
    size_t pos = 0;

    /* SELECT clause */
    if (append_str(sql_out, buf_size, &pos, "SELECT tag, time, value, status FROM ") ||
        append_str(sql_out, buf_size, &pos, HISTORIAN_PI_TABLE_ARCHIVE)) return -1;

    /* WHERE clause */
    if (append_str(sql_out, buf_size, &pos, " WHERE ")) return -1;

    /* Tag filter */
    if (spec->tag_id >= 0) {
        if (append_str(sql_out, buf_size, &pos, "tag = '")) return -1;
        /* Note: in production, look up tag name from metadata by tag_id */
        if (append_int(sql_out, buf_size, &pos, spec->tag_id)) return -1;
        if (append_str(sql_out, buf_size, &pos, "'")) return -1;
    } else if (spec->tag_filter) {
        if (append_str(sql_out, buf_size, &pos, "tag LIKE ")) return -1;
        if (append_sql_string(sql_out, buf_size, &pos, spec->tag_filter)) return -1;
    } else if (spec->tag_ids && spec->tag_count > 0) {
        if (append_str(sql_out, buf_size, &pos, "tag IN (")) return -1;
        size_t max_items = spec->tag_count;
        if (max_items > HISTORIAN_PI_MAX_IN_ITEMS)
            max_items = HISTORIAN_PI_MAX_IN_ITEMS;
        for (size_t i = 0; i < max_items; i++) {
            if (i > 0) {
                if (append_str(sql_out, buf_size, &pos, ", ")) return -1;
            }
            if (append_int(sql_out, buf_size, &pos, spec->tag_ids[i])) return -1;
        }
        if (append_str(sql_out, buf_size, &pos, ")")) return -1;
    }

    /* Time range */
    if (append_str(sql_out, buf_size, &pos, " AND time BETWEEN ")) return -1;

    char ts_start[32], ts_end[32];
    /* Convert timestamps to PI-compatible string format */
    snprintf(ts_start, sizeof(ts_start), "%lld",
             (long long)(spec->time_range.start_time.epoch_ms / 1000));
    snprintf(ts_end, sizeof(ts_end), "%lld",
             (long long)(spec->time_range.end_time.epoch_ms / 1000));

    if (append_sql_string(sql_out, buf_size, &pos, ts_start) ||
        append_str(sql_out, buf_size, &pos, " AND ") ||
        append_sql_string(sql_out, buf_size, &pos, ts_end)) return -1;

    /* Quality filter */
    if (spec->filter_good_only) {
        if (append_str(sql_out, buf_size, &pos, " AND status = 0")) return -1;
    }

    /* ORDER BY */
    if (spec->direction == HISTORIAN_DIR_BACKWARD) {
        if (append_str(sql_out, buf_size, &pos, " ORDER BY time DESC")) return -1;
    } else {
        if (append_str(sql_out, buf_size, &pos, " ORDER BY time ASC")) return -1;
    }

    /* Pagination */
    if (spec->page_size > 0) {
        if (append_str(sql_out, buf_size, &pos, " OFFSET ")) return -1;
        if (append_int(sql_out, buf_size, &pos,
                       (int64_t)(spec->page_offset * spec->page_size))) return -1;
        if (append_str(sql_out, buf_size, &pos, " ROWS FETCH NEXT ")) return -1;
        if (append_int(sql_out, buf_size, &pos, (int64_t)spec->page_size)) return -1;
        if (append_str(sql_out, buf_size, &pos, " ROWS ONLY")) return -1;
    }

    return (int)pos;
}

/**
 * Generate SQL SELECT for Honeywell PHD SQL dialect.
 */
static int generate_phd_sql(const historian_query_spec_t *spec,
                              char *sql_out, size_t buf_size)
{
    size_t pos = 0;

    if (append_str(sql_out, buf_size, &pos, "SELECT TagName, TimeStamp, Value, Quality FROM ") ||
        append_str(sql_out, buf_size, &pos, HISTORIAN_PHD_TABLE_RAW) ||
        append_str(sql_out, buf_size, &pos, " WHERE ")) return -1;

    /* Tag filter */
    if (spec->tag_id >= 0) {
        if (append_str(sql_out, buf_size, &pos, "TagName = ")) return -1;
        if (append_int(sql_out, buf_size, &pos, spec->tag_id)) return -1;
    } else if (spec->tag_filter) {
        if (append_str(sql_out, buf_size, &pos, "TagName LIKE ")) return -1;
        if (append_sql_string(sql_out, buf_size, &pos, spec->tag_filter)) return -1;
    }

    /* Time range using UNIX epoch seconds for PHD */
    if (append_str(sql_out, buf_size, &pos, " AND TimeStamp >= ")) return -1;
    if (append_int(sql_out, buf_size, &pos,
                   spec->time_range.start_time.epoch_ms / 1000)) return -1;
    if (append_str(sql_out, buf_size, &pos, " AND TimeStamp <= ")) return -1;
    if (append_int(sql_out, buf_size, &pos,
                   spec->time_range.end_time.epoch_ms / 1000)) return -1;

    if (append_str(sql_out, buf_size, &pos, " ORDER BY TimeStamp ASC")) return -1;

    return (int)pos;
}

/**
 * Generate ANSI SQL-92 standard query.
 */
static int generate_standard_sql(const historian_query_spec_t *spec,
                                   char *sql_out, size_t buf_size)
{
    size_t pos = 0;

    if (append_str(sql_out, buf_size, &pos,
                   "SELECT tag_id, epoch_ms, value, quality FROM historian_data WHERE ")) return -1;

    if (spec->tag_id >= 0) {
        if (append_str(sql_out, buf_size, &pos, "tag_id = ")) return -1;
        if (append_int(sql_out, buf_size, &pos, spec->tag_id)) return -1;
    }

    /* Time range with boundary modes */
    const char *start_op = (spec->time_range.start_mode == HISTORIAN_BOUNDARY_EXCLUSIVE)
                           ? " > " : " >= ";
    const char *end_op   = (spec->time_range.end_mode == HISTORIAN_BOUNDARY_EXCLUSIVE)
                           ? " < " : " <= ";

    if (append_str(sql_out, buf_size, &pos, " AND epoch_ms")) return -1;
    if (append_str(sql_out, buf_size, &pos, start_op)) return -1;
    if (append_int(sql_out, buf_size, &pos,
                   spec->time_range.start_time.epoch_ms)) return -1;
    if (append_str(sql_out, buf_size, &pos, " AND epoch_ms")) return -1;
    if (append_str(sql_out, buf_size, &pos, end_op)) return -1;
    if (append_int(sql_out, buf_size, &pos,
                   spec->time_range.end_time.epoch_ms)) return -1;

    /* Value range filter */
    if (spec->filter_apply_range) {
        if (append_str(sql_out, buf_size, &pos, " AND value BETWEEN ")) return -1;
        char vmin[32], vmax[32];
        snprintf(vmin, sizeof(vmin), "%g", spec->filter_value_min);
        snprintf(vmax, sizeof(vmax), "%g", spec->filter_value_max);
        if (append_str(sql_out, buf_size, &pos, vmin)) return -1;
        if (append_str(sql_out, buf_size, &pos, " AND ")) return -1;
        if (append_str(sql_out, buf_size, &pos, vmax)) return -1;
    }

    /* Quality filter */
    if (spec->filter_good_only) {
        if (append_str(sql_out, buf_size, &pos, " AND (quality & 0x00C0) = 0x00C0")) return -1;
    }

    /* ORDER BY */
    if (spec->direction == HISTORIAN_DIR_BACKWARD) {
        if (append_str(sql_out, buf_size, &pos, " ORDER BY epoch_ms DESC")) return -1;
    } else {
        if (append_str(sql_out, buf_size, &pos, " ORDER BY epoch_ms ASC")) return -1;
    }

    /* LIMIT / OFFSET */
    if (spec->max_points > 0) {
        if (append_str(sql_out, buf_size, &pos, " LIMIT ")) return -1;
        if (append_int(sql_out, buf_size, &pos, (int64_t)spec->max_points)) return -1;
    }
    if (spec->page_offset > 0 && spec->page_size > 0) {
        if (append_str(sql_out, buf_size, &pos, " OFFSET ")) return -1;
        if (append_int(sql_out, buf_size, &pos,
                       (int64_t)(spec->page_offset * spec->page_size))) return -1;
    }

    return (int)pos;
}

int historian_generate_sql(const historian_query_spec_t *spec,
                            char *sql_out, size_t buf_size)
{
    if (!spec || !sql_out || buf_size == 0) return -1;
    memset(sql_out, 0, buf_size);

    switch (spec->sql_dialect) {
    case HISTORIAN_SQL_PI_OLEDB:
        return generate_pi_oledb_sql(spec, sql_out, buf_size);
    case HISTORIAN_SQL_PHD:
        return generate_phd_sql(spec, sql_out, buf_size);
    case HISTORIAN_SQL_STANDARD:
    case HISTORIAN_SQL_ASPEN:
    case HISTORIAN_SQL_CUSTOM:
    default:
        return generate_standard_sql(spec, sql_out, buf_size);
    }
}

/* =========================================================================
 * L4: Parameterized SQL Queries (SQL Injection Prevention)
 * ========================================================================= */

int historian_generate_parameterized_sql(const historian_query_spec_t *spec,
                                          char *sql_out, size_t buf_size,
                                          double *params_out, size_t param_cap,
                                          size_t *param_count)
{
    if (!spec || !sql_out || buf_size == 0) return -1;

    size_t pos = 0;
    size_t pc = 0;
    memset(sql_out, 0, buf_size);

    /* Parameterized query uses ? placeholders instead of literal values */
    if (append_str(sql_out, buf_size, &pos,
                   "SELECT tag_id, epoch_ms, value, quality "
                   "FROM historian_data WHERE tag_id = ? AND epoch_ms >= ? "
                   "AND epoch_ms <= ?")) return -1;

    /* Fill parameter array */
    if (params_out && pc < param_cap) params_out[pc++] = (double)spec->tag_id;
    if (params_out && pc < param_cap)
        params_out[pc++] = (double)spec->time_range.start_time.epoch_ms;
    if (params_out && pc < param_cap)
        params_out[pc++] = (double)spec->time_range.end_time.epoch_ms;

    if (spec->filter_apply_range) {
        if (append_str(sql_out, buf_size, &pos, " AND value BETWEEN ? AND ?")) return -1;
        if (params_out && pc < param_cap) params_out[pc++] = spec->filter_value_min;
        if (params_out && pc < param_cap) params_out[pc++] = spec->filter_value_max;
    }

    if (spec->max_points > 0) {
        if (append_str(sql_out, buf_size, &pos, " LIMIT ?")) return -1;
        if (params_out && pc < param_cap) params_out[pc++] = (double)spec->max_points;
    }

    if (param_count) *param_count = pc;
    return (int)pos;
}

/* =========================================================================
 * L2: Core Retrieval Functions
 * ========================================================================= */

int historian_retrieve_raw(const historian_query_spec_t *spec,
                            historian_result_set_t *result)
{
    if (!spec || !result) return -1;

    /* Validate the query specification */
    int valid = historian_query_spec_validate(spec);
    if (valid != 0) return valid;

    /* Generate the SQL query */
    char sql[HISTORIAN_SQL_MAX_LEN];
    int sql_len = historian_generate_sql(spec, sql, sizeof(sql));
    if (sql_len < 0) return sql_len;

    /* In a real implementation, this would execute the SQL against the
     * historian database, parse the result set, and populate 'result'.
     * For this reference implementation, we demonstrate the full
     * SQL generation pipeline and expected result structure.
     *
     * The generated SQL (accessible via sql buffer) can be executed
     * against any ODBC/JDBC driver supporting the target dialect.
     */

    /* Simulate a result with sample data for testing/demonstration */
    result->count = 0;
    /* Placeholder: in production, execute SQL and parse results */
    return 0;
}

int historian_retrieve_interpolated(const historian_query_spec_t *spec,
                                     historian_result_set_t *result)
{
    if (!spec || !result) return -1;

    int valid = historian_query_spec_validate(spec);
    if (valid != 0) return valid;

    /* For interpolated retrieval, first get raw data, then resample */
    historian_query_spec_t raw_spec = *spec;
    raw_spec.query_mode = HISTORIAN_QUERY_RAW;
    raw_spec.sampling_mode = HISTORIAN_SAMPLING_ALL;

    int ret = historian_retrieve_raw(&raw_spec, result);
    if (ret < 0) return ret;

    /* Interpolation logic would resample result->points at the
     * specified interval using the chosen interpolation method.
     * The raw points serve as control points for interpolation.
     */
    return 0;
}

int historian_retrieve_aggregated(const historian_query_spec_t *spec,
                                   historian_result_set_t *result)
{
    if (!spec || !result) return -1;

    int valid = historian_query_spec_validate(spec);
    if (valid != 0) return valid;

    /* Aggregation retrieval: SELECT agg_fn(value) ...
     * GROUP BY (epoch_ms / bucket_size)
     */
    result->count = 0;
    return 0;
}

int historian_retrieve_snapshot(const historian_query_spec_t *spec,
                                 historian_result_set_t *result)
{
    if (!spec || !result) return -1;

    /* Snapshot: SELECT value, time FROM snapshot WHERE tag = ? */
    result->count = 0;
    return 0;
}

/* =========================================================================
 * L2: Multi-tag Retrieval
 * ========================================================================= */

int historian_retrieve_multi_tag(const historian_query_spec_t *spec,
                                  historian_result_set_t *result)
{
    if (!spec || !result) return -1;

    if (!spec->tag_ids || spec->tag_count == 0) {
        /* Fall back to single-tag retrieval */
        return historian_retrieve_raw(spec, result);
    }

    /* For multi-tag queries, we generate a UNION ALL or use IN clause.
     * PI OLEDB: tag IN ('t1','t2','t3')
     * Standard SQL: WHERE tag_id IN (1, 2, 3) ORDER BY epoch_ms
     */
    char sql[HISTORIAN_SQL_MAX_LEN];
    int sql_len = historian_generate_sql(spec, sql, sizeof(sql));
    if (sql_len < 0) return sql_len;

    result->count = 0;
    return 0;
}

/* =========================================================================
 * L5: Query Cost Estimation and Optimization
 * ========================================================================= */

int64_t historian_estimate_row_count(const historian_query_spec_t *spec)
{
    if (!spec) return -1;

    /* Estimate based on:
     *   1. Time range duration
     *   2. Typical sample rate (inverse of archive_period_ms or guessed)
     *   3. Compression ratio (typical: 10:1)

     * Formula: estimated_rows = (duration_ms / typical_sample_interval)
     *                            * number_of_tags / compression_ratio
     */

    int64_t duration_ms = spec->time_range.end_time.epoch_ms -
                           spec->time_range.start_time.epoch_ms;

    if (duration_ms <= 0) return 0;

    /* Assume 1-second sample rate as a conservative estimate */
    int64_t typical_interval_ms = 1000;
    int64_t raw_estimate = duration_ms / typical_interval_ms;

    /* Apply compression factor (typical historian: 10:1 compression) */
    double compression_factor = 10.0;
    int64_t compressed_estimate = (int64_t)(raw_estimate / compression_factor);

    /* Adjust for number of tags */
    size_t tag_multiplier = (spec->tag_ids && spec->tag_count > 0)
                             ? spec->tag_count : 1;
    int64_t estimate = compressed_estimate * (int64_t)tag_multiplier;

    /* Cap at reasonable limits */
    if (estimate > HISTORIAN_MAX_QUERY_ROWS)
        estimate = HISTORIAN_MAX_QUERY_ROWS;

    return (estimate > 0) ? estimate : 1;
}

size_t historian_suggest_page_size(const historian_query_spec_t *spec)
{
    /* Suggest a page size that balances memory usage and round-trips.
     *
     * A typical historian data point is ~40 bytes (tag_id:4, ts:16,
     * value:8, quality:2, padding).
     *
     * Target ~1 MB per page:
     *   1 MB / 40 bytes = 25000 points per page
     *
     * For mobile/embedded: 1000 points
     * For server: 50000 points
     */

    /* Estimate row count to calibrate page size */
    int64_t estimated = historian_estimate_row_count(spec);
    if (estimated <= 0) return 1000;

    size_t target_page_bytes = 1048576; /* 1 MB */
    size_t bytes_per_row = 48; /* Conservative estimate */

    size_t page_size = target_page_bytes / bytes_per_row;

    /* At minimum return 100 points, at most 100000 */
    if (page_size < 100) page_size = 100;
    if (page_size > 100000) page_size = 100000;

    /* Don't page if total is small */
    if ((int64_t)page_size > estimated) page_size = (size_t)estimated;

    return page_size;
}
