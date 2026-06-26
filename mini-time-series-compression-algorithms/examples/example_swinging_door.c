/**
 * @file example_swinging_door.c
 * @brief End-to-End Swinging Door (OSIsoft PI) Compression Example
 *
 * Demonstrates the Bristol Swinging Door algorithm on simulated
 * industrial process data with step changes and noise.
 *
 * L6 Canonical Problem: Compress a process variable with
 * step changes while preserving trend fidelity.
 * L7 Industrial Application: OSIsoft PI historian compression.
 */

#include <stdio.h>
#include <stdlib.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include "ts_swinging_door.h"
#include "ts_deadband.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NUM_POINTS 500

int main(void)
{
    printf("=== Swinging Door Compression Example ===\n\n");

    /* Generate signal: ramp + step + sinusoidal + noise */
    ts_data_point_t signal[NUM_POINTS];
    srand(123);

    for (int i = 0; i < NUM_POINTS; i++) {
        signal[i].epoch_us = 1000000000000LL + (int64_t)i * 1000000LL;
        double v = 50.0;
        v += 0.02 * (double)i;                    /* Slow ramp */
        if (i >= 200 && i < 250) v += 15.0;       /* Step change */
        v += 3.0 * sin(2.0 * M_PI * i / 100.0);   /* Oscillation */
        v += 0.3 * ((double)rand() / RAND_MAX - 0.5);  /* Noise */
        signal[i].value = v;
        signal[i].quality = TS_QUALITY_GOOD;
    }

    /* Apply swinging door compression */
    ts_swinging_door_state_t state;
    ts_swinging_door_config_t config = {
        .compdev = 1.0,
        .compmax_us = 60000000LL,  /* 60 seconds max */
        .compmin_us = 0
    };
    ts_swinging_door_init(&state, &config);

    ts_data_point_t archived[NUM_POINTS];
    size_t num_archived = 0;

    for (int i = 0; i < NUM_POINTS; i++) {
        bool arch;
        ts_data_point_t out;
        ts_swinging_door_filter(&state, &signal[i], &arch, &out);
        if (arch) archived[num_archived++] = out;
    }

    /* Flush remaining pending points */
    ts_data_point_t flush[2];
    size_t num_flush;
    ts_swinging_door_flush(&state, flush, &num_flush);
    for (size_t i = 0; i < num_flush; i++) {
        archived[num_archived++] = flush[i];
    }

    /* Print statistics */
    ts_compression_stats_t stats;
    ts_swinging_door_get_stats(&state, &stats);

    printf("  Points received:  %llu\n",
           (unsigned long long)stats.points_received);
    printf("  Points archived:  %llu\n",
           (unsigned long long)stats.points_archived);
    printf("  Points discarded: %llu\n",
           (unsigned long long)stats.points_discarded);
    printf("  Compression ratio: %.2f:1\n", stats.compression_ratio);

    /* Compute reconstruction error */
    double rmse, max_err;
    ts_swinging_door_reconstruction_error(signal, NUM_POINTS,
                                           archived, num_archived,
                                           &rmse, &max_err);
    printf("  Reconstruction RMSE: %.4f\n", rmse);
    printf("  Max error:           %.4f\n", max_err);

    /* Show first 10 archived points */
    printf("\n  First 10 archived points:\n");
    printf("  %-6s  %-18s  %-12s\n", "Index", "Timestamp", "Value");
    size_t show = (num_archived < 10) ? num_archived : 10;
    for (size_t i = 0; i < show; i++) {
        printf("  %-6zu  %-18lld  %-12.3f\n",
               i, (long long)archived[i].epoch_us, archived[i].value);
    }

    /* Auto-tune recommendation */
    double auto_compdev = ts_swinging_door_autotune_compdev(
        signal, NUM_POINTS, 2.0);
    printf("\n  Auto-tuned compdev (k=2.0): %.3f\n", auto_compdev);

    printf("\n=== Example Complete ===\n");
    return 0;
}
