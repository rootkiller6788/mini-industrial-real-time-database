/**
 * @file examples/example_kpi_dashboard.c
 * @brief KPI Dashboard — Manufacturing KPI Calculation and Rollup
 *
 * Demonstrates:
 *   - KPI registration for OEE, Availability, Performance, Quality
 *   - Threshold-based traffic-light evaluation
 *   - Hierarchical rollup (line-level → plant-level OEE)
 *   - Composite score calculation
 *
 * ISO 22400-2:2014 — OEE = Availability × Performance × Quality
 * 
 * ISA-95 Level 3/4 — Manufacturing Operations KPIs
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "pi_af_analytics_kpi.h"

int main(void) {
    printf("=== PI AF Analytics — KPI Dashboard Example ===\n\n");

    pi_af_kpi_context_t kctx;
    pi_af_kpi_init(&kctx);

    /* ---- Define KPIs for Production Line 1 ---- */
    pi_af_kpi_t avail_l1, perf_l1, qual_l1, oee_l1;
    memset(&avail_l1, 0, sizeof(avail_l1));
    memset(&perf_l1,  0, sizeof(perf_l1));
    memset(&qual_l1,  0, sizeof(qual_l1));
    memset(&oee_l1,   0, sizeof(oee_l1));

    /* Availability: target = 90%, higher is better */
    strcpy(avail_l1.name, "Line1 Availability");
    strcpy(avail_l1.uom, "%");
    avail_l1.direction = PI_AF_KPI_DIR_HIGHER_BETTER;
    avail_l1.target = 90.0;
    avail_l1.thresholds[0].value = 80.0;
    avail_l1.thresholds[0].status = PI_AF_KPI_STATUS_WARNING;
    avail_l1.thresholds[1].value = 60.0;
    avail_l1.thresholds[1].status = PI_AF_KPI_STATUS_CRITICAL;
    avail_l1.threshold_count = 2;

    /* Performance: target = 95%, higher is better */
    strcpy(perf_l1.name, "Line1 Performance");
    strcpy(perf_l1.uom, "%");
    perf_l1.direction = PI_AF_KPI_DIR_HIGHER_BETTER;
    perf_l1.target = 95.0;
    perf_l1.thresholds[0].value = 85.0;
    perf_l1.thresholds[0].status = PI_AF_KPI_STATUS_WARNING;
    perf_l1.thresholds[1].value = 70.0;
    perf_l1.thresholds[1].status = PI_AF_KPI_STATUS_CRITICAL;
    perf_l1.threshold_count = 2;

    /* Quality: target = 99%, higher is better */
    strcpy(qual_l1.name, "Line1 Quality");
    strcpy(qual_l1.uom, "%");
    qual_l1.direction = PI_AF_KPI_DIR_HIGHER_BETTER;
    qual_l1.target = 99.0;
    qual_l1.thresholds[0].value = 97.0;
    qual_l1.thresholds[0].status = PI_AF_KPI_STATUS_WARNING;
    qual_l1.threshold_count = 1;

    uint32_t id_avail, id_perf, id_qual;
    pi_af_kpi_register(&kctx, &avail_l1, &id_avail);
    pi_af_kpi_register(&kctx, &perf_l1,  &id_perf);
    pi_af_kpi_register(&kctx, &qual_l1,  &id_qual);

    printf("Registered KPIs:\n");
    printf("  [%u] %s\n", id_avail, avail_l1.name);
    printf("  [%u] %s\n", id_perf,  perf_l1.name);
    printf("  [%u] %s\n", id_qual,  qual_l1.name);

    /* ---- Simulate KPI values ---- */
    double actual_avail = 87.5;  /* 87.5% → WARNING (below 90% target) */
    double actual_perf  = 96.2;  /* 96.2% → GOOD (above 95% target) */
    double actual_qual  = 98.7;  /* 98.7% → WARNING (below 99% target) */

    pi_af_kpi_update(&kctx, id_avail, actual_avail, time(NULL));
    pi_af_kpi_update(&kctx, id_perf,  actual_perf,  time(NULL));
    pi_af_kpi_update(&kctx, id_qual,  actual_qual,  time(NULL));

    pi_af_kpi_t *kpi;

    printf("\nKPI Status Dashboard:\n");
    printf("  %-25s %8s  %8s  %s\n", "KPI", "Value", "Score", "Status");
    printf("  %-25s %8s  %8s  %s\n", "---", "-----", "-----", "------");

    kpi = pi_af_kpi_get(&kctx, id_avail);
    printf("  %-25s %7.1f%%  %6.1f  %s\n",
           kpi->name, kpi->current_value, kpi->perf_score,
           pi_af_kpi_status_name(kpi->current_status));

    kpi = pi_af_kpi_get(&kctx, id_perf);
    printf("  %-25s %7.1f%%  %6.1f  %s\n",
           kpi->name, kpi->current_value, kpi->perf_score,
           pi_af_kpi_status_name(kpi->current_status));

    kpi = pi_af_kpi_get(&kctx, id_qual);
    printf("  %-25s %7.1f%%  %6.1f  %s\n",
           kpi->name, kpi->current_value, kpi->perf_score,
           pi_af_kpi_status_name(kpi->current_status));

    /* ---- Composite Score ---- */
    double scores[] = {
        pi_af_kpi_get(&kctx, id_avail)->perf_score,
        pi_af_kpi_get(&kctx, id_perf)->perf_score,
        pi_af_kpi_get(&kctx, id_qual)->perf_score,
    };
    double weights[] = {1.0, 1.0, 1.0};
    double composite;
    pi_af_kpi_composite_score(scores, weights, 3, &composite);

    printf("\n  Composite Score: %.1f / 100\n", composite);

    printf("\n=== KPI Dashboard Complete ===\n");
    return 0;
}
