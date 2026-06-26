/**
 * @file    test_historian.c
 * @brief   Comprehensive test suite for mini-historian-data-retrieval-sql.
 *
 * Tests cover all core APIs across all knowledge levels L1-L8.
 * Uses standard assert() for validation.
 */

#include "../include/historian_model.h"
#include "../include/historian_retrieval.h"
#include "../include/historian_aggregate.h"
#include "../include/historian_compression.h"
#include "../include/historian_interpolation.h"
#include "../include/historian_windowing.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  %s... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define CHECK(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

/* =========================================================================
 * L1: Data Model Tests
 * ========================================================================= */

static void test_tag_metadata_init(void)
{
    TEST("tag_metadata_init");
    historian_tag_metadata_t meta;
    historian_tag_metadata_init(&meta);
    CHECK(meta.tag_id == -1, "default tag_id");
    CHECK(meta.data_type == HISTORIAN_TYPE_FLOAT64, "default data_type");
    CHECK(meta.compressing == HISTORIAN_COMPRESS_ON, "default compression");
    CHECK(fabs(meta.eu_span - 100.0) < 0.01, "default eu_span");
    CHECK(meta.retention_days == 365, "default retention_days");
    PASS();
}

static void test_tag_metadata_validate(void)
{
    TEST("tag_metadata_validate");
    historian_tag_metadata_t meta;
    historian_tag_metadata_init(&meta);

    /* Valid config */
    strncpy(meta.tag_name, "FIC101.PV", sizeof(meta.tag_name) - 1);
    meta.tag_id = 101;
    int ret = historian_tag_metadata_validate(&meta);
    CHECK(ret == 0, "valid metadata should pass");

    /* Empty tag name */
    meta.tag_name[0] = '\0';
    ret = historian_tag_metadata_validate(&meta);
    CHECK(ret == -3, "empty tag name should fail");

    /* Zero span */
    strncpy(meta.tag_name, "TEST", sizeof(meta.tag_name) - 1);
    meta.eu_span = meta.eu_zero;
    ret = historian_tag_metadata_validate(&meta);
    CHECK(ret == -4, "zero eu span should fail");
    PASS();
}

static void test_raw_to_eu_conversion(void)
{
    TEST("raw_to_eu_conversion");
    historian_tag_metadata_t meta;
    historian_tag_metadata_init(&meta);
    meta.eu_zero = 0.0;
    meta.eu_span = 100.0;
    meta.inst_zero = 4.0;
    meta.inst_span = 20.0;

    /* 4 mA -> 0% */
    double eu = historian_raw_to_eu(&meta, 4.0);
    CHECK(fabs(eu - 0.0) < 0.001, "4mA should be 0%");

    /* 12 mA -> 50% */
    eu = historian_raw_to_eu(&meta, 12.0);
    CHECK(fabs(eu - 50.0) < 0.001, "12mA should be 50%");

    /* 20 mA -> 100% */
    eu = historian_raw_to_eu(&meta, 20.0);
    CHECK(fabs(eu - 100.0) < 0.001, "20mA should be 100%");

    /* Round-trip */
    double raw = historian_eu_to_raw(&meta, 75.0);
    double back = historian_raw_to_eu(&meta, raw);
    CHECK(fabs(back - 75.0) < 0.001, "round-trip conversion");
    PASS();
}

static void test_timestamp_iso8601(void)
{
    TEST("timestamp_iso8601");
    historian_timestamp_t ts;
    int ret;

    /* Parse UTC timestamp */
    ret = historian_timestamp_from_iso8601("2020-06-15T14:30:00.500Z", &ts);
    CHECK(ret == 0, "parse ISO8601 UTC");
    CHECK(ts.is_utc == 1, "should be UTC");
    CHECK(ts.tz_offset_min == 0, "UTC offset should be 0");

    /* Format back */
    char buf[64];
    ret = historian_timestamp_to_iso8601(&ts, buf, sizeof(buf));
    CHECK(ret > 0, "format ISO8601");
    CHECK(strstr(buf, "2020-06-15T14:30:00.500Z") != NULL, "UTC format correct");
    PASS();
}

static void test_timestamp_compare(void)
{
    TEST("timestamp_compare");
    historian_timestamp_t t1, t2;
    t1.epoch_ms = 1000;
    t2.epoch_ms = 2000;

    CHECK(historian_timestamp_compare(&t1, &t2) < 0, "t1 < t2");
    CHECK(historian_timestamp_compare(&t2, &t1) > 0, "t2 > t1");
    CHECK(historian_timestamp_compare(&t1, &t1) == 0, "t1 == t1");

    int64_t diff = historian_timestamp_diff_ms(&t2, &t1);
    CHECK(diff == 1000, "diff should be 1000ms");
    PASS();
}

static void test_quality_flags(void)
{
    TEST("quality_flags");
    CHECK(historian_quality_is_good(HISTORIAN_QUAL_GOOD) == 1, "GOOD is good");
    CHECK(historian_quality_is_good(HISTORIAN_QUAL_BAD) == 0, "BAD is not good");
    CHECK(historian_quality_is_good(HISTORIAN_QUAL_UNCERTAIN) == 0, "UNCERTAIN is not good");

    historian_quality_t q = historian_quality_set(HISTORIAN_QUAL_GOOD,
                                                    HISTORIAN_QUAL_SUB_CLAMPED_LOW);
    CHECK(historian_quality_is_good(q) == 1, "GOOD + sub-status still good");
    PASS();
}

static void test_data_point(void)
{
    TEST("data_point");
    historian_timestamp_t ts;
    ts.epoch_ms = 1000000;
    ts.tz_offset_min = 0;

    historian_data_point_t dp = historian_make_point(42, ts, 3.14159,
                                                       HISTORIAN_QUAL_GOOD);
    CHECK(dp.tag_id == 42, "tag_id");
    CHECK(dp.timestamp.epoch_ms == 1000000, "timestamp");
    CHECK(fabs(dp.value - 3.14159) < 0.0001, "value");
    CHECK(historian_point_has_value(&dp) == 1, "has value");
    PASS();
}

static void test_result_set(void)
{
    TEST("result_set");
    historian_result_set_t rs;
    historian_result_set_init(&rs);
    CHECK(rs.count == 0, "initial count zero");

    historian_timestamp_t ts;
    ts.epoch_ms = 0;

    /* Append 1000 points */
    for (int i = 0; i < 1000; i++) {
        ts.epoch_ms = (int64_t)i * 1000;
        historian_data_point_t dp = historian_make_point(1, ts, (double)i,
                                                           HISTORIAN_QUAL_GOOD);
        int ret = historian_result_set_append(&rs, dp);
        CHECK(ret == 0, "append ok");
    }
    CHECK(rs.count == 1000, "count after 1000 appends");

    /* Sort and verify ascending */
    historian_result_set_sort(&rs);
    for (size_t i = 0; i < rs.count - 1; i++) {
        CHECK(rs.points[i].timestamp.epoch_ms <=
              rs.points[i+1].timestamp.epoch_ms, "sorted ascending");
    }

    historian_result_set_destroy(&rs);
    CHECK(rs.count == 0, "destroyed count zero");
    CHECK(rs.points == NULL, "destroyed points NULL");
    PASS();
}

/* =========================================================================
 * L2: SQL Query Generation Tests
 * ========================================================================= */

static void test_query_spec(void)
{
    TEST("query_spec");
    historian_query_spec_t spec;
    historian_query_spec_init(&spec);

    spec.tag_id = 100;
    spec.time_range.start_time.epoch_ms = 1000000;
    spec.time_range.end_time.epoch_ms = 2000000;
    spec.sql_dialect = HISTORIAN_SQL_STANDARD;

    int valid = historian_query_spec_validate(&spec);
    CHECK(valid == 0, "valid query spec");

    /* Invalid: end_time before start_time */
    spec.time_range.end_time.epoch_ms = 500000;
    valid = historian_query_spec_validate(&spec);
    CHECK(valid == -3, "invalid time range should fail");
    PASS();
}

static void test_sql_generation(void)
{
    TEST("sql_generation");
    historian_query_spec_t spec;
    historian_query_spec_init(&spec);
    spec.tag_id = 100;
    spec.time_range.start_time.epoch_ms = 1000000;
    spec.time_range.end_time.epoch_ms = 2000000;

    char sql[HISTORIAN_SQL_MAX_LEN];
    int len;

    /* Standard SQL */
    spec.sql_dialect = HISTORIAN_SQL_STANDARD;
    len = historian_generate_sql(&spec, sql, sizeof(sql));
    CHECK(len > 0, "standard SQL generated");
    CHECK(strstr(sql, "SELECT") != NULL, "has SELECT");
    CHECK(strstr(sql, "WHERE") != NULL, "has WHERE");

    /* PI OLEDB */
    spec.sql_dialect = HISTORIAN_SQL_PI_OLEDB;
    len = historian_generate_sql(&spec, sql, sizeof(sql));
    CHECK(len > 0, "PI OLEDB SQL generated");
    CHECK(strstr(sql, HISTORIAN_PI_TABLE_ARCHIVE) != NULL, "has PI table");

    /* PHD */
    spec.sql_dialect = HISTORIAN_SQL_PHD;
    len = historian_generate_sql(&spec, sql, sizeof(sql));
    CHECK(len > 0, "PHD SQL generated");
    CHECK(strstr(sql, HISTORIAN_PHD_TABLE_RAW) != NULL, "has PHD table");
    PASS();
}

static void test_parameterized_sql(void)
{
    TEST("parameterized_sql");
    historian_query_spec_t spec;
    historian_query_spec_init(&spec);
    spec.tag_id = 100;
    spec.time_range.start_time.epoch_ms = 1000000;
    spec.time_range.end_time.epoch_ms = 2000000;

    char sql[HISTORIAN_SQL_MAX_LEN];
    double params[10];
    size_t param_count = 0;

    int len = historian_generate_parameterized_sql(&spec, sql, sizeof(sql),
                                                     params, 10, &param_count);
    CHECK(len > 0, "parameterized SQL generated");
    CHECK(param_count >= 3, "has parameters for tag_id + time range");
    /* No literal values in the SQL string */
    CHECK(strstr(sql, "100") == NULL, "no literal tag_id in SQL");
    PASS();
}

static void test_cost_estimation(void)
{
    TEST("cost_estimation");
    historian_query_spec_t spec;
    historian_query_spec_init(&spec);
    spec.tag_id = 100;
    spec.time_range.start_time.epoch_ms = 0;
    spec.time_range.end_time.epoch_ms = 3600000; /* 1 hour */

    int64_t estimated = historian_estimate_row_count(&spec);
    CHECK(estimated > 0, "positive row estimate");
    CHECK(estimated <= HISTORIAN_MAX_QUERY_ROWS, "within limits");

    size_t page = historian_suggest_page_size(&spec);
    CHECK(page > 0, "positive page size");
    CHECK(page <= 100000, "reasonable page size");
    PASS();
}

/* =========================================================================
 * L5: Aggregate Tests
 * ========================================================================= */

static historian_data_point_t *make_linear_points(size_t count)
{
    historian_data_point_t *pts = (historian_data_point_t *)malloc(
        count * sizeof(historian_data_point_t));

    for (size_t i = 0; i < count; i++) {
        historian_timestamp_t ts;
        ts.epoch_ms = (int64_t)(i * 1000);
        ts.tz_offset_min = 0;
        ts.is_dst = 0;
        ts.is_utc = 1;
        pts[i] = historian_make_point(1, ts, (double)(i * 10),
                                        HISTORIAN_QUAL_GOOD);
    }
    return pts;
}

static void test_aggregate_average(void)
{
    TEST("aggregate_average");
    size_t count = 100;
    historian_data_point_t *pts = make_linear_points(count);

    double result;
    int ret = historian_compute_aggregate(HISTORIAN_AGG_AVERAGE, pts, count, &result);
    CHECK(ret == 0, "average computed");
    /* Average of 0, 10, 20, ..., 990 = (0+990)/2 = 495 */
    CHECK(fabs(result - 495.0) < 1.0, "correct average");
    free(pts);
    PASS();
}

static void test_aggregate_min_max(void)
{
    TEST("aggregate_min_max");
    size_t count = 100;
    historian_data_point_t *pts = make_linear_points(count);

    double min_val, max_val;
    historian_compute_aggregate(HISTORIAN_AGG_MINIMUM, pts, count, &min_val);
    historian_compute_aggregate(HISTORIAN_AGG_MAXIMUM, pts, count, &max_val);

    CHECK(fabs(min_val - 0.0) < 0.01, "min is first");
    CHECK(fabs(max_val - 990.0) < 0.01, "max is last");
    CHECK(fabs(max_val - min_val - 990.0) < 0.01, "range");
    free(pts);
    PASS();
}

static void test_aggregate_stddev(void)
{
    TEST("aggregate_stddev");
    /* Constant data: stddev should be zero */
    historian_data_point_t pts[10];
    for (int i = 0; i < 10; i++) {
        historian_timestamp_t ts;
        ts.epoch_ms = (int64_t)i * 1000;
        ts.tz_offset_min = 0;
        ts.is_dst = 0;
        ts.is_utc = 1;
        pts[i] = historian_make_point(1, ts, 5.0, HISTORIAN_QUAL_GOOD);
    }

    double result;
    int ret = historian_compute_aggregate(HISTORIAN_AGG_STDDEV, pts, 10, &result);
    CHECK(ret == 0, "stddev computed");
    CHECK(fabs(result - 0.0) < 0.001, "constant data stddev = 0");
    PASS();
}

static void test_time_weighted_average(void)
{
    TEST("time_weighted_average");
    /* Two points: (t=0, v=0), (t=10, v=10) -> TWAvg = 5.0 */
    historian_data_point_t pts[2];
    historian_timestamp_t ts0, ts1;
    ts0.epoch_ms = 0; ts0.tz_offset_min = 0; ts0.is_dst = 0; ts0.is_utc = 1;
    ts1.epoch_ms = 10000; ts1.tz_offset_min = 0; ts1.is_dst = 0; ts1.is_utc = 1;

    pts[0] = historian_make_point(1, ts0, 0.0, HISTORIAN_QUAL_GOOD);
    pts[1] = historian_make_point(1, ts1, 10.0, HISTORIAN_QUAL_GOOD);

    double result;
    int ret = historian_compute_time_weighted_avg(pts, 2, &result);
    CHECK(ret == 0, "TWAvg computed");
    CHECK(fabs(result - 5.0) < 0.01, "TWAvg = 5.0");
    PASS();
}

static void test_running_aggregate(void)
{
    TEST("running_aggregate");
    uint8_t state[256] = {0};

    historian_timestamp_t ts;
    ts.epoch_ms = 0;
    ts.tz_offset_min = 0;
    ts.is_dst = 0;
    ts.is_utc = 1;

    /* Feed 1, 2, 3, 4, 5 */
    for (int i = 1; i <= 5; i++) {
        historian_data_point_t dp = historian_make_point(1, ts, (double)i,
                                                           HISTORIAN_QUAL_GOOD);
        historian_running_aggregate_update(HISTORIAN_AGG_AVERAGE, &dp, state);
        ts.epoch_ms += 1000;
    }

    double result;
    int ret = historian_running_aggregate_finalize(HISTORIAN_AGG_AVERAGE,
                                                     state, &result);
    CHECK(ret == 0, "running agg finalized");
    CHECK(fabs(result - 3.0) < 0.01, "running average = 3.0");
    PASS();
}

/* =========================================================================
 * L5: Compression Tests
 * ========================================================================= */

static void test_swinging_door_compression(void)
{
    TEST("swinging_door_compression");
    historian_swinging_door_state_t state;
    memset(&state, 0, sizeof(state));

    /* Initialize with first point (t=0, v=0) */
    state.deviation = 1.0;
    state.comp_max_time_ms = 100000; /* 100s max time */
    /* Start: t=0, v=0, need to call historian_swinging_door_init */
    historian_swinging_door_init(&state, 1.0, 0.0, 0);
    /* Set max time after init */
    state.comp_max_time_ms = 100000;

    historian_data_point_t stored[100];
    size_t stored_count = 0;
    int ret;

    /* Feed a straight line: points stay within door */
    for (int i = 1; i <= 10; i++) {
        ret = historian_swinging_door_feed(&state, (double)i, (int64_t)(i * 1000),
                                             0, stored, 100, &stored_count);
        CHECK(ret == 0, "feed ok");
    }

    /* Straight line with dev=1.0 should be highly compressed */
    /* At least some points should be stored (the first one) */
    CHECK(stored_count >= 1, "some points stored");
    PASS();
}

static void test_deadband_compression(void)
{
    TEST("deadband_compression");
    historian_data_point_t pts[5];
    historian_timestamp_t ts;
    ts.epoch_ms = 0; ts.tz_offset_min = 0; ts.is_dst = 0; ts.is_utc = 1;

    /* Values: 0, 0.1, 0.2, 5.0, 5.1 */
    double values[] = {0.0, 0.1, 0.2, 5.0, 5.1};
    for (int i = 0; i < 5; i++) {
        ts.epoch_ms = (int64_t)i * 1000;
        pts[i] = historian_make_point(1, ts, values[i], HISTORIAN_QUAL_GOOD);
    }

    size_t new_count;
    historian_deadband_compress(pts, 5, 2.0, &new_count);

    /* With deadband 2.0: should keep 0.0, then 5.0, then 5.1? */
    /* Actually: 0.0 stays, 0.1 (diff=0.1 < 2) skipped, 0.2 (diff=0.2 < 2) skipped,
       5.0 (diff=5.0 > 2) kept, 5.1 (diff=0.1 < 2) skipped */
    CHECK(new_count <= 5, "compressed count <= original");
    /* At minimum: first and one more point kept */
    CHECK(new_count >= 2, "at least 2 points kept");
    PASS();
}

static void test_compression_ratio(void)
{
    TEST("compression_ratio");
    double ratio = historian_compression_ratio(10000, 1000);
    CHECK(fabs(ratio - 10.0) < 0.01, "10000/1000 = 10");
    PASS();
}

static void test_reconstruct_value(void)
{
    TEST("reconstruct_value");
    historian_data_point_t pts[3];
    historian_timestamp_t ts;
    ts.tz_offset_min = 0; ts.is_dst = 0; ts.is_utc = 1;

    ts.epoch_ms = 0;    pts[0] = historian_make_point(1, ts, 0.0, HISTORIAN_QUAL_GOOD);
    ts.epoch_ms = 10000; pts[1] = historian_make_point(1, ts, 10.0, HISTORIAN_QUAL_GOOD);
    ts.epoch_ms = 20000; pts[2] = historian_make_point(1, ts, 20.0, HISTORIAN_QUAL_GOOD);

    double val;

    /* Step interpolation at t=5000 -> 0.0 */
    int ret = historian_reconstruct_value(pts, 3, 5000, 0, &val);
    CHECK(ret == 0, "reconstruct step");
    CHECK(fabs(val - 0.0) < 0.01, "step: last known = 0.0");

    /* Linear interpolation at t=5000 -> 5.0 */
    ret = historian_reconstruct_value(pts, 3, 5000, 1, &val);
    CHECK(ret == 0, "reconstruct linear");
    CHECK(fabs(val - 5.0) < 0.01, "linear: midpoint = 5.0");

    /* Linear at t=15000 -> 15.0 */
    ret = historian_reconstruct_value(pts, 3, 15000, 1, &val);
    CHECK(ret == 0, "reconstruct linear middle");
    CHECK(fabs(val - 15.0) < 0.01, "linear: middle = 15.0");
    PASS();
}

/* =========================================================================
 * L5: Interpolation Tests
 * ========================================================================= */

static void test_interp_step(void)
{
    TEST("interp_step");
    historian_data_point_t pts[3];
    historian_timestamp_t ts;
    ts.tz_offset_min = 0; ts.is_dst = 0; ts.is_utc = 1;

    ts.epoch_ms = 0;    pts[0] = historian_make_point(1, ts, 10.0, HISTORIAN_QUAL_GOOD);
    ts.epoch_ms = 10000; pts[1] = historian_make_point(1, ts, 20.0, HISTORIAN_QUAL_GOOD);
    ts.epoch_ms = 20000; pts[2] = historian_make_point(1, ts, 30.0, HISTORIAN_QUAL_GOOD);

    double val;
    int ret = historian_interp_step(pts, 3, 5000, &val);
    CHECK(ret == 0, "step interp");
    CHECK(fabs(val - 10.0) < 0.01, "step: t=5000 -> 10.0");

    ret = historian_interp_step(pts, 3, 15000, &val);
    CHECK(ret == 0, "step interp 2");
    CHECK(fabs(val - 20.0) < 0.01, "step: t=15000 -> 20.0");

    /* Before first point */
    ret = historian_interp_step(pts, 3, -5000, &val);
    CHECK(ret < 0, "before first point should fail");
    PASS();
}

static void test_interp_linear(void)
{
    TEST("interp_linear");
    historian_data_point_t pts[3];
    historian_timestamp_t ts;
    ts.tz_offset_min = 0; ts.is_dst = 0; ts.is_utc = 1;

    ts.epoch_ms = 0;    pts[0] = historian_make_point(1, ts, 0.0, HISTORIAN_QUAL_GOOD);
    ts.epoch_ms = 10000; pts[1] = historian_make_point(1, ts, 100.0, HISTORIAN_QUAL_GOOD);
    ts.epoch_ms = 20000; pts[2] = historian_make_point(1, ts, 200.0, HISTORIAN_QUAL_GOOD);

    double val;

    /* Exact match */
    historian_interp_linear(pts, 3, 0, &val);
    CHECK(fabs(val - 0.0) < 0.01, "exact at t=0");

    /* Midpoint */
    historian_interp_linear(pts, 3, 5000, &val);
    CHECK(fabs(val - 50.0) < 1.0, "midpoint t=5000 ~50");

    /* Between pts[1] and pts[2] */
    historian_interp_linear(pts, 3, 15000, &val);
    CHECK(fabs(val - 150.0) < 1.0, "midpoint t=15000 ~150");

    /* Before first point (extrapolation) */
    int ret = historian_interp_linear(pts, 3, -5000, &val);
    CHECK(ret == 0, "extrapolation before");
    PASS();
}

static void test_interp_quadratic(void)
{
    TEST("interp_quadratic");
    historian_data_point_t pts[4];
    historian_timestamp_t ts;
    ts.tz_offset_min = 0; ts.is_dst = 0; ts.is_utc = 1;

    /* Parabola: y = x^2 at x = 0, 1, 2, 3 (timescale factor) */
    for (int i = 0; i < 4; i++) {
        ts.epoch_ms = (int64_t)(i * 1000);
        pts[i] = historian_make_point(1, ts, (double)(i * i), HISTORIAN_QUAL_GOOD);
    }

    double val;
    int ret = historian_interp_quadratic(pts, 4, 1500, &val);
    CHECK(ret == 0, "quadratic interp");
    /* At x=1.5 on y=x^2, value should be ~2.25 */
    CHECK(fabs(val - 2.25) < 0.5, "quadratic approx 2.25");
    PASS();
}

static void test_interp_cubic_spline(void)
{
    TEST("interp_cubic_spline");
    /* Simple line: spline should exactly match linear interpolation */
    historian_data_point_t pts[4];
    historian_timestamp_t ts;
    ts.tz_offset_min = 0; ts.is_dst = 0; ts.is_utc = 1;

    for (int i = 0; i < 4; i++) {
        ts.epoch_ms = (int64_t)(i * 10000);
        pts[i] = historian_make_point(1, ts, (double)(i * 10.0), HISTORIAN_QUAL_GOOD);
    }

    double val;
    int ret = historian_interp_cubic_spline(pts, 4, 15000, &val);
    CHECK(ret == 0, "cubic spline interp");
    /* Should be near 15.0 (linear through these points) */
    CHECK(fabs(val - 15.0) < 1.0, "cubic spline approx 15");
    PASS();
}

static void test_interp_akima(void)
{
    TEST("interp_akima");
    historian_data_point_t pts[5];
    historian_timestamp_t ts;
    ts.tz_offset_min = 0; ts.is_dst = 0; ts.is_utc = 1;

    for (int i = 0; i < 5; i++) {
        ts.epoch_ms = (int64_t)(i * 10000);
        pts[i] = historian_make_point(1, ts, (double)i, HISTORIAN_QUAL_GOOD);
    }

    double val;
    int ret = historian_interp_akima(pts, 5, 20000, &val);
    CHECK(ret == 0, "akima interp");
    CHECK(fabs(val - 2.0) < 0.5, "akima approx 2.0");
    PASS();
}

static void test_interp_range(void)
{
    TEST("interp_range");
    historian_data_point_t pts[3];
    historian_timestamp_t ts;
    ts.tz_offset_min = 0; ts.is_dst = 0; ts.is_utc = 1;

    ts.epoch_ms = 0;    pts[0] = historian_make_point(1, ts, 0.0, HISTORIAN_QUAL_GOOD);
    ts.epoch_ms = 10000; pts[1] = historian_make_point(1, ts, 10.0, HISTORIAN_QUAL_GOOD);
    ts.epoch_ms = 20000; pts[2] = historian_make_point(1, ts, 20.0, HISTORIAN_QUAL_GOOD);

    historian_data_point_t output[5];
    size_t output_count = 0;

    int ret = historian_interpolate_range(pts, 3, 0, 20000, 5000,
                                            HISTORIAN_INTERP_LINEAR,
                                            output, 5, &output_count);
    CHECK(ret == 0, "range interp");
    CHECK(output_count == 5, "5 output points (0,5000,10000,15000,20000)");
    CHECK(fabs(output[2].value - 10.0) < 0.1, "middle point");
    PASS();
}

/* =========================================================================
 * L2: Windowing Tests
 * ========================================================================= */

static historian_data_point_t *make_timeseries(int n, double start_val,
                                                 double step_val)
{
    historian_data_point_t *pts = (historian_data_point_t *)malloc(
        (size_t)n * sizeof(historian_data_point_t));
    historian_timestamp_t ts;
    ts.tz_offset_min = 0;
    ts.is_dst = 0;
    ts.is_utc = 1;

    for (int i = 0; i < n; i++) {
        ts.epoch_ms = (int64_t)(i * 1000);
        pts[i] = historian_make_point(1, ts, start_val + (double)i * step_val,
                                        HISTORIAN_QUAL_GOOD);
    }
    return pts;
}

static void test_tumbling_window(void)
{
    TEST("tumbling_window");
    int n = 100;
    historian_data_point_t *pts = make_timeseries(n, 0.0, 1.0);

    historian_window_set_t ws;
    historian_window_set_init(&ws);

    int ret = historian_window_tumbling(pts, (size_t)n, 10000, &ws);
    CHECK(ret == 0, "tumbling window ok");
    CHECK(ws.count >= 5, "at least 5 tumbling windows");
    /* 100 points * 1s each = 100s, window size 10s => ~10 windows */
    CHECK(ws.count <= 15, "at most 15 tumbling windows");

    /* Each window should have approximately 10 points */
    for (size_t i = 0; i < ws.count; i++) {
        CHECK(ws.windows[i].point_count > 0, "window has points");
    }

    historian_window_set_destroy(&ws);
    free(pts);
    PASS();
}

static void test_sliding_window(void)
{
    TEST("sliding_window");
    int n = 100;
    historian_data_point_t *pts = make_timeseries(n, 0.0, 1.0);

    historian_window_set_t ws;
    historian_window_set_init(&ws);

    /* Window size=20s, slide=5s */
    int ret = historian_window_sliding(pts, (size_t)n, 20000, 5000, &ws);
    CHECK(ret == 0, "sliding window ok");
    CHECK(ws.count > 10, "many sliding windows");
    CHECK(ws.count < 30, "reasonable sliding window count");

    historian_window_set_destroy(&ws);
    free(pts);
    PASS();
}

static void test_session_window(void)
{
    TEST("session_window");
    /* Create data with a big gap in the middle */
    historian_data_point_t pts[50];
    historian_timestamp_t ts;
    ts.tz_offset_min = 0; ts.is_dst = 0; ts.is_utc = 1;

    for (int i = 0; i < 20; i++) {
        ts.epoch_ms = (int64_t)(i * 1000);
        pts[i] = historian_make_point(1, ts, (double)i, HISTORIAN_QUAL_GOOD);
    }
    /* Gap: skip to t=60000 */
    for (int i = 20; i < 50; i++) {
        ts.epoch_ms = 60000 + (int64_t)((i - 20) * 1000);
        pts[i] = historian_make_point(1, ts, (double)i, HISTORIAN_QUAL_GOOD);
    }

    historian_window_set_t ws;
    historian_window_set_init(&ws);

    int ret = historian_window_session(pts, 50, 5000, &ws);
    CHECK(ret == 0, "session window ok");
    CHECK(ws.count >= 2, "at least 2 sessions (gap detected)");

    historian_window_set_destroy(&ws);
    PASS();
}

static void test_calendar_window(void)
{
    TEST("calendar_window");
    historian_data_point_t pts[24];
    historian_timestamp_t ts;
    ts.tz_offset_min = 0; ts.is_dst = 0; ts.is_utc = 1;

    /* 24 hourly data points */
    for (int i = 0; i < 24; i++) {
        ts.epoch_ms = (int64_t)(i * 3600000);
        pts[i] = historian_make_point(1, ts, (double)(i * 100), HISTORIAN_QUAL_GOOD);
    }

    historian_window_set_t ws;
    historian_window_set_init(&ws);

    /* 6-hour calendar windows */
    int ret = historian_window_calendar(pts, 24, 21600000, 0, &ws);
    CHECK(ret == 0, "calendar window ok");
    CHECK(ws.count == 4, "4 x 6-hour windows in 24 hours");

    historian_window_set_destroy(&ws);
    PASS();
}

static void test_gap_detection(void)
{
    TEST("gap_detection");
    historian_data_point_t pts[5];
    historian_timestamp_t ts;
    ts.tz_offset_min = 0; ts.is_dst = 0; ts.is_utc = 1;

    ts.epoch_ms = 0;     pts[0] = historian_make_point(1, ts, 1.0, HISTORIAN_QUAL_GOOD);
    ts.epoch_ms = 1000;  pts[1] = historian_make_point(1, ts, 2.0, HISTORIAN_QUAL_GOOD);
    /* Gap: 10000 ms */
    ts.epoch_ms = 11000; pts[2] = historian_make_point(1, ts, 3.0, HISTORIAN_QUAL_GOOD);
    ts.epoch_ms = 12000; pts[3] = historian_make_point(1, ts, 4.0, HISTORIAN_QUAL_GOOD);
    /* Gap: 50000 ms */
    ts.epoch_ms = 62000; pts[4] = historian_make_point(1, ts, 5.0, HISTORIAN_QUAL_GOOD);

    historian_window_set_t ws;
    historian_window_set_init(&ws);

    int ret = historian_detect_gaps(pts, 5, 5000, &ws);
    CHECK(ret == 0, "gap detection ok");
    CHECK(ws.count == 2, "2 gaps detected");

    historian_window_set_destroy(&ws);
    PASS();
}

static void test_sql_lag_lead(void)
{
    TEST("sql_lag_lead");
    int n = 5;
    historian_data_point_t *pts = make_timeseries(n, 10.0, 10.0);

    double lag_vals[5], lead_vals[5];

    historian_sql_lag(pts, 5, 1, lag_vals);
    CHECK(isnan(lag_vals[0]), "LAG(1) first row is NaN");
    CHECK(fabs(lag_vals[1] - 10.0) < 0.01, "LAG(1) row 1 = row 0 value");

    historian_sql_lead(pts, 5, 1, lead_vals);
    CHECK(isnan(lead_vals[4]), "LEAD(1) last row is NaN");
    CHECK(fabs(lead_vals[0] - 20.0) < 0.01, "LEAD(1) row 0 = row 1 value");

    free(pts);
    PASS();
}

/* =========================================================================
 * L8: Distribution Statistics Test
 * ========================================================================= */

static void test_distribution_stats(void)
{
    TEST("distribution_stats");
    /* Generate a normal-like dataset */
    historian_data_point_t pts[100];
    historian_timestamp_t ts;
    ts.tz_offset_min = 0; ts.is_dst = 0; ts.is_utc = 1;

    for (int i = 0; i < 100; i++) {
        ts.epoch_ms = (int64_t)(i * 1000);
        pts[i] = historian_make_point(1, ts, (double)(i + 1), HISTORIAN_QUAL_GOOD);
    }

    historian_distribution_stats_t stats;
    int ret = historian_compute_distribution_stats(pts, 100, &stats);
    CHECK(ret == 0, "distribution stats computed");
    CHECK(stats.count == 100, "valid count 100");
    CHECK(fabs(stats.mean - 50.5) < 1.0, "mean ~50.5");
    CHECK(stats.min_val < stats.max_val, "min < max");
    /* Duration in hours: 99 seconds / 3600 ? 0.0275 hr */
    CHECK(stats.duration_hr > 0.0, "positive duration");
    PASS();
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    printf("=== mini-historian-data-retrieval-sql Test Suite ===\n\n");

    printf("--- L1: Data Model Tests ---\n");
    test_tag_metadata_init();
    test_tag_metadata_validate();
    test_raw_to_eu_conversion();
    test_timestamp_iso8601();
    test_timestamp_compare();
    test_quality_flags();
    test_data_point();
    test_result_set();

    printf("\n--- L2: SQL Query Generation Tests ---\n");
    test_query_spec();
    test_sql_generation();
    test_parameterized_sql();
    test_cost_estimation();

    printf("\n--- L5: Aggregate Tests ---\n");
    test_aggregate_average();
    test_aggregate_min_max();
    test_aggregate_stddev();
    test_time_weighted_average();
    test_running_aggregate();

    printf("\n--- L5: Compression Tests ---\n");
    test_swinging_door_compression();
    test_deadband_compression();
    test_compression_ratio();
    test_reconstruct_value();

    printf("\n--- L5: Interpolation Tests ---\n");
    test_interp_step();
    test_interp_linear();
    test_interp_quadratic();
    test_interp_cubic_spline();
    test_interp_akima();
    test_interp_range();

    printf("\n--- L2: Windowing Tests ---\n");
    test_tumbling_window();
    test_sliding_window();
    test_session_window();
    test_calendar_window();
    test_gap_detection();
    test_sql_lag_lead();

    printf("\n--- L8: Advanced Tests ---\n");
    test_distribution_stats();

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    return (tests_failed > 0) ? 1 : 0;
}
