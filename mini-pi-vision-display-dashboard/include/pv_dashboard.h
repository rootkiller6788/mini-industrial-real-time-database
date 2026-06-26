/**
 * pv_dashboard.h - PI Vision Dashboard Layout & Navigation (L3, L4, L6)
 *
 * Dashboard grid layout engine, display navigation hierarchy,
 * and responsive layout management per ISA-101.
 *
 * Knowledge Coverage:
 *   L3 Engineering Structures: Grid layout engine, responsive calculation
 *   L4 Standards: ISA-101 display navigation hierarchy (Levels 1-4)
 *   L6 Canonical Problems: Process overview, alarm summary layouts
 *
 * Reference: ISA-101.01-2015; OSIsoft PI Vision Dashboard Documentation
 * Course Map: RWTH Aachen Industrial Control, ISA/IEC ISA-101
 */

#ifndef PV_DASHBOARD_H
#define PV_DASHBOARD_H

#include "pv_display.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * L1: Dashboard & Navigation Types
 * ========================================================================= */

/** ISA-101 display hierarchy level */
typedef enum {
    PV_NAV_LEVEL_1 = 1,  /* Process area overview */
    PV_NAV_LEVEL_2 = 2,  /* Unit control */
    PV_NAV_LEVEL_3 = 3,  /* Equipment detail */
    PV_NAV_LEVEL_4 = 4   /* Component/IO detail */
} pv_navigation_level_t;

/** Display navigation link */
typedef struct {
    uint32_t target_display_id;
    char     label[128];
    pv_navigation_level_t level;
    pv_rect_t click_region;
} pv_nav_link_t;

/** Grid layout specification */
typedef struct {
    int columns;
    int rows;
    int gutter_px;         /* Gap between cells */
    int margin_px;         /* Outer margin */
} pv_grid_layout_t;

/** Grid cell placement */
typedef struct {
    int col_start;         /* Column index (0-based) */
    int col_span;          /* Column span (>= 1) */
    int row_start;         /* Row index (0-based) */
    int row_span;          /* Row span (>= 1) */
} pv_grid_cell_t;

/** Dashboard */
typedef struct {
    uint32_t       dashboard_id;
    char           name[128];
    pv_grid_layout_t layout;
    pv_display_t  *active_display;
    int            display_count;
    pv_display_t **displays;     /* Array of display pointers */
    pv_nav_link_t *nav_links;    /* Navigation links array */
    int            nav_link_count;
    pv_navigation_level_t max_depth; /* Maximum navigation depth */
} pv_dashboard_t;

/** Color palette definition (ISA-101 compliant) */
typedef struct {
    pv_color_t background;
    pv_color_t canvas;
    pv_color_t text_primary;
    pv_color_t text_secondary;
    pv_color_t alarm_critical;
    pv_color_t alarm_high;
    pv_color_t alarm_medium;
    pv_color_t alarm_low;
    pv_color_t status_normal;
    pv_color_t status_warning;
    pv_color_t grid_line;
    pv_color_t accent;
} pv_color_palette_t;

/* =========================================================================
 * L3: Grid Layout Engine
 * ========================================================================= */

/**
 * pv_grid_cell_to_rect - Convert grid cell placement to display rectangle
 *
 * Maps a grid cell specification to a percentage-based rectangle.
 * Accounts for gutters and margins proportionally.
 *
 * Formula: x% = margin + col_start * (col_width + gutter)
 *          width% = col_span * col_width + (col_span - 1) * gutter
 *
 * @param layout  Grid layout
 * @param cell    Cell placement
 * @param rect    Output: bounding rectangle in percentage
 *
 * Complexity: O(1)
 */
void pv_grid_cell_to_rect(const pv_grid_layout_t *layout,
                           const pv_grid_cell_t *cell,
                           pv_rect_t *rect);

/**
 * pv_grid_optimal_columns - Compute optimal column count for canvas width
 *
 * Determines the number of columns that gives cell widths closest to
 * a target width (default 200px) while keeping columns >= min_cols.
 *
 * @param canvas_width_px  Canvas width in pixels
 * @param target_cell_px   Target cell width (suggested: 200)
 * @param min_cols         Minimum column count (suggested: 4)
 * @return                 Optimal column count
 *
 * Complexity: O(1)
 */
int pv_grid_optimal_columns(int canvas_width_px, int target_cell_px, int min_cols);

/**
 * pv_grid_cell_from_rect - Find grid cell containing a coordinate
 *
 * Reverse mapping: given a point, find which grid cell it falls in.
 * Useful for click detection in dashboard navigation.
 *
 * @param layout  Grid layout
 * @param point   Point in percentage coordinates
 * @param cell    Output: grid cell containing the point
 * @return        1 if point is within grid, 0 otherwise
 *
 * Complexity: O(1)
 */
int pv_grid_cell_from_rect(const pv_grid_layout_t *layout,
                            pv_coord_t point,
                            pv_grid_cell_t *cell);

/* =========================================================================
 * L4: ISA-101 Navigation
 * ========================================================================= */

/**
 * pv_nav_validate_hierarchy - Validate display navigation hierarchy
 *
 * ISA-101 requires: Level 1 (Overview) -> Level 2 (Unit) -> Level 3 (Detail)
 * -> Level 4 (Component). This function checks that navigation links follow
 * the proper hierarchy (each link goes to same or next level, never skipping).
 *
 * @param links       Array of navigation links
 * @param count       Number of links
 * @param start_level Starting display level
 * @return            1 if valid, 0 if hierarchy violation
 *
 * Complexity: O(N)
 * Standard: ISA-101.01-2015 Section 5.4 Navigation Hierarchy
 */
int pv_nav_validate_hierarchy(const pv_nav_link_t *links, int count,
                               pv_navigation_level_t start_level);

/**
 * pv_nav_count_by_level - Count navigation links at each hierarchy level
 *
 * @param links   Navigation links
 * @param count   Number of links
 * @param level_counts Output: counts[0..3] for levels 1..4
 *
 * Complexity: O(N)
 */
void pv_nav_count_by_level(const pv_nav_link_t *links, int count,
                            int level_counts[4]);

/* =========================================================================
 * L6: Dashboard Factory
 * ========================================================================= */

pv_dashboard_t* pv_dashboard_create(const char *name, int columns, int rows);
void pv_dashboard_destroy(pv_dashboard_t *dashboard);
int pv_dashboard_add_display(pv_dashboard_t *dashboard, pv_display_t *display);
int pv_dashboard_remove_display(pv_dashboard_t *dashboard, uint32_t display_id);
pv_display_t* pv_dashboard_get_display(const pv_dashboard_t *dashboard, uint32_t display_id);
int pv_dashboard_add_nav_link(pv_dashboard_t *dashboard, const pv_nav_link_t *link);
void pv_dashboard_set_active_display(pv_dashboard_t *dashboard, uint32_t display_id);

#ifdef __cplusplus
}
#endif

#endif /* PV_DASHBOARD_H */

pv_display_t* pv_dashboard_clone_display(const pv_display_t *src, const char *new_name);
int pv_dashboard_build_breadcrumb(const pv_dashboard_t *db, char *buffer, int buf_size);
void pv_dashboard_count_displays_by_level(const pv_dashboard_t *db, int counts[4]);
int pv_dashboard_validate_layout_zones(const pv_dashboard_t *db);
