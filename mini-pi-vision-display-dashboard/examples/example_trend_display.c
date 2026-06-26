/**
 * example_trend_display.c - L6: Multi-Pen Trend Display
 *
 * Demonstrates trend buffer operations, data decimation,
 * and multi-pen trend configuration for process analysis.
 */

#include "pv_display.h"
#include "pv_symbol.h"
#include "pv_trend.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main(void) {
    printf("=== Multi-Pen Trend Display Example ===\n");

    /* Create trend data buffers for 3 pens */
    pv_trend_buffer_t *pen1 = pv_trend_buffer_create(1000);
    pv_trend_buffer_t *pen2 = pv_trend_buffer_create(1000);
    pv_trend_buffer_t *pen3 = pv_trend_buffer_create(1000);

    if (!pen1 || !pen2 || !pen3) {
        printf("Buffer creation failed\n");
        return 1;
    }

    /* Simulate process data: 8 hours at 30-second intervals */
    time_t now = time(NULL);
    time_t start = now - 8 * 3600;
    int num_points = (8 * 3600) / 30;  /* 960 points */

    printf("Simulating %d data points (8 hours at 30s intervals)...\n", num_points);
    for (int i = 0; i < num_points; i++) {
        time_t t = start + i * 30;
        double phase = (double)i / (double)num_points * 2.0 * M_PI * 4.0; /* 4 cycles */

        /* Pen 1: Sinusoidal temperature (150 +/- 10 degC) */
        pv_trend_point_t pt1 = {t, 150.0 + 10.0 * sin(phase), 100, 1};
        pv_trend_buffer_push(pen1, &pt1);

        /* Pen 2: Gradually increasing pressure with noise */
        double base = 2.0 + (double)i / num_points * 1.5;
        double noise = ((double)(rand() % 100) / 100.0 - 0.5) * 0.1;
        pv_trend_point_t pt2 = {t, base + noise, 100, 1};
        pv_trend_buffer_push(pen2, &pt2);

        /* Pen 3: Flow rate (step changes every ~2 hours) */
        double flow = 300.0;
        if (i > num_points / 4) flow = 350.0;
        if (i > num_points / 2) flow = 250.0;
        if (i > 3 * num_points / 4) flow = 400.0;
        pv_trend_point_t pt3 = {t, flow, 100, 1};
        pv_trend_buffer_push(pen3, &pt3);
    }

    /* Statistics for each pen */
    double min1, max1, avg1, min2, max2, avg2, min3, max3, avg3;
    int cnt1, cnt2, cnt3;
    pv_trend_buffer_get_statistics(pen1, &min1, &max1, &avg1, &cnt1);
    pv_trend_buffer_get_statistics(pen2, &min2, &max2, &avg2, &cnt2);
    pv_trend_buffer_get_statistics(pen3, &min3, &max3, &avg3, &cnt3);

    printf("\nPen 1 (Temperature): min=%.2f max=%.2f avg=%.2f count=%d\n",
           min1, max1, avg1, cnt1);
    printf("Pen 2 (Pressure):    min=%.2f max=%.2f avg=%.2f count=%d\n",
           min2, max2, avg2, cnt2);
    printf("Pen 3 (Flow Rate):   min=%.2f max=%.2f avg=%.2f count=%d\n",
           min3, max3, avg3, cnt3);

    /* Decimate for display (800px wide = max 400 displayable points) */
    int displayable = pv_trend_get_display_points(800);
    printf("\nDisplayable points: %d (800px canvas)\n", displayable);

    pv_trend_point_t *all_points = (pv_trend_point_t*)malloc(
        (size_t)num_points * sizeof(pv_trend_point_t));
    pv_trend_point_t *decimated = (pv_trend_point_t*)malloc(
        (size_t)displayable * sizeof(pv_trend_point_t));

    /* Extract all points from pen 1 */
    for (int i = 0; i < num_points; i++) {
        pv_trend_buffer_get(pen1, i, &all_points[i]);
    }

    /* Apply Douglas-Peucker decimation */
    int kept = pv_trend_decimate_douglas_peucker(all_points, num_points,
        displayable, 0.5, decimated);
    printf("Decimated %d points -> %d points (%.1f%% reduction)\n",
           num_points, kept, 100.0 * (1.0 - (double)kept / num_points));

    /* Demonstrate Wu anti-aliased line rendering */
    pv_trend_line_segment_t segments[500];
    pv_color_t red = {0xFF, 0x00, 0x00, 0xFF};
    int nsegs = pv_trend_render_line_wu(0, 0, 400, 200, red, segments, 500);
    printf("\nWu line rendering: %d anti-aliased segments (0,0)->(400,200)\n", nsegs);

    /* Time-to-pixel mapping */
    time_t t_end = now;
    time_t t_start = now - 8 * 3600;
    int pixel = pv_trend_time_to_pixel_x(now - 4 * 3600, t_start, t_end, 50, 700);
    printf("Midpoint (T+4h) maps to pixel X = %d\n", pixel);

    free(all_points);
    free(decimated);
    pv_trend_buffer_destroy(pen1);
    pv_trend_buffer_destroy(pen2);
    pv_trend_buffer_destroy(pen3);

    printf("Done.\n");
    return 0;
}
