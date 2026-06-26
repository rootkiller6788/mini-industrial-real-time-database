/**
 * pv_render.c - Rendering Pipeline (L3, L5, L8)
 */
#include "pv_render.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

void pv_damage_region_init(pv_damage_region_t *dr) {
    if (!dr) return;
    dr->region.origin.x=100.0; dr->region.origin.y=100.0;
    dr->region.width=0.0; dr->region.height=0.0; dr->pass_mask=0;
}

void pv_damage_region_add(pv_damage_region_t *dr, pv_rect_t rect) {
    if (!dr) return;
    if (dr->region.width==0.0 && dr->region.height==0.0) { dr->region=rect; return; }
    double r1r=dr->region.origin.x+dr->region.width;
    double r1b=dr->region.origin.y+dr->region.height;
    double r2r=rect.origin.x+rect.width, r2b=rect.origin.y+rect.height;
    double nx=(dr->region.origin.x<rect.origin.x)?dr->region.origin.x:rect.origin.x;
    double ny=(dr->region.origin.y<rect.origin.y)?dr->region.origin.y:rect.origin.y;
    double nr=(r1r>r2r)?r1r:r2r, nb=(r1b>r2b)?r1b:r2b;
    dr->region.origin.x=nx; dr->region.origin.y=ny;
    dr->region.width=nr-nx; dr->region.height=nb-ny;
}

void pv_damage_region_union(pv_damage_region_t *dst, const pv_damage_region_t *src) {
    if (!dst||!src) return;
    pv_damage_region_add(dst, src->region);
    dst->pass_mask|=src->pass_mask;
}

int pv_damage_region_is_empty(const pv_damage_region_t *dr) {
    return (!dr || dr->region.width<=0.0 || dr->region.height<=0.0);
}

void pv_damage_region_clear(pv_damage_region_t *dr) { pv_damage_region_init(dr); }

pv_render_queue_t* pv_render_queue_create(int cap, int cw, int ch) {
    pv_render_queue_t *q=(pv_render_queue_t*)calloc(1,sizeof(pv_render_queue_t));
    if(!q)return NULL;
    q->commands=(pv_render_command_t*)calloc((size_t)cap,sizeof(pv_render_command_t));
    if(!q->commands){free(q);return NULL;}
    q->capacity=cap; q->canvas_width=cw; q->canvas_height=ch; return q;
}

void pv_render_queue_destroy(pv_render_queue_t *q) { if(q){free(q->commands);free(q);} }

int pv_render_queue_add_command(pv_render_queue_t *q, const pv_render_command_t *cmd) {
    if(!q||!cmd||q->count>=q->capacity)return 0;
    q->commands[q->count++]=*cmd; return 1;
}

static int cmp_z(const void *a,const void *b) {
    return ((const pv_render_command_t*)a)->z_order - ((const pv_render_command_t*)b)->z_order;
}

void pv_render_queue_sort(pv_render_queue_t *q) {
    if(q&&q->count>1) qsort(q->commands,(size_t)q->count,sizeof(pv_render_command_t),cmp_z);
}

void pv_render_queue_clear(pv_render_queue_t *q) { if(q)q->count=0; }

int pv_render_queue_get_commands_by_pass(const pv_render_queue_t *q, pv_render_pass_t pass, pv_render_command_t *out, int max_out) {
    if(!q||!out||max_out<=0)return 0;
    int found=0;
    for(int i=0;i<q->count&&found<max_out;i++)
        if((int)(q->commands[i].type/PV_RENDER_PASS_COUNT)==(int)pass) out[found++]=q->commands[i];
    return found;
}

typedef struct { uint32_t key; pv_render_command_t cmd; int valid; } cache_ent_t;

pv_display_cache_t* pv_display_cache_create(int max_e) {
    if(max_e<=0)return NULL;
    pv_display_cache_t *c=(pv_display_cache_t*)calloc(1,sizeof(pv_display_cache_t));
    if(!c)return NULL;
    c->cache_entries=calloc((size_t)max_e,sizeof(cache_ent_t));
    if(!c->cache_entries){free(c);return NULL;}
    c->max_entries=max_e; return c;
}

void pv_display_cache_destroy(pv_display_cache_t *c) { if(c){free(c->cache_entries);free(c);} }

int pv_display_cache_lookup(pv_display_cache_t *c, uint32_t key, pv_render_command_t *out) {
    if(!c||!out)return 0;
    cache_ent_t *e=(cache_ent_t*)c->cache_entries;
    for(int i=0;i<c->max_entries;i++) {
        if(e[i].valid&&e[i].key==key){*out=e[i].cmd;c->hits++;return 1;}
    }
    c->misses++; return 0;
}

void pv_display_cache_insert(pv_display_cache_t *c, uint32_t key, const pv_render_command_t *cmd) {
    if(!c||!cmd)return;
    cache_ent_t *e=(cache_ent_t*)c->cache_entries;
    for(int i=0;i<c->max_entries;i++) {
        if(!e[i].valid){e[i].key=key;e[i].cmd=*cmd;e[i].valid=1;c->entry_count++;return;}
    }
    e[0].key=key; e[0].cmd=*cmd; /* FIFO eviction */
}

void pv_display_cache_invalidate(pv_display_cache_t *c) {
    if(!c)return;
    cache_ent_t *e=(cache_ent_t*)c->cache_entries;
    for(int i=0;i<c->max_entries;i++)e[i].valid=0;
    c->entry_count=0;
}

double pv_display_cache_hit_rate(const pv_display_cache_t *c) {
    if(!c)return 0.0;
    int t=c->hits+c->misses;
    return (t>0)?(double)c->hits/(double)t:0.0;
}

pv_resolution_mode_t pv_render_adaptive_resolution(const pv_time_range_t *tr, int canvas_w) {
    if(!tr)return PV_RES_AUTO;
    double dur = tr->is_relative ? (double)tr->relative_seconds : difftime(tr->end_time, tr->start_time);
    int dp = canvas_w/2;
    if(dur<=60.0)return PV_RES_RAW;
    if(dur<=3600.0)return (dp>=3600)?PV_RES_SECOND:PV_RES_MINUTE;
    if(dur<=86400.0)return (dp>=1440)?PV_RES_MINUTE:PV_RES_HOUR;
    if(dur<=604800.0)return (dp>=168)?PV_RES_HOUR:PV_RES_DAY;
    if(dur<=2592000.0)return PV_RES_DAY;
    return PV_RES_MONTH;
}

/* =========================================================================
 * L5: Render Pass Scheduling
 *
 * Schedules render passes based on damage region analysis.
 * Returns a bitmask of passes that need to be executed.
 * ========================================================================= */

int pv_render_schedule_passes(const pv_damage_region_t *dr) {
    if (!dr || pv_damage_region_is_empty(dr)) return 0;
    /* If any damage, we need at minimum the background pass */
    int pass_mask = 1 << PV_RENDER_PASS_BACKGROUND;
    /* Check if symbols area is damaged */
    if (dr->region.width > 0.0 && dr->region.height > 0.0) {
        pass_mask |= 1 << PV_RENDER_PASS_SYMBOLS;
    }
    return pass_mask;
}

/* =========================================================================
 * L5: Canvas Coordinate Bounds Checking
 *
 * Clamps pixel coordinates to canvas boundaries to prevent
 * rendering outside the display area.
 * ========================================================================= */

void pv_render_clamp_to_canvas(int *x, int *y, int canvas_w, int canvas_h) {
    if (!x || !y) return;
    if (*x < 0) *x = 0;
    if (*x >= canvas_w) *x = canvas_w - 1;
    if (*y < 0) *y = 0;
    if (*y >= canvas_h) *y = canvas_h - 1;
}

/* =========================================================================
 * L5: Color Blending for Overlays
 *
 * Alpha-blends a foreground color onto a background color.
 * Uses standard Porter-Duff "over" compositing.
 * result = fg * alpha + bg * (1 - alpha)
 *
 * Knowledge: Alpha compositing for transparent overlays.
 * Reference: Porter & Duff (1984) SIGGRAPH
 * ========================================================================= */

pv_color_t pv_render_blend_colors(pv_color_t fg, pv_color_t bg) {
    pv_color_t result;
    double alpha = fg.a / 255.0;
    double inv_alpha = 1.0 - alpha;
    result.r = (uint8_t)(fg.r * alpha + bg.r * inv_alpha + 0.5);
    result.g = (uint8_t)(fg.g * alpha + bg.g * inv_alpha + 0.5);
    result.b = (uint8_t)(fg.b * alpha + bg.b * inv_alpha + 0.5);
    double a_val = fg.a + bg.a * inv_alpha;
    result.a = (uint8_t)(a_val > 255.0 ? 255.0 : a_val + 0.5);
    return result;
}

/* =========================================================================
 * L5: HSV to RGB Conversion
 *
 * Converts HSV color space to RGB for implementing color gradients
 * and heat maps in PI Vision displays.
 *
 * Knowledge: Color space transformation for data visualization.
 * ========================================================================= */

pv_color_t pv_render_hsv_to_rgb(double h, double s, double v) {
    pv_color_t c;
    c.a = 255;
    if (s <= 0.0) {
        c.r = c.g = c.b = (uint8_t)(v * 255.0);
        return c;
    }
    h = fmod(h, 360.0);
    if (h < 0.0) h += 360.0;
    h /= 60.0;
    int i = (int)floor(h);
    double f = h - (double)i;
    double p = v * (1.0 - s);
    double q = v * (1.0 - s * f);
    double t = v * (1.0 - s * (1.0 - f));
    double r, g, b;
    switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    c.r = (uint8_t)(r * 255.0 + 0.5);
    c.g = (uint8_t)(g * 255.0 + 0.5);
    c.b = (uint8_t)(b * 255.0 + 0.5);
    return c;
}

/* =========================================================================
 * L5: Render Statistics
 *
 * Tracks and reports rendering performance metrics for
 * dashboard optimization.
 * ========================================================================= */

typedef struct {
    int frames_rendered;
    int commands_issued;
    int cache_hits;
    int cache_misses;
    double total_render_time_ms;
} pv_render_stats_t;

static pv_render_stats_t g_render_stats = {0, 0, 0, 0, 0.0};

void pv_render_stats_reset(void) {
    memset(&g_render_stats, 0, sizeof(g_render_stats));
}

void pv_render_stats_record(int commands, int cache_hits, int cache_misses, double time_ms) {
    g_render_stats.frames_rendered++;
    g_render_stats.commands_issued += commands;
    g_render_stats.cache_hits += cache_hits;
    g_render_stats.cache_misses += cache_misses;
    g_render_stats.total_render_time_ms += time_ms;
}

void pv_render_stats_print(void) {
    printf("Render Statistics:\n");
    printf("  Frames: %d\n", g_render_stats.frames_rendered);
    printf("  Commands: %d\n", g_render_stats.commands_issued);
    printf("  Cache hit rate: %.1f%%\n",
           (g_render_stats.cache_hits + g_render_stats.cache_misses > 0) ?
           100.0 * (double)g_render_stats.cache_hits /
           (double)(g_render_stats.cache_hits + g_render_stats.cache_misses) : 0.0);
    printf("  Avg frame time: %.2f ms\n",
           (g_render_stats.frames_rendered > 0) ?
           g_render_stats.total_render_time_ms / g_render_stats.frames_rendered : 0.0);
}
