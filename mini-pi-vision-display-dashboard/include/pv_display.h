/**
 * pv_display.h — PI Vision Display Object Model (L1: Core Definitions)
 *
 * Defines fundamental data structures for industrial display dashboards
 * following the OSIsoft PI Vision display model and ISA-101 HMI hierarchy.
 *
 * Knowledge Coverage:
 *   L1 Definitions: Display, DisplayElement, DataBinding, TimeRange,
 *                   AssetContext, DisplayState, ResolutionMode
 *   L2 Core Concepts: Display hierarchy, data subscription, real-time update
 *   L3 Engineering Structures: Object tree, coordinate system, grid layout
 *
 * Reference: ISA-101.01-2015 HMI Standard; OSIsoft PI Vision Documentation
 * Course Map: MIT 2.171, Stanford ENGR205, ISA/IEC ISA-101
 */

#ifndef PV_DISPLAY_H
#define PV_DISPLAY_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * L1: Core Display Types
 * ========================================================================= */

/** Display resolution modes for multi-scale rendering */
typedef enum {
    PV_RES_AUTO        = 0,
    PV_RES_RAW         = 1,
    PV_RES_SECOND      = 2,
    PV_RES_MINUTE      = 3,
    PV_RES_HOUR        = 4,
    PV_RES_DAY         = 5,
    PV_RES_WEEK        = 6,
    PV_RES_MONTH       = 7
} pv_resolution_mode_t;

/** Display state for lifecycle management */
typedef enum {
    PV_DISPLAY_LOADING = 0,
    PV_DISPLAY_ACTIVE  = 1,
    PV_DISPLAY_PAUSED  = 2,
    PV_DISPLAY_STALE   = 3,
    PV_DISPLAY_ERROR   = 4
} pv_display_state_t;

/** Time range specification with absolute and relative modes */
typedef struct {
    int     is_relative;
    time_t  start_time;
    time_t  end_time;
    int64_t relative_seconds;
    char    label[64];
} pv_time_range_t;

/** Data source binding to PI Point or AF Attribute */
typedef struct {
    char   server_name[128];
    char   point_name[256];
    char   attribute_path[512];
    char   uom[32];
    double zero;
    double span;
    int    is_step;
    int    is_af_attribute;
} pv_data_binding_t;

/** Coordinate in percentage-based display space */
typedef struct {
    double x;
    double y;
} pv_coord_t;

/** Rectangle in display coordinates */
typedef struct {
    pv_coord_t origin;
    double     width;
    double     height;
} pv_rect_t;

/** Asset Framework context for asset-relative displays */
typedef struct {
    char af_database[128];
    char root_element[256];
    char current_element[256];
    int  template_level;
} pv_asset_context_t;

/** RGBA color (8 bits per channel) */
typedef struct {
    uint8_t r, g, b, a;
} pv_color_t;

/* =========================================================================
 * L1: Symbol Type Enumeration
 * ========================================================================= */

typedef enum {
    PV_SYM_NONE             = 0,
    PV_SYM_VALUE            = 1,
    PV_SYM_TREND            = 2,
    PV_SYM_BAR_GRAPH        = 3,
    PV_SYM_GAUGE            = 4,
    PV_SYM_STATE_INDICATOR  = 5,
    PV_SYM_TEXT_LABEL       = 6,
    PV_SYM_IMAGE            = 7,
    PV_SYM_TABLE            = 8,
    PV_SYM_ALARM_LIST       = 9,
    PV_SYM_KPI_INDICATOR    = 10,
    PV_SYM_PIE_CHART        = 11,
    PV_SYM_XY_PLOT          = 12,
    PV_SYM_ASSET_COMPARISON = 13,
    PV_SYM_HORIZONTAL_BAR   = 14,
    PV_SYM_VERTICAL_BAR     = 15,
    PV_SYM_RADIAL_GAUGE     = 16
} pv_symbol_type_t;

typedef struct pv_display_element pv_display_element_t;

/** Display element - a visual item on a display */
struct pv_display_element {
    uint32_t              element_id;
    pv_symbol_type_t      sym_type;
    pv_rect_t             bounds;
    pv_data_binding_t     data_binding;
    pv_color_t            background;
    pv_color_t            foreground;
    int                   visible;
    int                   z_order;
    char                  label[128];
    double                last_value;
    int                   last_quality;
    time_t                last_update;
    pv_display_element_t *next;
    pv_display_element_t *children;
};

/* =========================================================================
 * L1: Display Definition
 * ========================================================================= */

/** PI Vision Display */
typedef struct {
    uint32_t              display_id;
    char                  name[128];
    char                  description[512];
    pv_time_range_t       time_range;
    pv_asset_context_t    asset_context;
    pv_display_state_t    state;
    double                refresh_rate_sec;
    pv_color_t            canvas_color;
    int                   element_count;
    pv_display_element_t *elements;
    void                 *render_context;
    uint32_t              version;
} pv_display_t;

/* =========================================================================
 * L2: Display Lifecycle API
 * ========================================================================= */

pv_display_t* pv_display_create(const char *name, const char *description);
void pv_display_destroy(pv_display_t *display);
void pv_display_set_time_range(pv_display_t *display, const pv_time_range_t *range);
void pv_display_set_asset_context(pv_display_t *display, const pv_asset_context_t *ctx);
void pv_display_add_element(pv_display_t *display, pv_display_element_t *elem);
int  pv_display_remove_element(pv_display_t *display, uint32_t element_id);
pv_display_element_t* pv_display_find_element(pv_display_t *display, uint32_t element_id);
int  pv_display_get_element_count_recursive(const pv_display_t *display);

pv_display_element_t* pv_element_create(pv_symbol_type_t sym_type,
                                         pv_rect_t bounds, const char *label);
void pv_element_destroy(pv_display_element_t *elem);
void pv_element_destroy_tree(pv_display_element_t *elem);
void pv_element_add_child(pv_display_element_t *parent, pv_display_element_t *child);
void pv_element_bind_data(pv_display_element_t *elem, const pv_data_binding_t *binding);
void pv_element_update_value(pv_display_element_t *elem,
                              double value, int quality, time_t timestamp);

/* =========================================================================
 * L3: Coordinate Transforms
 * ========================================================================= */

void pv_coord_to_pixel(pv_coord_t coord, int canvas_width, int canvas_height,
                        int *px, int *py);
void pv_rect_to_pixel(pv_rect_t rect, int canvas_w, int canvas_h,
                       int *out_x, int *out_y, int *out_w, int *out_h);
int  pv_rect_contains_point(pv_rect_t rect, pv_coord_t point);
int  pv_rects_overlap(pv_rect_t a, pv_rect_t b);

/* =========================================================================
 * L3: Display State & Versioning
 * ========================================================================= */

int      pv_display_set_state(pv_display_t *display, pv_display_state_t state);
uint32_t pv_display_increment_version(pv_display_t *display);
int      pv_display_is_stale(const pv_display_t *display, int timeout_seconds);

#ifdef __cplusplus
}
#endif

#endif /* PV_DISPLAY_H */

/* =========================================================================
 * L5: Additional Display API
 * ========================================================================= */

int pv_display_serialize_size(const pv_display_t *display);
int pv_display_serialize(const pv_display_t *display, char *buffer, int buf_size);
void pv_display_sort_elements(pv_display_t *display);
void pv_display_count_by_type(const pv_display_t *display, int counts[17]);
int pv_display_validate_time_range(const pv_time_range_t *range);
void pv_element_set_visible(pv_display_element_t *elem, int visible, pv_display_t *display);
size_t pv_display_memory_usage(const pv_display_t *display);
