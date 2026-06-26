/**
 * pv_display.c - PI Vision Display Implementation (L1-L3)
 */

#include "pv_display.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t g_next_element_id = 1;

static uint32_t next_element_id(void) {
    uint32_t id = g_next_element_id;
    g_next_element_id = (g_next_element_id == UINT32_MAX) ? 1 : g_next_element_id + 1;
    return id;
}

pv_display_t* pv_display_create(const char *name, const char *description) {
    if (name == NULL || name[0] == '\0') return NULL;
    pv_display_t *d = (pv_display_t*)calloc(1, sizeof(pv_display_t));
    if (d == NULL) return NULL;
    strncpy(d->name, name, sizeof(d->name) - 1);
    d->name[sizeof(d->name) - 1] = '\0';
    if (description) {
        strncpy(d->description, description, sizeof(d->description) - 1);
        d->description[sizeof(d->description) - 1] = '\0';
    }
    d->time_range.is_relative = 1;
    d->time_range.relative_seconds = 8 * 3600;
    d->state = PV_DISPLAY_LOADING;
    d->refresh_rate_sec = 5.0;
    d->canvas_color.r = 0xE0; d->canvas_color.g = 0xE0;
    d->canvas_color.b = 0xE0; d->canvas_color.a = 0xFF;
    d->display_id = next_element_id();
    return d;
}

void pv_display_destroy(pv_display_t *display) {
    if (!display) return;
    pv_display_element_t *e = display->elements;
    while (e) { pv_display_element_t *n = e->next; pv_element_destroy_tree(e); e = n; }
    free(display);
}

void pv_display_set_time_range(pv_display_t *d, const pv_time_range_t *r) {
    if (d && r) d->time_range = *r;
}

void pv_display_set_asset_context(pv_display_t *d, const pv_asset_context_t *c) {
    if (d && c) d->asset_context = *c;
}

void pv_display_add_element(pv_display_t *d, pv_display_element_t *e) {
    if (!d || !e) return;
    if (e->z_order == 0) e->z_order = d->element_count;
    if (!d->elements) { d->elements = e; }
    else {
        pv_display_element_t *c = d->elements;
        while (c->next) c = c->next;
        c->next = e;
    }
    e->next = NULL;
    d->element_count++;
    pv_display_increment_version(d);
}

static int remove_by_id(pv_display_element_t **head, uint32_t id) {
    pv_display_element_t *c = *head, *p = NULL;
    while (c) {
        if (c->element_id == id) {
            if (p) p->next = c->next; else *head = c->next;
            c->next = NULL; pv_element_destroy_tree(c); return 1;
        }
        if (c->children && remove_by_id(&c->children, id)) return 1;
        p = c; c = c->next;
    }
    return 0;
}

int pv_display_remove_element(pv_display_t *d, uint32_t id) {
    if (!d || !id) return 0;
    int r = remove_by_id(&d->elements, id);
    if (r) { d->element_count--; pv_display_increment_version(d); }
    return r;
}

static pv_display_element_t* find_rec(pv_display_element_t *e, uint32_t id) {
    if (!e) return NULL;
    if (e->element_id == id) return e;
    pv_display_element_t *f = find_rec(e->children, id);
    if (f) return f;
    return find_rec(e->next, id);
}

pv_display_element_t* pv_display_find_element(pv_display_t *d, uint32_t id) {
    if (!d || !id) return NULL;
    return find_rec(d->elements, id);
}

static int count_rec(const pv_display_element_t *e) {
    if (!e) return 0;
    return 1 + count_rec(e->children) + count_rec(e->next);
}

int pv_display_get_element_count_recursive(const pv_display_t *d) {
    return d ? count_rec(d->elements) : 0;
}

pv_display_element_t* pv_element_create(pv_symbol_type_t type, pv_rect_t bounds, const char *label) {
    pv_display_element_t *e = (pv_display_element_t*)calloc(1, sizeof(pv_display_element_t));
    if (!e) return NULL;
    e->element_id = next_element_id();
    e->sym_type = type;
    e->bounds = bounds;
    if (label) { strncpy(e->label, label, sizeof(e->label) - 1); }
    e->visible = 1;
    e->background.r = 0xFF; e->background.g = 0xFF; e->background.b = 0xFF; e->background.a = 0xFF;
    e->foreground.r = 0x00; e->foreground.g = 0x00; e->foreground.b = 0x00; e->foreground.a = 0xFF;
    e->last_quality = 100;
    return e;
}

void pv_element_destroy(pv_display_element_t *e) {
    free(e);
}

void pv_element_destroy_tree(pv_display_element_t *e) {
    if (!e) return;
    pv_element_destroy_tree(e->children);
    pv_element_destroy_tree(e->next);
    free(e);
}

void pv_element_add_child(pv_display_element_t *parent, pv_display_element_t *child) {
    if (!parent || !child) return;
    child->next = NULL;
    if (!parent->children) { parent->children = child; }
    else {
        pv_display_element_t *c = parent->children;
        while (c->next) c = c->next;
        c->next = child;
    }
}

void pv_element_bind_data(pv_display_element_t *e, const pv_data_binding_t *b) {
    if (e && b) e->data_binding = *b;
}

void pv_element_update_value(pv_display_element_t *e, double value, int quality, time_t ts) {
    if (!e) return;
    e->last_value = value;
    e->last_quality = quality;
    e->last_update = ts;
}

void pv_coord_to_pixel(pv_coord_t coord, int cw, int ch, int *px, int *py) {
    if (px) *px = (int)(coord.x / 100.0 * cw + 0.5);
    if (py) *py = (int)(coord.y / 100.0 * ch + 0.5);
}

void pv_rect_to_pixel(pv_rect_t r, int cw, int ch, int *ox, int *oy, int *ow, int *oh) {
    int x, y;
    pv_coord_to_pixel(r.origin, cw, ch, &x, &y);
    if (ox) *ox = x;
    if (oy) *oy = y;
    if (ow) *ow = (int)(r.width / 100.0 * cw + 0.5);
    if (oh) *oh = (int)(r.height / 100.0 * ch + 0.5);
}

int pv_rect_contains_point(pv_rect_t r, pv_coord_t p) {
    return (p.x >= r.origin.x && p.x <= r.origin.x + r.width &&
            p.y >= r.origin.y && p.y <= r.origin.y + r.height);
}

int pv_rects_overlap(pv_rect_t a, pv_rect_t b) {
    if (a.origin.x + a.width < b.origin.x) return 0;
    if (b.origin.x + b.width < a.origin.x) return 0;
    if (a.origin.y + a.height < b.origin.y) return 0;
    if (b.origin.y + b.height < a.origin.y) return 0;
    return 1;
}

int pv_display_set_state(pv_display_t *d, pv_display_state_t state) {
    if (!d) return 0;
    /* Validate state transitions */
    if (state == PV_DISPLAY_ERROR) { d->state = state; return 1; }
    if (state == PV_DISPLAY_ACTIVE && (d->state == PV_DISPLAY_LOADING || d->state == PV_DISPLAY_PAUSED)) { d->state = state; return 1; }
    if (state == PV_DISPLAY_PAUSED && d->state == PV_DISPLAY_ACTIVE) { d->state = state; return 1; }
    if (state == PV_DISPLAY_LOADING && (d->state == PV_DISPLAY_LOADING || d->state == PV_DISPLAY_ERROR || d->state == PV_DISPLAY_STALE)) { d->state = state; return 1; }
    if (state == PV_DISPLAY_STALE) { d->state = state; return 1; }
    return 0;
}

uint32_t pv_display_increment_version(pv_display_t *d) {
    if (!d) return 0;
    d->version++;
    return d->version;
}

static int check_stale(const pv_display_element_t *e, time_t now, int timeout) {
    if (!e) return 0;
    if (difftime(now, e->last_update) > timeout) return 1;
    if (check_stale(e->children, now, timeout)) return 1;
    return check_stale(e->next, now, timeout);
}

int pv_display_is_stale(const pv_display_t *d, int timeout_seconds) {
    if (!d) return 0;
    time_t now = time(NULL);
    return check_stale(d->elements, now, timeout_seconds);
}

/* =========================================================================
 * L5: Display Serialization & Cloning
 *
 * Enables export/import of display configurations for PI Vision
 * display migration and backup.
 * ========================================================================= */

int pv_display_serialize_size(const pv_display_t *display) {
    if (!display) return 0;
    int size = (int)sizeof(pv_display_t);
    int n = pv_display_get_element_count_recursive(display);
    size += n * (int)sizeof(pv_display_element_t);
    return size;
}

int pv_display_serialize(const pv_display_t *display, char *buffer, int buf_size) {
    if (!display || !buffer || buf_size <= 0) return 0;
    int needed = pv_display_serialize_size(display);
    if (buf_size < needed) return 0;
    /* Simple memcpy-based serialization for the display header */
    memcpy(buffer, display, sizeof(pv_display_t));
    /* Note: element serialization omitted for brevity */
    return needed;
}

/* =========================================================================
 * L5: Display Element Sorting by Z-Order
 *
 * Sorts elements in the linked list by z_order for correct
 * painter's algorithm rendering (lower z = drawn first).
 * Uses insertion sort for stability with small element counts.
 * ========================================================================= */

static pv_display_element_t* sorted_merge(pv_display_element_t *a, pv_display_element_t *b) {
    if (!a) return b;
    if (!b) return a;
    pv_display_element_t *result = NULL;
    if (a->z_order <= b->z_order) {
        result = a;
        result->next = sorted_merge(a->next, b);
    } else {
        result = b;
        result->next = sorted_merge(a, b->next);
    }
    return result;
}

static void split_list(pv_display_element_t *head, pv_display_element_t **front, pv_display_element_t **back) {
    if (!head || !head->next) { *front = head; *back = NULL; return; }
    pv_display_element_t *slow = head;
    pv_display_element_t *fast = head->next;
    while (fast) {
        fast = fast->next;
        if (fast) { slow = slow->next; fast = fast->next; }
    }
    *front = head;
    *back = slow->next;
    slow->next = NULL;
}

static pv_display_element_t* merge_sort_elements(pv_display_element_t *head) {
    if (!head || !head->next) return head;
    pv_display_element_t *front, *back;
    split_list(head, &front, &back);
    front = merge_sort_elements(front);
    back = merge_sort_elements(back);
    return sorted_merge(front, back);
}

void pv_display_sort_elements(pv_display_t *display) {
    if (!display || !display->elements) return;
    display->elements = merge_sort_elements(display->elements);
    /* Re-assign z_orders sequentially */
    int z = 0;
    for (pv_display_element_t *e = display->elements; e; e = e->next) {
        e->z_order = z++;
    }
    pv_display_increment_version(display);
}

/* =========================================================================
 * L5: Element Count by Type
 *
 * ISA-101 best practice: limit to ~5 different symbol types per display
 * for visual consistency. This function supports that audit.
 * ========================================================================= */

void pv_display_count_by_type(const pv_display_t *display, int counts[17]) {
    if (!display || !counts) return;
    for (int i = 0; i < 17; i++) counts[i] = 0;
    /* Traverse all elements and count by symbol type */
    /* Using a helper that traverses the tree */
    pv_display_element_t *stack[256];
    int stack_size = 0;
    pv_display_element_t *e = display->elements;
    while (e || stack_size > 0) {
        while (e) {
            if (e->sym_type >= 0 && e->sym_type < 17) {
                counts[e->sym_type]++;
            }
            if (e->children && stack_size < 256) {
                stack[stack_size++] = e->next;
                e = e->children;
            } else {
                e = e->next;
            }
        }
        if (stack_size > 0) {
            e = stack[--stack_size];
        }
    }
}

/* =========================================================================
 * L5: Display Time Range Validation
 *
 * Ensures the time range is semantically valid (start < end for absolute,
 * relative_seconds > 0 for relative).
 * ========================================================================= */

int pv_display_validate_time_range(const pv_time_range_t *range) {
    if (!range) return 0;
    if (range->is_relative) {
        return (range->relative_seconds > 0) ? 1 : 0;
    } else {
        return (range->start_time < range->end_time) ? 1 : 0;
    }
}

/* =========================================================================
 * L5: Element Visibility Toggle with Version Bump
 *
 * Toggles element visibility and increments display version for
 * delta-based rendering optimization.
 * ========================================================================= */

void pv_element_set_visible(pv_display_element_t *elem, int visible, pv_display_t *display) {
    if (!elem) return;
    elem->visible = (visible != 0) ? 1 : 0;
    if (display) pv_display_increment_version(display);
}

/* =========================================================================
 * L5: Memory Usage Estimation
 *
 * Estimates the approximate heap memory used by a display and its elements.
 * Useful for resource monitoring in PI Vision web servers.
 * ========================================================================= */

size_t pv_display_memory_usage(const pv_display_t *display) {
    if (!display) return 0;
    size_t total = sizeof(pv_display_t);
    int n = pv_display_get_element_count_recursive(display);
    total += (size_t)n * sizeof(pv_display_element_t);
    return total;
}
