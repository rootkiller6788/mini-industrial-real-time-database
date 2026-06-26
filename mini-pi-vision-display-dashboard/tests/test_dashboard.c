#include "pv_dashboard.h"
#include "pv_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static int tr=0,tp=0;
#define T(n) do{tr++;printf("  TEST %s... ",n);}while(0)
#define P() do{printf("PASS\n");tp++;}while(0)

int main(void) {
    printf("=== Dashboard Tests ===\n");

    T("dashboard_create");
    pv_dashboard_t *db = pv_dashboard_create("MainDashboard", 4, 3);
    assert(db != NULL); assert(!strcmp(db->name, "MainDashboard"));
    assert(db->layout.columns == 4); assert(db->layout.rows == 3);
    pv_dashboard_destroy(db);
    assert(pv_dashboard_create(NULL, 2, 2) == NULL);
    P();

    T("dashboard_add_get_remove");
    db = pv_dashboard_create("DB", 2, 2);
    pv_display_t *d1 = pv_display_create("D1", NULL);
    pv_display_t *d2 = pv_display_create("D2", NULL);
    assert(pv_dashboard_add_display(db, d1) == 1);
    assert(db->display_count == 1);
    assert(pv_dashboard_add_display(db, d2) == 1);
    uint32_t did = d1->display_id;
    assert(pv_dashboard_get_display(db, did) == d1);
    assert(pv_dashboard_remove_display(db, did) == 1);
    assert(db->display_count == 1);
    pv_dashboard_destroy(db);
    P();

    T("nav_validate_hierarchy");
    pv_nav_link_t links[3];
    links[0].level = PV_NAV_LEVEL_2;
    links[1].level = PV_NAV_LEVEL_3;
    links[2].level = PV_NAV_LEVEL_2;
    /* From level 2: links to 2, 3, 2 are all valid (diff -1, 0, or 1) */
    assert(pv_nav_validate_hierarchy(links, 3, PV_NAV_LEVEL_2) == 1);
    links[0].level = PV_NAV_LEVEL_4;
    /* From level 1: jump to level 4 is invalid */
    assert(pv_nav_validate_hierarchy(links, 1, PV_NAV_LEVEL_1) == 0);
    P();

    T("nav_count_by_level");
    pv_nav_link_t l2[5];
    l2[0].level=PV_NAV_LEVEL_1; l2[1].level=PV_NAV_LEVEL_2;
    l2[2].level=PV_NAV_LEVEL_2; l2[3].level=PV_NAV_LEVEL_3;
    l2[4].level=PV_NAV_LEVEL_4;
    int counts[4];
    pv_nav_count_by_level(l2, 5, counts);
    assert(counts[0]==1); assert(counts[1]==2);
    assert(counts[2]==1); assert(counts[3]==1);
    P();

    T("grid_optimal_columns");
    int cols = pv_grid_optimal_columns(1920, 200, 4);
    assert(cols >= 4);
    assert(pv_grid_optimal_columns(800, 200, 4) == 4);
    P();

    T("dashboard_set_active");
    db = pv_dashboard_create("ADB", 2, 2);
    d1 = pv_display_create("D1", NULL);
    d2 = pv_display_create("D2", NULL);
    pv_dashboard_add_display(db, d1);
    pv_dashboard_add_display(db, d2);
    pv_dashboard_set_active_display(db, d2->display_id);
    assert(db->active_display == d2);
    pv_dashboard_destroy(db);
    P();

    printf("=== Results: %d/%d tests passed ===\n",tp,tr);
    return (tp==tr)?0:1;
}
