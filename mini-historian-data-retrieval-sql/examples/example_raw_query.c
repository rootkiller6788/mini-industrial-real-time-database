/**
 * example_raw_query.c - Raw data retrieval demonstration.
 *
 * Demonstrates querying raw (archived) data points from a historian
 * using SQL query generation and data model management.
 *
 * Knowledge: L2 Raw retrieval, L3 SQL generation, L7 PI OLEDB pattern.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/historian_model.h"
#include "../include/historian_retrieval.h"

int main(void)
{
    printf("=== Example: Raw Data Retrieval ===

");

    /* 1. Define a tag */
    historian_tag_metadata_t tag;
    historian_tag_metadata_init(&tag);
    tag.tag_id = 101;
    strncpy(tag.tag_name, "FIC101.PV", sizeof(tag.tag_name) - 1);
    strncpy(tag.descriptor, "Feed Flow Controller PV", sizeof(tag.descriptor) - 1);
    strncpy(tag.eng_units, "kg/h", sizeof(tag.eng_units) - 1);
    tag.eu_zero = 0.0;
    tag.eu_span = 1000.0;
    tag.inst_zero = 4.0;
    tag.inst_span = 20.0;

    printf("Tag:   %s
", tag.tag_name);
    printf("Descr: %s
", tag.descriptor);
    printf("Units: %s
", tag.eng_units);
    printf("Range: %.1f - %.1f %s

", tag.eu_zero, tag.eu_span, tag.eng_units);

    /* 2. Create some sample data points */
    historian_timestamp_t ts;
    ts.tz_offset_min = 480; /* UTC+8 */
    ts.is_dst = 0;
    ts.is_utc = 1;

    historian_result_set_t rs;
    historian_result_set_init(&rs);

    /* Generate 10 data points at 1-minute intervals */
    for (int i = 0; i < 10; i++) {
        ts.epoch_ms = (int64_t)(3600000 + i * 60000); /* Starting at 1:00 AM */
        double value = 500.0 + 20.0 * sin((double)i * 0.5);
        historian_data_point_t dp = historian_make_point(
            tag.tag_id, ts, value, HISTORIAN_QUAL_GOOD);
        historian_result_set_append(&rs, dp);
    }

    printf("Generated %zu data points:
", rs.count);
    for (size_t i = 0; i < rs.count; i++) {
        char time_buf[64];
        historian_timestamp_to_iso8601(&rs.points[i].timestamp, time_buf, sizeof(time_buf));
        printf("  [%s]  value = %8.2f %s  quality = %s
",
               time_buf, rs.points[i].value, tag.eng_units,
               historian_quality_is_good(rs.points[i].quality) ? "GOOD" : "BAD");
    }

    /* 3. Build a SQL query to retrieve this data */
    historian_query_spec_t spec;
    historian_query_spec_init(&spec);
    spec.tag_id = tag.tag_id;
    spec.time_range.start_time.epoch_ms = 0;
    spec.time_range.end_time.epoch_ms = 7200000; /* 2 hours */
    spec.time_range.start_mode = HISTORIAN_BOUNDARY_INCLUSIVE;
    spec.time_range.end_mode = HISTORIAN_BOUNDARY_INCLUSIVE;
    spec.sql_dialect = HISTORIAN_SQL_STANDARD;

    char sql[HISTORIAN_SQL_MAX_LEN];
    int sql_len = historian_generate_sql(&spec, sql, sizeof(sql));
    if (sql_len > 0) {
        printf("
Generated SQL (%d chars):
%s
", sql_len, sql);
    }

    /* 4. Demonstrate PI OLEDB SQL dialect */
    spec.sql_dialect = HISTORIAN_SQL_PI_OLEDB;
    sql_len = historian_generate_sql(&spec, sql, sizeof(sql));
    if (sql_len > 0) {
        printf("
PI OLEDB SQL (%d chars):
%s
", sql_len, sql);
    }

    /* 5. Instrument value conversion */
    printf("
Instrument scaling:
");
    printf("  4 mA  -> %.1f %s
", historian_raw_to_eu(&tag, 4.0), tag.eng_units);
    printf("  12 mA -> %.1f %s
", historian_raw_to_eu(&tag, 12.0), tag.eng_units);
    printf("  20 mA -> %.1f %s
", historian_raw_to_eu(&tag, 20.0), tag.eng_units);

    historian_result_set_destroy(&rs);
    printf("
=== Example Complete ===
");
    return 0;
}
