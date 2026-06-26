/**
 * @file example_deadband.c
 * @brief End-to-End Deadband Compression Example
 *
 * Demonstrates absolute, percent, and combined deadband filtering
 * on a simulated industrial temperature signal with noise.
 *
 * L6 Canonical Problem: Compressing a noisy temperature sensor
 * signal while preserving meaningful changes.
 */

#include <stdio.h>
#include <stdlib.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <time.h>
#include "ts_deadband.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NUM_POINTS 1000

int main(void)
{
    printf("=== Deadband Compression Example ===\n\n");

    /* Generate simulated temperature signal: 25C base + slow sine + noise */
    ts_data_point_t signal[NUM_POINTS];
    srand(42);

    for (int i = 0; i < NUM_POINTS; i++) {
        signal[i].epoch_us = (int64_t)i * 1000000LL;
        signal[i].value = 25.0
                        + 2.0 * sin(2.0 * M_PI * (double)i / 200.0)
                        + 0.15 * ((double)rand() / RAND_MAX - 0.5);
        signal[i].quality = TS_QUALITY_GOOD;
    }

    /* Test 1: Absolute deadband */
    printf("--- Absolute Deadband (epsilon=0.5 C) ---\n");
    {
        ts_deadband_state_t state;
        ts_deadband_config_t config = {
            .mode = TS_DEADBAND_ABSOLUTE,
            .threshold_abs = 0.5
        };
        ts_deadband_init(&state, &config);

        ts_data_point_t archived[NUM_POINTS];
        size_t num_archived = 0;
        ts_deadband_filter_batch(&state, signal, NUM_POINTS,
                                  archived, &num_archived);

        ts_compression_stats_t stats;
        ts_deadband_get_stats(&state, &stats);
        printf("  Points received:  %llu\n",
               (unsigned long long)stats.points_received);
        printf("  Points archived:  %llu\n",
               (unsigned long long)stats.points_archived);
        printf("  Compression ratio: %.2f:1\n", stats.compression_ratio);
        printf("  Bytes saved:      %.1f%%\n", stats.bytes_saved_pct);

        double rmse, max_err;
        ts_deadband_reconstruction_error(signal, NUM_POINTS,
                                          archived, num_archived,
                                          &rmse, &max_err);
        printf("  Reconstruction RMSE: %.4f\n", rmse);
        printf("  Max error:           %.4f\n\n", max_err);
    }

    /* Test 2: Compare different epsilon values */
    printf("--- Epsilon Sweep ---\n");
    printf("  Epsilon  |  Arch %%  |  Ratio  |  RMSE\n");
    printf("  ---------|----------|---------|--------\n");

    double epsilons[] = {0.1, 0.2, 0.5, 1.0, 2.0};
    for (int e = 0; e < 5; e++) {
        ts_deadband_state_t state;
        ts_deadband_config_t config = {
            .mode = TS_DEADBAND_ABSOLUTE,
            .threshold_abs = epsilons[e]
        };
        ts_deadband_init(&state, &config);

        ts_data_point_t archived[NUM_POINTS];
        size_t num_archived = 0;
        ts_deadband_filter_batch(&state, signal, NUM_POINTS,
                                  archived, &num_archived);

        ts_compression_stats_t stats;
        ts_deadband_get_stats(&state, &stats);

        double rmse, max_err;
        ts_deadband_reconstruction_error(signal, NUM_POINTS,
                                          archived, num_archived,
                                          &rmse, &max_err);

        printf("  %7.1f  |  %5.1f%%  |  %5.2f  |  %.4f\n",
               epsilons[e],
               100.0 - stats.points_archived * 100.0 / stats.points_received,
               stats.compression_ratio,
               rmse);
    }

    /* Test 3: Theoretical estimate */
    printf("\n--- Theoretical Compression Estimate ---\n");
    double sigma = 0.15;  /* Noise standard deviation */
    for (int e = 0; e < 5; e++) {
        double estimated = ts_deadband_estimate_compression(epsilons[e], sigma);
        printf("  epsilon=%.1f, sigma=%.2f: estimated ratio = %.2f:1\n",
               epsilons[e], sigma, estimated);
    }

    printf("\n=== Example Complete ===\n");
    return 0;
}
