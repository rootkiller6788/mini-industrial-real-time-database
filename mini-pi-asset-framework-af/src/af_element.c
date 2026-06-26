/**
 * @file af_element.c
 * @brief AFElement implementation — asset hierarchy management
 *
 * Implements the PI AF element hierarchy: creation, destruction,
 * parent-child navigation, path computation, template/category association,
 * and subtree traversal.
 *
 * Knowledge coverage:
 * L1: AFElement struct operations (create, destroy, set properties)
 * L2: Asset hierarchy (add/remove child, find child, parent navigation)
 * L3: AF path computation (absolute and relative, ISA-95 conventions)
 * L4: ISA-95 equipment hierarchy model enforcement
 *
 * Reference: OSIsoft PI AF SDK 2.10 Developer Guide
 *            ISA-95 Part 2 Object Model
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "af_element.h"
#include "af_attribute.h"
#include "af_template.h"
#include "af_category.h"

/* ─── UUID Generation ────────────────────────────────────────── */
static void gen_uuid(char *buf, size_t sz)
{
    /* Simple deterministic UUID-like string based on time and counter.
     * In production PI AF, UUID would use RFC 4122.
     * This generates a unique-enough ID for testing purposes. */
    static uint64_t counter = 0;
    uint64_t ts = (uint64_t)time(NULL);
    uint64_t c = counter++;
    snprintf(buf, sz, "%016llx-%016llx-%04llx",
             (unsigned long long)ts,
             (unsigned long long)((uintptr_t)buf ^ c),
             (unsigned long long)c);
}

/* ─── Element Lifecycle ──────────────────────────────────────── */
AFElement* af_element_create(const char *name, af_element_type_t type)
{
    if (!name || name[0] == '\0') return NULL;

    AFElement *elem = (AFElement*)calloc(1, sizeof(AFElement));
    if (!elem) return NULL;

    gen_uuid(elem->id, sizeof(elem->id));
    strncpy(elem->name, name, AF_MAX_NAME_LEN - 1);
    elem->name[AF_MAX_NAME_LEN - 1] = '\0';
    elem->type = type;

    elem->parent = NULL;
    elem->children[0] = NULL;
    elem->child_count = 0;
    elem->attr_count = 0;
    elem->cat_count = 0;
    elem->template_ref = NULL;
    elem->is_template_derived = false;

    elem->created_time = (uint64_t)(time(NULL) * 1000);
    elem->modified_time = elem->created_time;
    strncpy(elem->created_by, "af_system", sizeof(elem->created_by) - 1);

    return elem;
}

static void af_element_destroy_recursive(AFElement *elem)
{
    if (!elem) return;

    /* Destroy all children recursively */
    while (elem->child_count > 0) {
        AFElement *child = elem->children[0];
        /* Detach first to avoid double-free */
        af_element_remove_child(child);
        af_element_destroy_recursive(child);
    }

    /* Destroy all attributes */
    for (size_t i = 0; i < elem->attr_count; i++) {
        if (elem->attributes[i]) {
            af_attribute_destroy(elem->attributes[i]);
            elem->attributes[i] = NULL;
        }
    }

    free(elem);
}

void af_element_destroy(AFElement *elem)
{
    if (!elem) return;

    /* Detach from parent first */
    if (elem->parent) {
        af_element_remove_child(elem);
    }
    af_element_destroy_recursive(elem);
}

bool af_element_set_description(AFElement *elem, const char *desc)
{
    if (!elem || !desc) return false;
    strncpy(elem->description, desc, AF_MAX_DESCRIPTION_LEN - 1);
    elem->description[AF_MAX_DESCRIPTION_LEN - 1] = '\0';
    elem->modified_time = (uint64_t)(time(NULL) * 1000);
    return true;
}

/* ─── Hierarchy Management ──────────────────────────────────── */
bool af_element_add_child(AFElement *parent, AFElement *child)
{
    if (!parent || !child) return false;
    if (parent->child_count >= AF_MAX_CHILDREN) return false;
    if (child->parent != NULL) return false; /* Already has parent */

    /* Prevent circular hierarchy */
    if (child == parent) return false;
    if (af_element_is_descendant(child, parent)) return false;

    parent->children[parent->child_count] = child;
    parent->child_count++;
    child->parent = parent;

    parent->modified_time = (uint64_t)(time(NULL) * 1000);
    return true;
}

bool af_element_remove_child(AFElement *child)
{
    if (!child || !child->parent) return false;

    AFElement *parent = child->parent;
    for (size_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            /* Shift remaining children */
            for (size_t j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->children[parent->child_count - 1] = NULL;
            parent->child_count--;
            child->parent = NULL;
            parent->modified_time = (uint64_t)(time(NULL) * 1000);
            return true;
        }
    }
    return false;
}

AFElement* af_element_find_child(const AFElement *parent, const char *name)
{
    if (!parent || !name) return NULL;
    for (size_t i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i]->name, name) == 0) {
            return parent->children[i];
        }
    }
    return NULL;
}

/* ─── Path Computation ──────────────────────────────────────── */
int af_element_get_path(const AFElement *elem, char *buf, size_t bufsz)
{
    if (!elem || !buf || bufsz < 4) return -1;

    /* Build path bottom-up: collect names, then reverse */
    const AFElement *nodes[256];  /* Max depth 256 */
    int depth = 0;
    const AFElement *curr = elem;

    while (curr && depth < 256) {
        nodes[depth++] = curr;
        curr = curr->parent;
    }

    /* Build path string: \\Root\Child\...\Element */
    size_t pos = 0;
    for (int i = depth - 1; i >= 0; i--) {
        const char *name = nodes[i]->name;
        size_t name_len = strlen(name);

        /* Need space for "\\" + name + "\0" */
        if (pos + 2 + name_len + 1 > bufsz) return -1;

        buf[pos++] = '\\';
        buf[pos++] = '\\';
        if (i < depth - 1) {
            /* Only first component needs two backslashes, rest need one */
            /* PI AF path: \\Enterprise\Site\Area\Unit */
            /* Actually format is \\Enterprise\Site\... (two backslashes at start, single separators) */
            /* Already wrote \\, now write name */
            pos -= 1; /* Overwrite second backslash for intermediate components */
            memcpy(buf + pos, name, name_len);
            pos += name_len;
            if (i > 0) {
                buf[pos++] = '\\';
            }
        } else {
            /* Root element: \\Name */
            memcpy(buf + pos, name, name_len);
            pos += name_len;
        }
    }

    buf[pos] = '\0';
    return (int)pos;
}

int af_element_get_relative_path(const AFElement *ancestor,
                                  const AFElement *descendant,
                                  char *buf, size_t bufsz)
{
    if (!ancestor || !descendant || !buf || bufsz < 2) return -1;

    /* Verify descendant is under ancestor */
    if (!af_element_is_descendant(ancestor, descendant)) return -1;

    /* Collect nodes from descendant up to (but not including) ancestor */
    const AFElement *nodes[256];
    int depth = 0;
    const AFElement *curr = descendant;

    while (curr && curr != ancestor && depth < 256) {
        nodes[depth++] = curr;
        curr = curr->parent;
    }

    /* Build relative path */
    size_t pos = 0;
    buf[pos++] = '.';
    buf[pos++] = '\\';

    for (int i = depth - 1; i >= 0; i--) {
        size_t name_len = strlen(nodes[i]->name);
        if (pos + name_len + 2 > bufsz) return -1;
        memcpy(buf + pos, nodes[i]->name, name_len);
        pos += name_len;
        if (i > 0) {
            buf[pos++] = '\\';
        }
    }

    buf[pos] = '\0';
    return (int)pos;
}

AFElement* af_element_find_by_path(const AFElement *root, const char *path)
{
    if (!root || !path) return NULL;

    /* Parse path: \\Comp1\Comp2\...\CompN */
    /* Skip leading backslashes */
    const char *p = path;
    while (*p == '\\') p++;

    if (*p == '\0') return (AFElement*)root;

    /* Extract first component */
    char component[AF_MAX_NAME_LEN];
    int ci = 0;
    while (*p && *p != '\\' && ci < (int)sizeof(component) - 1) {
        component[ci++] = *p++;
    }
    component[ci] = '\0';

    /* If first component matches root name, skip it (absolute path from root) */
    if (strcmp(root->name, component) == 0) {
        while (*p == '\\') p++;
        if (*p == '\0') return (AFElement*)root;
        return af_element_find_by_path(root, p);
    }

    /* Find child with matching name */
    AFElement *child = af_element_find_child(root, component);
    if (!child) return NULL;

    /* If more path, recurse; else return child */
    while (*p == '\\') p++;
    if (*p == '\0') return child;

    return af_element_find_by_path(child, p);
}

/* ─── Template / Category Association ───────────────────────── */
bool af_element_set_template(AFElement *elem, AFTemplate *tmpl)
{
    if (!elem) return false;
    elem->template_ref = tmpl;
    elem->modified_time = (uint64_t)(time(NULL) * 1000);
    return true;
}

int af_element_apply_template(AFElement *elem, const AFTemplate *tmpl)
{
    if (!elem || !tmpl) return -1;

    /* Get all effective attributes from the template (including inherited) */
    char attr_names[AF_MAX_ATTR_TEMPLATES][AF_MAX_ATTR_NAME_LEN];
    size_t count = 0;
    size_t total = af_template_list_effective_attrs(tmpl, attr_names, &count);

    int created = 0;
    for (size_t i = 0; i < count && i < total; i++) {
        /* Check if attribute already exists on element */
        if (af_element_get_attribute(elem, attr_names[i])) continue;

        const af_attr_template_t *atmpl = af_template_resolve_attr(tmpl, attr_names[i]);
        if (!atmpl) continue;

        AFAttribute *attr = af_attribute_create(attr_names[i], atmpl->value_type);
        if (!attr) continue;

        /* Set UOM */
        if (atmpl->uom[0] != '\0') {
            af_attribute_set_uom(attr, atmpl->uom);
        }

        /* Set default value if configured */
        if (atmpl->has_default) {
            af_attribute_set_default(attr, &atmpl->default_value);
            af_attribute_set_value(attr, &atmpl->default_value);
        }

        /* Set data reference */
        if (atmpl->dr_type != AF_DR_NONE) {
            af_attribute_set_dr_type(attr, atmpl->dr_type);
            if (atmpl->dr_config[0] != '\0') {
                af_attribute_set_dr_config(attr, atmpl->dr_config);
            }
        }

        /* Set metadata */
        af_attribute_set_hidden(attr, atmpl->is_hidden);

        if (!af_element_add_attribute(elem, attr)) {
            af_attribute_destroy(attr);
            continue;
        }
        created++;
    }

    elem->is_template_derived = true;
    elem->template_ref = (AFTemplate*)tmpl;
    elem->modified_time = (uint64_t)(time(NULL) * 1000);
    return created;
}

bool af_element_add_category(AFElement *elem, AFCategory *cat)
{
    if (!elem || !cat) return false;
    if (elem->cat_count >= AF_MAX_CATEGORIES) return false;

    /* Check for duplicates */
    for (size_t i = 0; i < elem->cat_count; i++) {
        if (af_category_equals(elem->categories[i], cat)) return false;
    }

    elem->categories[elem->cat_count++] = cat;
    elem->modified_time = (uint64_t)(time(NULL) * 1000);
    return true;
}

bool af_element_remove_category(AFElement *elem, AFCategory *cat)
{
    if (!elem || !cat) return false;

    for (size_t i = 0; i < elem->cat_count; i++) {
        if (af_category_equals(elem->categories[i], cat)) {
            /* Shift remaining */
            for (size_t j = i; j < elem->cat_count - 1; j++) {
                elem->categories[j] = elem->categories[j + 1];
            }
            elem->categories[--elem->cat_count] = NULL;
            elem->modified_time = (uint64_t)(time(NULL) * 1000);
            return true;
        }
    }
    return false;
}

bool af_element_has_category(const AFElement *elem, const AFCategory *cat)
{
    if (!elem || !cat) return false;
    for (size_t i = 0; i < elem->cat_count; i++) {
        if (af_category_equals(elem->categories[i], cat)) return true;
    }
    return false;
}

/* ─── Attribute Management ──────────────────────────────────── */
bool af_element_add_attribute(AFElement *elem, AFAttribute *attr)
{
    if (!elem || !attr) return false;
    if (elem->attr_count >= AF_MAX_ATTRIBUTES) return false;

    /* Check for duplicate names */
    for (size_t i = 0; i < elem->attr_count; i++) {
        if (strcmp(elem->attributes[i]->name, attr->name) == 0) return false;
    }

    elem->attributes[elem->attr_count++] = attr;
    attr->owner_element = elem;
    elem->modified_time = (uint64_t)(time(NULL) * 1000);
    return true;
}

AFAttribute* af_element_get_attribute(const AFElement *elem, const char *name)
{
    if (!elem || !name) return NULL;
    for (size_t i = 0; i < elem->attr_count; i++) {
        if (strcmp(elem->attributes[i]->name, name) == 0) {
            return elem->attributes[i];
        }
    }
    return NULL;
}

bool af_element_remove_attribute(AFElement *elem, const char *name)
{
    if (!elem || !name) return false;

    for (size_t i = 0; i < elem->attr_count; i++) {
        if (strcmp(elem->attributes[i]->name, name) == 0) {
            af_attribute_destroy(elem->attributes[i]);
            /* Shift remaining */
            for (size_t j = i; j < elem->attr_count - 1; j++) {
                elem->attributes[j] = elem->attributes[j + 1];
            }
            elem->attributes[--elem->attr_count] = NULL;
            elem->modified_time = (uint64_t)(time(NULL) * 1000);
            return true;
        }
    }
    return false;
}

/* ─── Traversal Utilities ────────────────────────────────────── */
int af_element_get_depth(const AFElement *elem)
{
    if (!elem) return 0;
    int depth = 0;
    const AFElement *curr = elem;
    while (curr->parent) {
        depth++;
        curr = curr->parent;
    }
    return depth;
}

const AFElement* af_element_get_root(const AFElement *elem)
{
    if (!elem) return NULL;
    const AFElement *curr = elem;
    while (curr->parent) {
        curr = curr->parent;
    }
    return curr;
}

bool af_element_is_descendant(const AFElement *ancestor,
                               const AFElement *descendant)
{
    if (!ancestor || !descendant) return false;

    const AFElement *curr = descendant;
    while (curr) {
        if (curr == ancestor) return true;
        curr = curr->parent;
    }
    return false;
}

size_t af_element_subtree_count(const AFElement *elem)
{
    if (!elem) return 0;
    size_t count = 1; /* Count self */
    for (size_t i = 0; i < elem->child_count; i++) {
        count += af_element_subtree_count(elem->children[i]);
    }
    return count;
}
