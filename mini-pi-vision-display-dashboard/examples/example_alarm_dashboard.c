/**
 * example_alarm_dashboard.c - L6: Alarm Summary Dashboard
 *
 * Demonstrates alarm list configuration, ISA-101 alarm color compliance,
 * and alarm severity management for industrial monitoring.
 */

#include "pv_display.h"
#include "pv_symbol.h"
#include "pv_dashboard.h"
#include "pv_hmi_standard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void) {
    printf("=== Alarm Summary Dashboard (ISA-18.2) ===\n");

    /* Create alarm display */
    pv_display_t *display = pv_display_create("AlarmSummary",
        "Unit 1 Alarm Summary - Last 24 Hours");
    if (!display) { printf("Failed to create display\n"); return 1; }

    /* Configure alarm list symbol */
    pv_alarm_list_symbol_t alarm_cfg;
    memset(&alarm_cfg, 0, sizeof(alarm_cfg));
    alarm_cfg.max_alarms = 20;
    alarm_cfg.show_acked = 1;
    alarm_cfg.show_rtn = 0;
    alarm_cfg.sort_by_time = 1;
    alarm_cfg.filter_severity = 0x1F;  /* Show all severities */

    /* ISA-101 alarm colors */
    alarm_cfg.critical_color.r = 0xFF; alarm_cfg.critical_color.g = 0x00;
    alarm_cfg.critical_color.b = 0x00; alarm_cfg.critical_color.a = 0xFF;
    alarm_cfg.high_color.r = 0xFF; alarm_cfg.high_color.g = 0xA5;
    alarm_cfg.high_color.b = 0x00; alarm_cfg.high_color.a = 0xFF;
    alarm_cfg.medium_color.r = 0xFF; alarm_cfg.medium_color.g = 0xFF;
    alarm_cfg.medium_color.b = 0x00; alarm_cfg.medium_color.a = 0xFF;
    alarm_cfg.low_color.r = 0x00; alarm_cfg.low_color.g = 0x80;
    alarm_cfg.low_color.b = 0xFF; alarm_cfg.low_color.a = 0xFF;
    alarm_cfg.unacked_bg.r = 0xFF; alarm_cfg.unacked_bg.g = 0xF0;
    alarm_cfg.unacked_bg.b = 0xF0; alarm_cfg.unacked_bg.a = 0x40;

    void *alarm_sym = pv_symbol_create_alarm_list(&alarm_cfg);
    if (!alarm_sym) { printf("Failed to create alarm list\n"); return 1; }

    /* Add alarm to display */
    pv_rect_t bounds = {{5.0, 5.0}, 90.0, 85.0};
    pv_display_element_t *elem = pv_element_create(PV_SYM_ALARM_LIST, bounds, "Alarms");
    pv_display_add_element(display, elem);

    /* Add KPI indicators for alarm statistics */
    bounds.origin.x = 5.0; bounds.origin.y = 92.0;
    bounds.width = 30.0; bounds.height = 6.0;
    elem = pv_element_create(PV_SYM_KPI_INDICATOR, bounds, "Active Alarms");
    pv_data_binding_t bind;
    memset(&bind, 0, sizeof(bind));
    strcpy(bind.point_name, "ALARM_ACTIVE_COUNT");
    pv_element_bind_data(elem, &bind);
    pv_element_update_value(elem, 3.0, 100, time(NULL));
    pv_display_add_element(display, elem);

    /* Second KPI */
    bounds.origin.x = 38.0;
    elem = pv_element_create(PV_SYM_KPI_INDICATOR, bounds, "Unacked Alarms");
    strcpy(bind.point_name, "ALARM_UNACKED_COUNT");
    pv_element_bind_data(elem, &bind);
    pv_element_update_value(elem, 1.0, 100, time(NULL));
    pv_display_add_element(display, elem);

    /* Third KPI */
    bounds.origin.x = 71.0;
    elem = pv_element_create(PV_SYM_KPI_INDICATOR, bounds, "Alarm Rate (/hr)");
    strcpy(bind.point_name, "ALARM_RATE");
    pv_element_bind_data(elem, &bind);
    pv_element_update_value(elem, 0.5, 100, time(NULL));
    pv_display_add_element(display, elem);

    /* Add to dashboard */
    pv_dashboard_t *dashboard = pv_dashboard_create("AlarmDashboard", 1, 3);
    pv_dashboard_add_display(dashboard, display);

    /* Validate alarm colors per ISA-101 */
    pv_color_palette_t palette;
    pv_hmi_get_palette(PV_PALETTE_GRAY, &palette);

    printf("\nISA-101 Alarm Color Validation:\n");
    pv_alarm_severity_t severities[] = {
        PV_ALARM_CRITICAL, PV_ALARM_HIGH, PV_ALARM_MEDIUM, PV_ALARM_LOW
    };
    const char *sev_names[] = {"Critical", "High", "Medium", "Low"};
    pv_color_t alarm_colors[] = {
        alarm_cfg.critical_color, alarm_cfg.high_color,
        alarm_cfg.medium_color, alarm_cfg.low_color
    };

    for (int i = 0; i < 4; i++) {
        int valid = pv_hmi_validate_alarm_color(
            &alarm_colors[i], severities[i], &palette);
        printf("  %s: %s\n", sev_names[i], valid ? "VALID" : "INVALID");
    }

    /* HMI audit */
    pv_hmi_audit_t audit_result;
    pv_hmi_audit_display(display, &audit_result);
    printf("\nHMI Compliance Audit:\n");
    pv_hmi_audit_print(&audit_result);

    /* Display state management simulation */
    printf("\nDisplay lifecycle:\n");
    pv_display_set_state(display, PV_DISPLAY_ACTIVE);
    printf("  State after activation: %d (ACTIVE)\n", display->state);

    /* Simulate data staleness check */
    int stale = pv_display_is_stale(display, 3600);
    printf("  Stale after 1h timeout: %s\n", stale ? "YES" : "NO");

    pv_symbol_destroy(PV_SYM_ALARM_LIST, alarm_sym);
    pv_dashboard_destroy(dashboard);
    printf("Done.\n");
    return 0;
}
