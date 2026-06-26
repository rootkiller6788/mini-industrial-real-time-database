/**
 * @file af_element.h
 * @brief PI Asset Framework - AFElement definition and API
 *
 * AFElement is the foundational entity in the OSIsoft PI Asset Framework.
 * It represents a physical or logical asset (equipment, location, system)
 * organized in a hierarchical tree structure.
 *
 * ISA-95 Equipment Hierarchy Model mapping:
 *   ENTERPRISE → SITE → AREA → PROCESS_CELL → UNIT → EQUIPMENT_MODULE → CONTROL_MODULE
 *
 * Reference: OSIsoft PI AF SDK 2.x Developer Guide
 *            ISA-95 Part 2: Object Model Attributes
 *
 * L1 Definitions:
 *   - AFElement: Named asset node with path-based addressing
 *   - AFElementType: Element type per ISA-95 equipment hierarchy
 *   - AFHierarchyPath: Absolute and relative path addressing
 *
 * L2 Core Concepts:
 *   - Asset hierarchy traversal (parent/child navigation)
 *   - Template-based element creation
 *   - Category-based element classification
 */

#ifndef AF_ELEMENT_H
#define AF_ELEMENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Forward declarations */
typedef struct af_element_s AFElement;
typedef struct af_attribute_s AFAttribute;
typedef struct af_template_s AFTemplate;
typedef struct af_category_s AFCategory;

/* ─── L1: Element Type Enumeration ─────────────────────────── */
typedef enum {
    AF_ELEM_TYPE_ASSET       = 0,
    AF_ELEM_TYPE_LOCATION    = 1,
    AF_ELEM_TYPE_SYSTEM      = 2,
    AF_ELEM_TYPE_ENTERPRISE  = 3,
    AF_ELEM_TYPE_SITE        = 4,
    AF_ELEM_TYPE_AREA        = 5,
    AF_ELEM_TYPE_PROCESS_CELL= 6,
    AF_ELEM_TYPE_UNIT        = 7,
    AF_ELEM_TYPE_EQ_MODULE   = 8,
    AF_ELEM_TYPE_CTRL_MODULE = 9
} af_element_type_t;

/* Maximum lengths */
#define AF_MAX_NAME_LEN         256
#define AF_MAX_PATH_LEN         1024
#define AF_MAX_DESCRIPTION_LEN  4096
#define AF_MAX_CHILDREN         1024
#define AF_MAX_ATTRIBUTES       512
#define AF_MAX_CATEGORIES       64

/* ─── L1: AFElement Core Structure ──────────────────────────── */
struct af_element_s {
    char     id[64];
    char     name[AF_MAX_NAME_LEN];
    char     description[AF_MAX_DESCRIPTION_LEN];
    af_element_type_t  type;

    AFElement          *parent;
    AFElement          *children[AF_MAX_CHILDREN];
    size_t              child_count;

    AFAttribute        *attributes[AF_MAX_ATTRIBUTES];
    size_t              attr_count;

    AFTemplate         *template_ref;

    AFCategory         *categories[AF_MAX_CATEGORIES];
    size_t              cat_count;

    bool                is_template_derived;
    uint64_t            created_time;
    uint64_t            modified_time;
    char                created_by[64];
};

/* ─── Element Lifecycle ─────────────────────────────────────── */
AFElement* af_element_create(const char *name, af_element_type_t type);
void af_element_destroy(AFElement *elem);
bool af_element_set_description(AFElement *elem, const char *desc);

/* ─── Hierarchy Management ──────────────────────────────────── */
bool af_element_add_child(AFElement *parent, AFElement *child);
bool af_element_remove_child(AFElement *child);
AFElement* af_element_find_child(const AFElement *parent, const char *name);

/* ─── Path Computation ──────────────────────────────────────── */
int af_element_get_path(const AFElement *elem, char *buf, size_t bufsz);
int af_element_get_relative_path(const AFElement *ancestor,
                                  const AFElement *descendant,
                                  char *buf, size_t bufsz);
AFElement* af_element_find_by_path(const AFElement *root, const char *path);

/* ─── Template / Category Association ───────────────────────── */
bool af_element_set_template(AFElement *elem, AFTemplate *tmpl);
int  af_element_apply_template(AFElement *elem, const AFTemplate *tmpl);
bool af_element_add_category(AFElement *elem, AFCategory *cat);
bool af_element_remove_category(AFElement *elem, AFCategory *cat);
bool af_element_has_category(const AFElement *elem, const AFCategory *cat);

/* ─── Attribute Management ──────────────────────────────────── */
bool af_element_add_attribute(AFElement *elem, AFAttribute *attr);
AFAttribute* af_element_get_attribute(const AFElement *elem, const char *name);
bool af_element_remove_attribute(AFElement *elem, const char *name);

/* ─── Traversal Utilities ────────────────────────────────────── */
int  af_element_get_depth(const AFElement *elem);
const AFElement* af_element_get_root(const AFElement *elem);
bool af_element_is_descendant(const AFElement *ancestor,
                               const AFElement *descendant);
size_t af_element_subtree_count(const AFElement *elem);

#endif /* AF_ELEMENT_H */
