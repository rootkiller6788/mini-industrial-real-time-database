/**
 * @file af_category.h
 * @brief PI Asset Framework - AFCategory definition and API
 *
 * AFCategory provides a tagging/classification mechanism for AF elements.
 * Categories follow ISA-95 classifications for equipment, functions,
 * and processes. Elements can have multiple categories assigned.
 *
 * L1 Definitions:
 *   - AFCategory: Named classification tag for elements
 *   - Category hierarchy: nested categories form taxonomies
 *
 * L2 Core Concepts:
 *   - Multi-category classification (ISA-95 functional model)
 *   - Category-based element filtering and search
 */

#ifndef AF_CATEGORY_H
#define AF_CATEGORY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define AF_MAX_CAT_NAME_LEN     256
#define AF_MAX_CAT_DESC_LEN     1024
#define AF_MAX_CAT_CHILDREN     128
#define AF_MAX_CAT_ELEMENTS     4096

struct af_element_s;
typedef struct af_category_s AFCategory;

/* ─── L1: AFCategory Structure ──────────────────────────────── */
struct af_category_s {
    char     id[64];
    char     name[AF_MAX_CAT_NAME_LEN];
    char     description[AF_MAX_CAT_DESC_LEN];

    /* Hierarchy */
    AFCategory  *parent;
    AFCategory  *children[AF_MAX_CAT_CHILDREN];
    size_t       child_count;

    /* Metadata */
    uint64_t  created_time;
    char      created_by[64];
};

/* ─── Category Lifecycle ────────────────────────────────────── */
AFCategory* af_category_create(const char *name);
void af_category_destroy(AFCategory *cat);
bool af_category_set_description(AFCategory *cat, const char *desc);

/* ─── Hierarchy ─────────────────────────────────────────────── */
bool af_category_add_child(AFCategory *parent, AFCategory *child);
bool af_category_remove_child(AFCategory *child);
AFCategory* af_category_find_child(const AFCategory *parent, const char *name);
int  af_category_get_path(const AFCategory *cat, char *buf, size_t bufsz);
int  af_category_get_depth(const AFCategory *cat);

/* ─── Element Association ───────────────────────────────────── */
/** Get the count of elements currently assigned to this category */
size_t af_category_get_element_count(const AFCategory *cat);

/* ─── Static Utility ────────────────────────────────────────── */
/** Check if two categories are equal (by ID) */
bool af_category_equals(const AFCategory *a, const AFCategory *b);

/** Get the root ancestor category */
const AFCategory* af_category_get_root(const AFCategory *cat);

/** Count total categories in tree */
size_t af_category_subtree_count(const AFCategory *cat);

#endif /* AF_CATEGORY_H */
