/**
 * @file af_category.c
 * @brief AFCategory implementation — element classification system
 *
 * Knowledge coverage:
 * L1: AFCategory struct operations
 * L2: Element-category many-to-many association
 * L3: Category hierarchy with path computation
 * L4: ISA-95 functional classification (equipment, function, process categories)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "af_category.h"

static void gen_uuid(char *buf, size_t sz)
{
    static uint64_t counter = 0;
    snprintf(buf, sz, "cat-%016llx-%04llx",
             (unsigned long long)(uintptr_t)buf,
             (unsigned long long)(counter++));
}

/* ─── Lifecycle ─────────────────────────────────────────────── */
AFCategory* af_category_create(const char *name)
{
    if (!name || name[0] == '\0') return NULL;

    AFCategory *cat = (AFCategory*)calloc(1, sizeof(AFCategory));
    if (!cat) return NULL;

    gen_uuid(cat->id, sizeof(cat->id));
    strncpy(cat->name, name, AF_MAX_CAT_NAME_LEN - 1);
    cat->name[AF_MAX_CAT_NAME_LEN - 1] = '\0';
    cat->parent = NULL;
    cat->child_count = 0;
    cat->created_time = (uint64_t)(time(NULL) * 1000);
    strncpy(cat->created_by, "af_system", sizeof(cat->created_by) - 1);
    return cat;
}

void af_category_destroy(AFCategory *cat)
{
    if (!cat) return;
    if (cat->parent) {
        af_category_remove_child(cat);
    }
    /* Destroy children recursively */
    while (cat->child_count > 0) {
        AFCategory *child = cat->children[0];
        af_category_remove_child(child);
        af_category_destroy(child);
    }
    free(cat);
}

bool af_category_set_description(AFCategory *cat, const char *desc)
{
    if (!cat || !desc) return false;
    strncpy(cat->description, desc, AF_MAX_CAT_DESC_LEN - 1);
    cat->description[AF_MAX_CAT_DESC_LEN - 1] = '\0';
    return true;
}

/* ─── Hierarchy ─────────────────────────────────────────────── */
bool af_category_add_child(AFCategory *parent, AFCategory *child)
{
    if (!parent || !child) return false;
    if (parent->child_count >= AF_MAX_CAT_CHILDREN) return false;
    if (child->parent) return false;
    if (child == parent) return false;

    parent->children[parent->child_count++] = child;
    child->parent = parent;
    return true;
}

bool af_category_remove_child(AFCategory *child)
{
    if (!child || !child->parent) return false;

    AFCategory *parent = child->parent;
    for (size_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            for (size_t j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->children[--parent->child_count] = NULL;
            child->parent = NULL;
            return true;
        }
    }
    return false;
}

AFCategory* af_category_find_child(const AFCategory *parent, const char *name)
{
    if (!parent || !name) return NULL;
    for (size_t i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i]->name, name) == 0) {
            return parent->children[i];
        }
    }
    return NULL;
}

int af_category_get_path(const AFCategory *cat, char *buf, size_t bufsz)
{
    if (!cat || !buf || bufsz < 2) return -1;

    const AFCategory *nodes[64];
    int depth = 0;
    const AFCategory *curr = cat;
    while (curr && depth < 64) {
        nodes[depth++] = curr;
        curr = curr->parent;
    }

    size_t pos = 0;
    for (int i = depth - 1; i >= 0; i--) {
        size_t nl = strlen(nodes[i]->name);
        if (pos + nl + 2 > bufsz) return -1;
        if (i < depth - 1) {
            buf[pos++] = '\\';
        }
        memcpy(buf + pos, nodes[i]->name, nl);
        pos += nl;
    }
    buf[pos] = '\0';
    return (int)pos;
}

int af_category_get_depth(const AFCategory *cat)
{
    if (!cat) return 0;
    int depth = 0;
    const AFCategory *curr = cat;
    while (curr->parent) {
        depth++;
        curr = curr->parent;
    }
    return depth;
}

/* ─── Element Association ───────────────────────────────────── */
size_t af_category_get_element_count(const AFCategory *cat)
{
    if (!cat) return 0;
    /* In a real PI AF system, the AF server maintains an index.
     * Here we return 0 — the actual count is tracked through
     * af_element_add_category / remove_category calls. */
    return 0;
}

/* ─── Static Utility ────────────────────────────────────────── */
bool af_category_equals(const AFCategory *a, const AFCategory *b)
{
    if (!a || !b) return false;
    return strcmp(a->id, b->id) == 0;
}

const AFCategory* af_category_get_root(const AFCategory *cat)
{
    if (!cat) return NULL;
    const AFCategory *curr = cat;
    while (curr->parent) {
        curr = curr->parent;
    }
    return curr;
}

size_t af_category_subtree_count(const AFCategory *cat)
{
    if (!cat) return 0;
    size_t count = 1;
    for (size_t i = 0; i < cat->child_count; i++) {
        count += af_category_subtree_count(cat->children[i]);
    }
    return count;
}
