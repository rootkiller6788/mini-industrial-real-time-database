/**
 * @file examples/example_rolling_analytics.c
 * @brief Rolling Analytics — Sliding Window Statistics for Pump Efficiency
 *
 * Demonstrates:
 *   - Sliding window buffer for real-time sensor data
 *   - Rolling average, min, max, stddev over a window
 *   - Exponential Moving Average for trend smoothing
 *   - Rate of change detection via linear regression
 *   - CUSUM for detecting efficiency degradation
 *   - Percent good calculation for data quality monitoring
 *
 * Use Case:
 *   A centrifugal pump's efficiency is monitored in real time.
 *   Rolling analytics detect when efficiency begins to degrade,
 *   enabling predictive maintenance before failure.
 *
 * @see ISO 14224:2016 — Reliability data for rotating equipment
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "pi_af_analytics_timeseries.h"
#include "pi_af_analytics_core.h"

int main(void) {
    printf("=== PI AF Analytics — Rolling Pump Efficiency Monitor ===\n\n");

    /* Simulate 24 hours of pump efficiency data (one sample per hour).
     * Efficiency starts at 85%, gradually degrades to 78% at hour 18,
     * then drops sharply to 65% after a seal failure at hour 20. */
    double efficiency_data[] = {
        85.2, 84.9, 85.1, 84.7, 85.0, 84.5, 84.8, 84.2,
        84.0, 83.7, 83.5, 83.8, 83.2, 83.0, 82.7, 82.5,
        82.1, 81.8, 78.5, 77.2, 65.0, 64.8, 64.5, 64.2
    };
    uint32_t n = 24;

    /* Set up a sliding window of 6 hours for computing rolling stats */
    pi_af_sliding_window_t win;
    pi_af_sliding_window_init(&win, 6);

    /* Set up EMA with alpha = 0.3 (equivalent to roughly 6-period window) */
    pi_af_ema_state_t ema;
    pi_af_ema_init(&ema, 0.3);

    /* Feed data into the window and compute stats */
    printf("Hour | Value | RollAvg |  EMA   | Status\n");
    printf("-----+-------+---------+--------+--------\n");

    pi_af_datapoint_t history[24];
    for (uint32_t i = 0; i < n; i++) {
        pi_af_datapoint_t pt;
        pt.timestamp = (time_t)(3600 * (i + 1));
        pt.value = efficiency_data[i];
        pt.quality = PI_AF_QUALITY_GOOD;
        pt.is_interpolated = false;

        pi_af_sliding_window_push(&win, &pt);
        history[i] = pt;

        /* Rolling average over the 6-hour window */
        double roll_avg = 0.0;
        if (pi_af_sliding_window_count(&win) > 0) {
            pi_af_window_aggregate(&win, PI_AF_AGG_AVERAGE, &roll_avg);
        }

        /* EMA */
        double ema_val = 0.0;
        pi_af_ema_update(&ema, pt.value, pt.timestamp, &ema_val);

        /* Status check */
        const char *status = "Normal";
        if (ema_val < 70.0) {
            status = "CRITICAL — Pump failure imminent!";
        } else if (ema_val < 80.0) {
            status = "WARNING — Efficiency degrading";
        }

        printf(" %3d  | %5.1f |  %5.2f  | %5.2f  | %s\n",
               i + 1, pt.value, roll_avg, ema_val, status);
    }

    /* ---- Rate of Change over last 8 hours ---- */
    printf("\n--- Rate of Change Analysis (last 8 hours) ---\n");

    pi_af_time_window_t roc_win;
    memset(&roc_win, 0, sizeof(roc_win));
    roc_win.type = PI_AF_WIN_TYPE_RELATIVE;
    roc_win.lookback_sec = 8.0 * 3600.0;

    double roc;
    pi_af_rate_of_change(history, n, &roc_win, &roc);
    printf("  Efficiency change rate: %.4f %% / hour\n", roc * 3600.0);
    printf("  (Negative = degrading, Positive = improving)\n");

    /* ---- CUSUM for detecting the efficiency drop ---- */
    printf("\n--- CUSUM Change Point Detection ---\n");

    double cusum_val;
    bool alarmed;
    pi_af_cusum_detect(history, n, 84.0, 1.5, 10.0, &cusum_val, &alarmed);
    printf("  CUSUM statistic: %.2f\n", cusum_val);
    printf("  Alarm: %s\n", alarmed ? "YES — Significant shift detected!" : "No shift detected");

    /* ---- Percent Good ---- */
    printf("\n--- Data Quality ---\n");
    double pg = pi_af_percent_good(history, n);
    printf("  Data completeness: %.1f%% (%u of %d points good)\n",
           pg, (uint32_t)(pg * n / 100.0), n);

    pi_af_sliding_window_free(&win);

    printf("\n=== Rolling Analytics Complete ===\n");
    return 0;
}
