/**
 * example_aggregation.c - Time-bucketed aggregation demonstration.
 *
 * Demonstrates computing aggregates over time buckets: hourly averages,
 * daily maxima, time-weighted averages for flow totalization.
 *
 * Knowledge: L2 Aggregated retrieval, L4 ISO 22400-2 KPIs,
 *             L5 Welford online algorithm, L7 PI TimeAvg equivalent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/historian_model.h"
#include "../include/historian_aggregate.h"

int main(void)
{
    printf("=== Example: Time-Bucketed Aggregation ===

");

    /* 1. Generate 24 hours of flow data (1 data point per minute = 1440 points) */
    historian_data_point_t *pts = (historian_data_point_t *)malloc(
        1440 * sizeof(historian_data_point_t));
    if (!pts) { fprintf(stderr, "malloc failed
"); return 1; }

    historian_timestamp_t ts;
    ts.tz_offset_min = 0;
    ts.is_dst = 0;
    ts.is_utc = 1;

    /* Simulate: flow oscillates around 500 kg/h with sin wave + noise */
    for (int i = 0; i < 1440; i++) {
        ts.epoch_ms = (int64_t)(i * 60000); /* 1 minute intervals */
        double hour_frac = (double)i / 60.0;
        double base = 500.0;
        double osc = 50.0 * sin(hour_frac * 0.5);
        double noise = 10.0 * ((double)rand() / (double)RAND_MAX - 0.5);
        pts[i] = historian_make_point(1, ts, base + osc + noise,
                                        HISTORIAN_QUAL_GOOD);
    }

    printf("Generated 1440 data points (24 hours at 1-min intervals)
");

    /* 2. Compute basic statistics */
    historian_distribution_stats_t stats;
    historian_compute_distribution_stats(pts, 1440, &stats);

    printf("
Distribution Statistics:
");
    printf("  Mean:     %.2f
", stats.mean);
    printf("  Median:   %.2f
", stats.median);
    printf("  StdDev:   %.2f
", stats.stddev);
    printf("  Skewness: %.3f
", stats.skewness);
    printf("  Kurtosis: %.3f
", stats.kurtosis);
    printf("  Min/Max:  %.2f / %.2f
", stats.min_val, stats.max_val);
    printf("  P10/P90:  %.2f / %.2f
", stats.p10, stats.p90);
    printf("  Duration: %.2f hours
", stats.duration_hr);

    /* 3. Hourly bucket averages (ISO 22400-2) */
    historian_aggregate_spec_t agg_spec;
    historian_aggregate_spec_init(&agg_spec);
    agg_spec.agg_type = HISTORIAN_AGG_AVERAGE;
    agg_spec.bucket.period = HISTORIAN_BUCKET_HOUR;
    agg_spec.bucket.alignment = HISTORIAN_ALIGN_QUERY_START;

    historian_bucket_result_set_t buckets;
    historian_bucket_result_set_init(&buckets);

    int ret = historian_compute_bucketed_aggregate(&agg_spec, pts, 1440, &buckets);
    if (ret == 0) {
        printf("
Hourly Averages (%zu buckets):
", buckets.count);
        for (size_t i = 0; i < buckets.count && i < 8; i++) {
            char tbuf[64];
            historian_timestamp_to_iso8601(&buckets.buckets[i].bucket_start,
                                            tbuf, sizeof(tbuf));
            printf("  Hour %zu: %s  avg=%.1f  n=%zu  %%good=%.0f
",
                   i, tbuf,
                   buckets.buckets[i].agg_value,
                   buckets.buckets[i].sample_count,
                   buckets.buckets[i].percent_good);
        }
        if (buckets.count > 8) printf("  ... and %zu more buckets
", buckets.count - 8);
    }

    /* 4. Time-weighted average (flow totalization) */
    double twavg;
    historian_compute_time_weighted_avg(pts, 1440, &twavg);
    printf("
Time-Weighted Average: %.2f (arithmetic mean: %.2f)
",
           twavg, stats.mean);
    printf("  (For constant-interval data, TWAvg ~ arithmetic mean)
");

    /* 5. Running aggregate demonstration */
    uint8_t state[256] = {0};
    printf("
Running aggregate (first 10 points):
");
    for (int i = 0; i < 10; i++) {
        historian_running_aggregate_update(HISTORIAN_AGG_AVERAGE, &pts[i], state);
    }
    double running_result;
    historian_running_aggregate_finalize(HISTORIAN_AGG_AVERAGE, state, &running_result);
    printf("  Running average of first 10 points: %.2f
", running_result);

    /* Cleanup */
    historian_bucket_result_set_destroy(&buckets);
    free(pts);

    printf("
=== Example Complete ===
");
    return 0;
}
