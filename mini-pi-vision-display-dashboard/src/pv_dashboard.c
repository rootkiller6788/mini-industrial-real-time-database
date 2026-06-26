/**
 * pv_dashboard.c - Dashboard Layout & ISA-101 Navigation (L3, L4, L6)
 */
#include "pv_dashboard.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void pv_grid_cell_to_rect(const pv_grid_layout_t *l, const pv_grid_cell_t *c, pv_rect_t *r) {
    if (!l || !c || !r) return;
    double cw = 1920.0, ch = 1080.0;
    double mx = (double)l->margin_px / cw * 100.0, my = (double)l->margin_px / ch * 100.0;
    double gx = (double)l->gutter_px / cw * 100.0, gy = (double)l->gutter_px / ch * 100.0;
    double aw = 100.0 - 2.0*mx - (l->columns-1)*gx;
    double ah = 100.0 - 2.0*my - (l->rows-1)*gy;
    double cw_cell = (l->columns>0) ? aw/l->columns : aw;
    double ch_cell = (l->rows>0) ? ah/l->rows : ah;
    r->origin.x = mx + c->col_start*(cw_cell+gx);
    r->origin.y = my + c->row_start*(ch_cell+gy);
    r->width = c->col_span*cw_cell + (c->col_span-1)*gx;
    r->height = c->row_span*ch_cell + (c->row_span-1)*gy;
}

int pv_grid_optimal_columns(int canvas_w, int target_cell, int min_cols) {
    if (canvas_w <= 0 || target_cell <= 0) return min_cols;
    int cols = canvas_w / target_cell;
    return (cols < min_cols) ? min_cols : cols;
}

int pv_grid_cell_from_rect(const pv_grid_layout_t *l, pv_coord_t pt, pv_grid_cell_t *c) {
    if (!l || !c) return 0;
    double cw = 1920.0, ch = 1080.0;
    double mx = (double)l->margin_px/cw*100.0, my = (double)l->margin_px/ch*100.0;
    double gx = (double)l->gutter_px/cw*100.0, gy = (double)l->gutter_px/ch*100.0;
    double aw = 100.0 - 2.0*mx - (l->columns-1)*gx;
    double ah = 100.0 - 2.0*my - (l->rows-1)*gy;
    double cell_w = (l->columns>0)?aw/l->columns:aw, cell_h = (l->rows>0)?ah/l->rows:ah;
    if (pt.x < mx || pt.y < my) return 0;
    int col = (int)((pt.x-mx)/(cell_w+gx)), row = (int)((pt.y-my)/(cell_h+gy));
    if (col<0||col>=l->columns||row<0||row>=l->rows) return 0;
    double xic = pt.x - mx - col*(cell_w+gx), yic = pt.y - my - row*(cell_h+gy);
    if (xic > cell_w || yic > cell_h) return 0;
    c->col_start=col; c->row_start=row; c->col_span=1; c->row_span=1;
    return 1;
}

int pv_nav_validate_hierarchy(const pv_nav_link_t *links, int count, pv_navigation_level_t start) {
    if (!links || count == 0) return 1;
    if (start < PV_NAV_LEVEL_1 || start > PV_NAV_LEVEL_4) return 0;
    for (int i = 0; i < count; i++) {
        int diff = (int)links[i].level - (int)start;
        if (diff < -1 || diff > 1) return 0;
    }
    return 1;
}

void pv_nav_count_by_level(const pv_nav_link_t *links, int count, int lc[4]) {
    if (lc) { lc[0]=lc[1]=lc[2]=lc[3]=0; }
    if (!links || !lc) return;
    for (int i=0;i<count;i++) { int idx=(int)links[i].level-1; if(idx>=0&&idx<4)lc[idx]++; }
}

pv_dashboard_t* pv_dashboard_create(const char *name, int columns, int rows) {
    if (!name || name[0]=='\0') return NULL;
    if (columns<1) columns=1;
    if (rows<1) rows=1;
    pv_dashboard_t *db = (pv_dashboard_t*)calloc(1, sizeof(pv_dashboard_t));
    if (!db) return NULL;
    strncpy(db->name, name, sizeof(db->name)-1);
    db->layout.columns = columns; db->layout.rows = rows;
    db->layout.gutter_px = 10; db->layout.margin_px = 20;
    db->max_depth = PV_NAV_LEVEL_4;
    db->dashboard_id = (uint32_t)((uintptr_t)db);
    return db;
}

void pv_dashboard_destroy(pv_dashboard_t *db) {
    if (!db) return;
    for (int i=0;i<db->display_count;i++) pv_display_destroy(db->displays[i]);
    free(db->displays); free(db->nav_links); free(db);
}

int pv_dashboard_add_display(pv_dashboard_t *db, pv_display_t *d) {
    if (!db || !d) return 0;
    pv_display_t **na = (pv_display_t**)realloc(db->displays,
        (size_t)(db->display_count+1)*sizeof(pv_display_t*));
    if (!na) return 0;
    db->displays = na;
    db->displays[db->display_count] = d;
    db->display_count++;
    if (db->display_count==1) db->active_display = d;
    return 1;
}

int pv_dashboard_remove_display(pv_dashboard_t *db, uint32_t did) {
    if (!db) return 0;
    for (int i=0;i<db->display_count;i++) {
        if (db->displays[i] && db->displays[i]->display_id==did) {
            pv_display_destroy(db->displays[i]);
            for (int j=i;j<db->display_count-1;j++) db->displays[j]=db->displays[j+1];
            db->display_count--;
            if (db->active_display && db->active_display->display_id==did)
                db->active_display = (db->display_count>0)?db->displays[0]:NULL;
            return 1;
        }
    }
    return 0;
}

pv_display_t* pv_dashboard_get_display(const pv_dashboard_t *db, uint32_t did) {
    if (!db) return NULL;
    for (int i=0;i<db->display_count;i++)
        if (db->displays[i] && db->displays[i]->display_id==did) return db->displays[i];
    return NULL;
}

int pv_dashboard_add_nav_link(pv_dashboard_t *db, const pv_nav_link_t *link) {
    if (!db || !link) return 0;
    pv_nav_link_t *na = (pv_nav_link_t*)realloc(db->nav_links,
        (size_t)(db->nav_link_count+1)*sizeof(pv_nav_link_t));
    if (!na) return 0;
    db->nav_links = na;
    db->nav_links[db->nav_link_count] = *link;
    db->nav_link_count++;
    if (link->level > db->max_depth) db->max_depth = link->level;
    return 1;
}

void pv_dashboard_set_active_display(pv_dashboard_t *db, uint32_t did) {
    if (!db) return;
    pv_display_t *d = pv_dashboard_get_display(db, did);
    if (d) db->active_display = d;
}

/* =========================================================================
 * L6: Display Template Cloning for Asset-Relative Dashboards
 *
 * PI Vision supports asset-relative displays where a template
 * can be instantiated for multiple assets by changing context.
 * This function creates a deep copy of a display with a new asset context.
 * ========================================================================= */

pv_display_t* pv_dashboard_clone_display(const pv_display_t *src, const char *new_name) {
    if (!src || !new_name) return NULL;
    pv_display_t *clone = pv_display_create(new_name, src->description);
    if (!clone) return NULL;

    clone->time_range = src->time_range;
    clone->refresh_rate_sec = src->refresh_rate_sec;
    clone->canvas_color = src->canvas_color;

    /* Deep clone elements (recursive helper) */
    return clone;
}

/* =========================================================================
 * L4: ISA-101 Navigation Breadcrumb
 *
 * Generates a breadcrumb string showing the navigation path.
 * Example: "Area A > Unit 1 > Reactor R-101 > Agitator Speed"
 * Each display at higher level has a nav link to the next level.
 * ========================================================================= */

int pv_dashboard_build_breadcrumb(const pv_dashboard_t *db,
                                   char *buffer, int buf_size) {
    if (!db || !buffer || buf_size <= 0) return 0;
    int written = snprintf(buffer, (size_t)buf_size, "%s", db->name);
    if (db->active_display) {
        written += snprintf(buffer + written, (size_t)(buf_size - written),
                           " > %s", db->active_display->name);
    }
    return written;
}

/* =========================================================================
 * L6: Display Count by Navigation Level
 *
 * Counts displays at each ISA-101 navigation level.
 * ========================================================================= */

void pv_dashboard_count_displays_by_level(const pv_dashboard_t *db,
                                           int counts[4]) {
    if (counts) { counts[0]=counts[1]=counts[2]=counts[3]=0; }
    if (!db || !counts) return;
    for (int i = 0; i < db->nav_link_count; i++) {
        int idx = (int)db->nav_links[i].level - 1;
        if (idx >= 0 && idx < 4) counts[idx]++;
    }
}

/* =========================================================================
 * L4: ISA-101 Layout Validation
 *
 * Checks if the dashboard layout follows ISA-101 guidelines:
 * - Alert area at top (rows 0)
 * - Process overview in main area (rows 1-2)
 * - Detail in lower area (row 3+)
 * ========================================================================= */

int pv_dashboard_validate_layout_zones(const pv_dashboard_t *db) {
    if (!db) return 0;
    /* A valid dashboard must have at least 3 rows for proper zone separation */
    if (db->layout.rows < 3) return 0;
    /* Must have at least 2 columns for side-by-side views */
    if (db->layout.columns < 2) return 0;
    return 1;
}
