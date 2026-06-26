/**
 * pv_render.h - PI Vision Rendering Pipeline (L3, L5, L8)
 *
 * Display rendering pipeline with damage region tracking,
 * delta-based updates, and display cache management.
 *
 * Knowledge Coverage:
 *   L3 Engineering Structures: Render pass scheduling, command queue
 *   L5 Algorithms: Damage region tracking, delta optimization
 *   L8 Advanced Topics: Adaptive resolution rendering
 *
 * Reference: OSIsoft PI Vision Rendering Architecture
 * Course Map: Stanford ENGR205, MIT 2.171, CMU 24-677
 */

#ifndef PV_RENDER_H
#define PV_RENDER_H

#include "pv_display.h"
#include "pv_symbol.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PV_RENDER_PASS_BACKGROUND = 0,
    PV_RENDER_PASS_GRID       = 1,
    PV_RENDER_PASS_SYMBOLS    = 2,
    PV_RENDER_PASS_OVERLAYS   = 3,
    PV_RENDER_PASS_COUNT      = 4
} pv_render_pass_t;

typedef enum {
    PV_RCMD_NONE       = 0,
    PV_RCMD_FILL_RECT  = 1,
    PV_RCMD_DRAW_LINE  = 2,
    PV_RCMD_DRAW_TEXT  = 3,
    PV_RCMD_DRAW_ARC   = 4,
    PV_RCMD_DRAW_IMAGE = 5
} pv_render_command_type_t;

typedef struct {
    pv_rect_t region;
    int       pass_mask;
} pv_damage_region_t;

typedef struct {
    pv_render_command_type_t type;
    pv_rect_t bounds;
    pv_color_t color;
    double    params[8];
    char      text[256];
    int       z_order;
} pv_render_command_t;

typedef struct {
    pv_render_command_t *commands;
    int capacity;
    int count;
    int canvas_width;
    int canvas_height;
} pv_render_queue_t;

typedef struct {
    void *cache_entries;
    int   entry_count;
    int   max_entries;
    int   hits;
    int   misses;
} pv_display_cache_t;

/* Damage tracking */
void pv_damage_region_init(pv_damage_region_t *dr);
void pv_damage_region_add(pv_damage_region_t *dr, pv_rect_t rect);
void pv_damage_region_union(pv_damage_region_t *dst, const pv_damage_region_t *src);
int  pv_damage_region_is_empty(const pv_damage_region_t *dr);
void pv_damage_region_clear(pv_damage_region_t *dr);

/* Render queue */
pv_render_queue_t* pv_render_queue_create(int capacity, int canvas_w, int canvas_h);
void pv_render_queue_destroy(pv_render_queue_t *queue);
int  pv_render_queue_add_command(pv_render_queue_t *queue, const pv_render_command_t *cmd);
void pv_render_queue_sort(pv_render_queue_t *queue);
void pv_render_queue_clear(pv_render_queue_t *queue);
int  pv_render_queue_get_commands_by_pass(const pv_render_queue_t *queue, pv_render_pass_t pass, pv_render_command_t *out, int max_out);

/* Display cache */
pv_display_cache_t* pv_display_cache_create(int max_entries);
void pv_display_cache_destroy(pv_display_cache_t *cache);
int  pv_display_cache_lookup(pv_display_cache_t *cache, uint32_t key, pv_render_command_t *out);
void pv_display_cache_insert(pv_display_cache_t *cache, uint32_t key, const pv_render_command_t *cmd);
void pv_display_cache_invalidate(pv_display_cache_t *cache);
double pv_display_cache_hit_rate(const pv_display_cache_t *cache);

/* Adaptive resolution (L8) */
pv_resolution_mode_t pv_render_adaptive_resolution(const pv_time_range_t *time_range, int canvas_width);

#ifdef __cplusplus
}
#endif

#endif /* PV_RENDER_H */
