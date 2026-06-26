/**
 * example_compression.c - Swinging door compression demonstration.
 *
 * Demonstrates Bristol's swinging door compression algorithm:
 *   - Feed a noisy sine wave through the compressor
 *   - Compare raw vs. compressed data point counts
 *   - Reconstruct values from compressed archive
 *
 * Knowledge: L3 Swinging door (Bristol 1990), L5 Deadband,
 *             L7 OSIsoft PI compression tuning.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/historian_model.h"
#include "../include/historian_compression.h"

int main(void)
{
    printf("=== Example: Swinging Door Compression ===

");

    /* 1. Generate a noisy sine wave signal (1000 points) */
    int raw_count = 1000;
    historian_data_point_t *raw_points = (historian_data_point_t *)malloc(
        (size_t)raw_count * sizeof(historian_data_point_t));
    if (!raw_points) { fprintf(stderr, "malloc failed
"); return 1; }

    historian_timestamp_t ts;
    ts.tz_offset_min = 0;
    ts.is_dst = 0;
    ts.is_utc = 1;

    for (int i = 0; i < raw_count; i++) {
        ts.epoch_ms = (int64_t)(i * 1000); /* 1-second samples */
        double t_sec = (double)i;
        double signal = 50.0 + 10.0 * sin(t_sec * 0.1) + 5.0 * sin(t_sec * 0.03);
        double noise = 0.5 * ((double)rand() / (double)RAND_MAX - 0.5);
        raw_points[i] = historian_make_point(1, ts, signal + noise,
                                               HISTORIAN_QUAL_GOOD);
    }

    printf("Raw signal: %d points over %.0f seconds
",
           raw_count, (double)(raw_count - 1));

    /* 2. Apply swinging door compression */
    historian_compression_params_t params;
    historian_compression_params_init(&params);
    params.method = HISTORIAN_COMPRESSION_SWINGING_DOOR;
    params.comp_deviation = 0.5;   /* +/- 0.5 unit deviation allowed */
    params.comp_min_time_ms = 0;   /* No minimum time */
    params.comp_max_time_ms = 3600000; /* Force store every hour */

    /* Allocate space for compressed output (worst case: same as input) */
    historian_data_point_t *compressed = (historian_data_point_t *)malloc(
        (size_t)raw_count * sizeof(historian_data_point_t));
    if (!compressed) { free(raw_points); return 1; }

    /* Initialize the swinging door state with the first point */
    historian_swinging_door_state_t state;
    historian_swinging_door_init(&state, params.comp_deviation,
                                   raw_points[0].value,
                                   raw_points[0].timestamp.epoch_ms);
    state.comp_max_time_ms = (int64_t)params.comp_max_time_ms;

    /* Feed all raw points through the compressor */
    size_t compressed_count = 0;
    /* Store first point */
    compressed[compressed_count++] = raw_points[0];

    for (int i = 1; i < raw_count; i++) {
        historian_data_point_t stored[1];
        size_t stored_cnt = 0;
        historian_swinging_door_feed(&state,
                                       raw_points[i].value,
                                       raw_points[i].timestamp.epoch_ms,
                                       0,
                                       stored, 1, &stored_cnt);
        for (size_t j = 0; j < stored_cnt && compressed_count < (size_t)raw_count; j++) {
            compressed[compressed_count++] = stored[j];
        }
    }

    printf("Compressed:   %zu points
", compressed_count);

    /* 3. Compute compression ratio */
    double ratio = historian_compression_ratio((size_t)raw_count, compressed_count);
    printf("Compression ratio: %.1f : 1
", ratio);

    /* Estimate expected compression */
    double estimated = historian_estimate_compression(10.0, params.comp_deviation);
    printf("Estimated ratio:   %.1f : 1 (Bristol heuristic)
", estimated);

    /* 4. Deadband comparison */
    historian_data_point_t *deadband_pts = (historian_data_point_t *)malloc(
        (size_t)raw_count * sizeof(historian_data_point_t));
    memcpy(deadband_pts, raw_points, (size_t)raw_count * sizeof(historian_data_point_t));

    size_t deadband_count;
    historian_deadband_compress(deadband_pts, (size_t)raw_count,
                                 1.0, &deadband_count);
    double db_ratio = historian_compression_ratio((size_t)raw_count, deadband_count);
    printf("Deadband (thresh=1.0): %zu points, ratio %.1f:1
",
           deadband_count, db_ratio);

    /* 5. Demonstrate value reconstruction */
    printf("
Value reconstruction from compressed archive:
");
    for (int i = 0; i < 5; i++) {
        int64_t query_ms = (int64_t)(rand() % (raw_count * 1000));
        double reconstructed, original;

        int ret = historian_reconstruct_value(compressed, compressed_count,
                                                query_ms, 1, &reconstructed);
        /* Find original value at or near query_ms */
        int best_idx = 0;
        int64_t best_diff = INT64_MAX;
        for (int j = 0; j < raw_count; j++) {
            int64_t diff = raw_points[j].timestamp.epoch_ms - query_ms;
            if (diff < 0) diff = -diff;
            if (diff < best_diff) { best_diff = diff; best_idx = j; }
        }
        original = raw_points[best_idx].value;

        if (ret == 0) {
            printf("  t=%lldms: reconstructed=%.3f  original=%.3f  error=%.3f
",
                   (long long)query_ms, reconstructed, original,
                   fabs(reconstructed - original));
        }
    }

    /* Cleanup */
    free(raw_points);
    free(compressed);
    free(deadband_pts);

    printf("
=== Example Complete ===
");
    return 0;
}
