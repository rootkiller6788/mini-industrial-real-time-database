/**
 * example_process_overview.c - L6: Process Overview Dashboard
 *
 * Demonstrates creating a Level 1 ISA-101 process area overview
 * with multiple value indicators, a trend, and an alarm summary.
 *
 * Knowledge: ISA-101 Level 1 display, process overview layout
 */

#include "pv_display.h"
#include "pv_symbol.h"
#include "pv_trend.h"
#include "pv_dashboard.h"
#include "pv_hmi_standard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void) {
    printf("=== Process Overview Dashboard (ISA-101 Level 1) ===\n");

    /* Create dashboard with 4-column grid */
    pv_dashboard_t *dashboard = pv_dashboard_create("ProcessOverview", 4, 3);
    if (!dashboard) { printf("Failed to create dashboard\n"); return 1; }

    /* Create the process overview display */
    pv_display_t *display = pv_display_create("AreaA_Overview",
        "Process Area A - Fermentation Unit Overview");
    if (!display) { printf("Failed to create display\n"); return 1; }

    /* Set time range to last 8 hours (shift operator's view) */
    pv_time_range_t tr = {1, 0, 0, 8 * 3600, "Last 8 Hours"};
    pv_display_set_time_range(display, &tr);

    /* Add KPI value indicators */
    pv_rect_t bounds;
    pv_display_element_t *elem;

    /* Reactor Temperature (top-left quadrant) */
    bounds.origin.x = 5.0; bounds.origin.y = 5.0;
    bounds.width = 20.0; bounds.height = 25.0;
    elem = pv_element_create(PV_SYM_VALUE, bounds, "Reactor Temp");
    pv_data_binding_t bind;
    memset(&bind, 0, sizeof(bind));
    strcpy(bind.server_name, "PI-DA-01");
    strcpy(bind.point_name, "TI-101.PV");
    strcpy(bind.uom, "degC");
    bind.zero = 0.0; bind.span = 200.0;
    pv_element_bind_data(elem, &bind);
    pv_element_update_value(elem, 152.5, 100, time(NULL));
    pv_display_add_element(display, elem);

    /* Reactor Pressure */
    bounds.origin.x = 30.0; bounds.origin.y = 5.0;
    elem = pv_element_create(PV_SYM_VALUE, bounds, "Reactor Press");
    memset(&bind, 0, sizeof(bind));
    strcpy(bind.server_name, "PI-DA-01");
    strcpy(bind.point_name, "PI-101.PV");
    strcpy(bind.uom, "MPa");
    bind.zero = 0.0; bind.span = 5.0;
    pv_element_bind_data(elem, &bind);
    pv_element_update_value(elem, 2.35, 100, time(NULL));
    pv_display_add_element(display, elem);

    /* Product Flow Rate */
    bounds.origin.x = 55.0; bounds.origin.y = 5.0;
    elem = pv_element_create(PV_SYM_VALUE, bounds, "Prod Flow");
    memset(&bind, 0, sizeof(bind));
    strcpy(bind.server_name, "PI-DA-01");
    strcpy(bind.point_name, "FI-201.PV");
    strcpy(bind.uom, "L/min");
    bind.zero = 0.0; bind.span = 500.0;
    pv_element_bind_data(elem, &bind);
    pv_element_update_value(elem, 342.0, 100, time(NULL));
    pv_display_add_element(display, elem);

    /* Level Indicator (with alarm limits) */
    bounds.origin.x = 5.0; bounds.origin.y = 35.0;
    elem = pv_element_create(PV_SYM_GAUGE, bounds, "Tank Level");
    memset(&bind, 0, sizeof(bind));
    strcpy(bind.server_name, "PI-DA-01");
    strcpy(bind.point_name, "LI-301.PV");
    strcpy(bind.uom, "%");
    bind.zero = 0.0; bind.span = 100.0;
    pv_element_bind_data(elem, &bind);
    pv_element_update_value(elem, 72.0, 100, time(NULL));
    pv_display_add_element(display, elem);

    /* Trend display (occupies right 40% of screen) */
    bounds.origin.x = 55.0; bounds.origin.y = 35.0;
    bounds.width = 40.0; bounds.height = 55.0;
    elem = pv_element_create(PV_SYM_TREND, bounds, "Temperature Trend");
    pv_display_add_element(display, elem);

    /* Add display to dashboard */
    pv_dashboard_add_display(dashboard, display);

    /* Navigate to detailed view */
    pv_nav_link_t nav;
    memset(&nav, 0, sizeof(nav));
    nav.level = PV_NAV_LEVEL_2;
    nav.target_display_id = 200;
    strcpy(nav.label, "Fermenter Detail");
    pv_dashboard_add_nav_link(dashboard, &nav);

    /* Audit the display for ISA-101 compliance */
    pv_hmi_audit_t audit;
    pv_hmi_audit_display(display, &audit);
    pv_hmi_audit_print(&audit);

    /* Report */
    int total_elements = pv_display_get_element_count_recursive(display);
    printf("\nDashboard: %s\n", dashboard->name);
    printf("  Displays: %d\n", dashboard->display_count);
    printf("  Elements in overview: %d\n", total_elements);
    printf("  Navigation links: %d\n", dashboard->nav_link_count);
    printf("  State: %d (0=LOADING, 1=ACTIVE)\n", display->state);

    pv_dashboard_destroy(dashboard);
    printf("Done.\n");
    return 0;
}
