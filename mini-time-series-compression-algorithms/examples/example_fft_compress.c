/**
 * @file example_fft_compress.c
 * @brief End-to-End Transform-Based Compression (DFT/DCT) Example
 *
 * Demonstrates frequency-domain compression on synthetic signals
 * with known spectral content. Compares DFT vs DCT compression
 * at various coefficient retention ratios.
 *
 * L6 Canonical Problem: Compress a multi-frequency vibration
 * signal using spectral sparsity.
 * L8 Advanced Topic: Frequency-domain thresholding.
 */

#include <stdio.h>
#include <stdlib.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include "ts_transform.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BLOCK_SIZE 64

int main(void)
{
    printf("=== Transform-Based Compression Example ===\n\n");

    /* Generate test signal: 3 sinusoids + noise */
    double signal[BLOCK_SIZE];
    for (int i = 0; i < BLOCK_SIZE; i++) {
        signal[i] =  1.0 * sin(2.0 * M_PI * (double)i * 2.0 / BLOCK_SIZE)
                   + 0.5 * sin(2.0 * M_PI * (double)i * 5.0 / BLOCK_SIZE)
                   + 0.3 * sin(2.0 * M_PI * (double)i * 13.0 / BLOCK_SIZE)
                   + 0.05 * ((double)rand() / RAND_MAX - 0.5);
    }

    printf("  Signal: 3 sinusoids + noise, block size = %d\n", BLOCK_SIZE);

    /* DFT Compression at various keep ratios */
    printf("\n--- DFT Compression (Cooley-Tukey FFT) ---\n");
    printf("  %-8s  %8s  %8s  %10s  %8s\n",
           "Keep%%", "Coeffs", "Ratio", "RMSE", "PSNR(dB)");
    printf("  %-8s  %8s  %8s  %10s  %8s\n",
           "--------", "--------", "--------", "----------", "--------");

    double keep_ratios[] = {0.05, 0.10, 0.20, 0.50, 1.00};

    for (int k = 0; k < 5; k++) {
        ts_transform_config_t config = {
            .transform_type = TS_TRANSFORM_DFT,
            .window_type = TS_WINDOW_HANN,
            .block_size = BLOCK_SIZE,
            .keep_ratio = keep_ratios[k],
            .quantize_bits = 0
        };

        ts_transform_stats_t stats;
        ts_transform_analyze(signal, BLOCK_SIZE, &config, &stats);

        double ratio = ts_transform_compression_ratio(BLOCK_SIZE, &stats);

        printf("  %7.1f%%  %8zu  %7.2fx  %10.6f  %7.2f\n",
               keep_ratios[k] * 100.0,
               stats.retained_coeffs,
               ratio,
               stats.rmse,
               stats.psnr_db);
    }

    /* DCT Compression */
    printf("\n--- DCT-II Compression ---\n");
    printf("  %-8s  %8s  %8s  %10s  %8s\n",
           "Keep%%", "Coeffs", "Ratio", "RMSE", "PSNR(dB)");
    printf("  %-8s  %8s  %8s  %10s  %8s\n",
           "--------", "--------", "--------", "----------", "--------");

    for (int k = 0; k < 5; k++) {
        ts_transform_config_t config = {
            .transform_type = TS_TRANSFORM_DCT,
            .window_type = TS_WINDOW_RECTANGULAR,
            .block_size = BLOCK_SIZE,
            .keep_ratio = keep_ratios[k],
            .quantize_bits = 0
        };

        ts_transform_stats_t stats;
        ts_transform_analyze(signal, BLOCK_SIZE, &config, &stats);

        double ratio = ts_transform_compression_ratio(BLOCK_SIZE, &stats);

        printf("  %7.1f%%  %8zu  %7.2fx  %10.6f  %7.2f\n",
               keep_ratios[k] * 100.0,
               stats.retained_coeffs,
               ratio,
               stats.rmse,
               stats.psnr_db);
    }

    /* Window comparison */
    printf("\n--- Window Function Comparison (DFT, 10%% keep) ---\n");
    ts_window_type_t windows[] = {
        TS_WINDOW_RECTANGULAR, TS_WINDOW_HANN,
        TS_WINDOW_HAMMING, TS_WINDOW_BLACKMAN
    };
    const char *win_names[] = {
        "Rectangular", "Hann", "Hamming", "Blackman"
    };

    for (int w = 0; w < 4; w++) {
        ts_transform_config_t config = {
            .transform_type = TS_TRANSFORM_DFT,
            .window_type = windows[w],
            .block_size = BLOCK_SIZE,
            .keep_ratio = 0.10,
            .quantize_bits = 0
        };

        ts_transform_stats_t stats;
        ts_transform_analyze(signal, BLOCK_SIZE, &config, &stats);

        printf("  %-12s: RMSE=%.6f  PSNR=%.2f dB\n",
               win_names[w], stats.rmse, stats.psnr_db);
    }

    /* Threshold demonstration */
    printf("\n--- Soft vs Hard Threshold (DCT) ---\n");
    {
        double dct_coeffs[BLOCK_SIZE];
        ts_dct_forward(signal, BLOCK_SIZE, dct_coeffs);

        double T = ts_universal_threshold(dct_coeffs, BLOCK_SIZE);
        printf("  Universal threshold (VisuShrink): T = %.4f\n", T);

        double hard_copy[BLOCK_SIZE];
        double soft_copy[BLOCK_SIZE];
        for (int i = 0; i < BLOCK_SIZE; i++) {
            hard_copy[i] = dct_coeffs[i];
            soft_copy[i] = dct_coeffs[i];
        }

        int hard_kept = ts_hard_threshold(hard_copy, BLOCK_SIZE, T);
        int soft_kept = ts_soft_threshold(soft_copy, BLOCK_SIZE, T);

        printf("  Hard threshold: %d coefficients kept\n", hard_kept);
        printf("  Soft threshold: %d coefficients kept\n", soft_kept);
    }

    printf("\n=== Example Complete ===\n");
    return 0;
}
