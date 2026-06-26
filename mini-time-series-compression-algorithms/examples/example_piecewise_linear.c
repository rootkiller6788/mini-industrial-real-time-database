/**
 * @file example_piecewise_linear.c
 * @brief End-to-End Piecewise Linear Approximation (PLA) Example
 *
 * Compares four PLA algorithms (Sliding Window, Top-Down, Bottom-Up,
 * SWAB) on a simulated industrial signal with trend, oscillation,
 * and noise.
 *
 * L6 Canonical Problem: Piecewise linear segmentation of process
 * data for trend-based compression.
 */

#include <stdio.h>
#include <stdlib.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include "ts_piecewise_linear.h"
#include "ts_deadband.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NUM_POINTS 200

int main(void)
{
    printf("=== Piecewise Linear Approximation Example ===\n\n");

    /* Generate test signal */
    ts_data_point_t signal[NUM_POINTS];
    srand(77);

    for (int i = 0; i < NUM_POINTS; i++) {
        signal[i].epoch_us = (int64_t)i * 1000000LL;
        double v = 10.0 + 0.1 * (double)i;                      /* Ramp */
        v += 5.0 * sin(2.0 * M_PI * (double)i / 50.0);          /* Sine */
        if (i > 150 && i < 180) v += 8.0;                       /* Step */
        v += 0.5 * ((double)rand() / RAND_MAX - 0.5);           /* Noise */
        signal[i].value = v;
        signal[i].quality = TS_QUALITY_GOOD;
    }

    /* Run all four algorithms */
    ts_pla_algorithm_t algorithms[] = {
        TS_PLA_SLIDING_WINDOW,
        TS_PLA_TOP_DOWN,
        TS_PLA_BOTTOM_UP,
        TS_PLA_SWAB
    };
    const char *algo_names[] = {
        "Sliding Window", "Top-Down", "Bottom-Up", "SWAB"
    };

    printf("  %-18s  %6s  %6s  %10s\n",
           "Algorithm", "Segs", "Ratio", "RMSE");
    printf("  %-18s  %6s  %6s  %10s\n",
           "------------------", "------", "------", "----------");

    for (int a = 0; a < 4; a++) {
        ts_pla_model_t model;
        ts_pla_model_init(&model, NUM_POINTS);

        ts_pla_config_t config = {
            .algorithm = algorithms[a],
            .error_tolerance = 1.0,
            .min_segment_points = 2,
            .max_segment_points = 0
        };

        ts_pla_compute(&model, signal, NUM_POINTS, &config);
        ts_pla_compute_error(&model, signal, NUM_POINTS);

        printf("  %-18s  %6zu  %5.1fx  %10.4f\n",
               algo_names[a],
               model.num_segments,
               model.compression_ratio,
               model.rmse);

        ts_pla_model_free(&model);
    }

    /* MDL optimal segment count */
    printf("\n--- MDL Optimal Segment Count ---\n");
    size_t optimal_k = 0;
    double mdl_cost = ts_pla_mdl_optimal_segments(
        signal, NUM_POINTS, 20, 1.5, &optimal_k);
    printf("  Optimal segments (MDL): %zu (cost=%.3f)\n",
           optimal_k, mdl_cost);

    /* PLA compress convenience function */
    printf("\n--- PLA Compression Output ---\n");
    {
        ts_data_point_t *compressed = NULL;
        size_t num_compressed = 0;

        ts_pla_config_t config = {
            .algorithm = TS_PLA_TOP_DOWN,
            .error_tolerance = 1.0,
            .min_segment_points = 2
        };

        ts_pla_compress(signal, NUM_POINTS, &config,
                         &compressed, &num_compressed);

        printf("  Compressed to %zu points (%.1f%% of original)\n",
               num_compressed,
               100.0 * (double)num_compressed / (double)NUM_POINTS);

        printf("  First 6 compressed points:\n");
        for (size_t i = 0; i < 6 && i < num_compressed; i++) {
            printf("    [%zu] t=%-10lld v=%-8.3f\n",
                   i, (long long)compressed[i].epoch_us,
                   compressed[i].value);
        }

        free(compressed);
    }

    printf("\n=== Example Complete ===\n");
    return 0;
}
