/**
 * @file af_template.h
 * @brief PI Asset Framework - AFTemplate definition and API
 *
 * AFTemplate defines the shape (attributes, metadata) that elements
 * created from the template will inherit. Templates support inheritance
 * chains, where a derived template inherits attribute definitions from
 * its base template.
 *
 * L1 Definitions:
 *   - AFTemplate: Reusable element definition with attribute templates
 *   - AFAttrTemplate: Attribute definition within a template
 *   - Template inheritance: derived templates extend base templates
 *
 * L2 Core Concepts:
 *   - Template instantiation: creating elements from templates
 *   - Attribute override: derived templates can override base definitions
 *   - Template versioning: tracking template changes
 *
 * L5 Algorithms:
 *   - Template inheritance merge algorithm (base + derived → resolved)
 *   - Template cycle detection (prevent circular inheritance)
 */

#ifndef AF_TEMPLATE_H
#define AF_TEMPLATE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "af_attribute.h"

#define AF_MAX_TMPL_NAME_LEN    256
#define AF_MAX_ATTR_TEMPLATES   256
#define AF_MAX_INHERIT_DEPTH    32

struct af_template_s;
typedef struct af_template_s AFTemplate;

/* ─── L1: Attribute Template ────────────────────────────────── */
typedef struct {
    char     name[AF_MAX_ATTR_NAME_LEN];
    char     description[AF_MAX_DESCRIPTION_LEN];
    af_value_type_t   value_type;
    char     uom[AF_MAX_UOM_LEN];

    /* Default value */
    af_value_t        default_value;
    bool              has_default;

    /* Data reference */
    af_data_reference_type_t dr_type;
    char     dr_config[AF_MAX_CONFIG_LEN];

    /* Metadata */
    bool     is_hidden;
    bool     is_key_attribute;
    bool     is_required;
    char     category[64];
} af_attr_template_t;

/* ─── L1: AFTemplate Structure ──────────────────────────────── */
struct af_template_s {
    char     id[64];
    char     name[AF_MAX_TMPL_NAME_LEN];
    char     description[AF_MAX_DESCRIPTION_LEN];

    /* Inheritance */
    AFTemplate *base_template;
    int         inherit_depth;

    /* Attribute definitions */
    af_attr_template_t attr_templates[AF_MAX_ATTR_TEMPLATES];
    size_t    attr_tmpl_count;

    /* Metadata */
    uint64_t  created_time;
    uint64_t  modified_time;
    int       version;
    char      created_by[64];
};

/* ─── Template Lifecycle ────────────────────────────────────── */
AFTemplate* af_template_create(const char *name);
void af_template_destroy(AFTemplate *tmpl);
bool af_template_set_description(AFTemplate *tmpl, const char *desc);

/* ─── Attribute Template Management ─────────────────────────── */
bool af_template_add_attr(AFTemplate *tmpl, const char *name,
                           af_value_type_t type, const char *uom);
bool af_template_remove_attr(AFTemplate *tmpl, const char *name);
const af_attr_template_t* af_template_find_attr(const AFTemplate *tmpl,
                                                  const char *name);
size_t af_template_attr_count(const AFTemplate *tmpl);

/**
 * Set the default value for an attribute in the template
 */
bool af_template_set_attr_default(AFTemplate *tmpl, const char *attr_name,
                                   const af_value_t *default_val);

/**
 * Set the data reference configuration for an attribute in the template
 */
bool af_template_set_attr_dr(AFTemplate *tmpl, const char *attr_name,
                              af_data_reference_type_t dr_type,
                              const char *dr_config);

/* ─── Inheritance ───────────────────────────────────────────── */
/**
 * Set the base template for inheritance.
 * Performs cycle detection to prevent circular inheritance.
 * @param tmpl Derived template
 * @param base Base template to inherit from
 * @return true if base set successfully, false if cycle detected
 */
bool af_template_set_base(AFTemplate *tmpl, AFTemplate *base);

/**
 * Get the effective (resolved) attribute template after inheritance.
 * If tmpl defines attr_name locally, returns local definition.
 * Otherwise walks the inheritance chain to find the definition.
 * @param tmpl Template
 * @param attr_name Attribute name
 * @return Resolved attribute template, or NULL
 */
const af_attr_template_t* af_template_resolve_attr(const AFTemplate *tmpl,
                                                      const char *attr_name);

/**
 * List all effective attribute names (including inherited ones).
 * @param tmpl Template
 * @param names Output array (caller-allocated, at least AF_MAX_ATTR_TEMPLATES)
 * @param count Output: number of names filled
 * @return Total count of effective attributes
 */
size_t af_template_list_effective_attrs(const AFTemplate *tmpl,
                                         char names[][AF_MAX_ATTR_NAME_LEN],
                                         size_t *count);

/**
 * Check if a template is in the inheritance chain of another.
 * @param descendant Potential descendant template
 * @param ancestor Potential ancestor template
 * @return true if descendant inherits from ancestor
 */
bool af_template_is_descendant(const AFTemplate *descendant,
                                const AFTemplate *ancestor);

/* ─── Instantiation ─────────────────────────────────────────── */
/**
 * Create a new element from this template.
 * The element will have attributes created from the template's
 * attribute definitions (resolved through inheritance).
 * @param tmpl Template to instantiate
 * @param elem_name Name for the new element
 * @return Newly created element, or NULL on failure
 */
struct af_element_s* af_template_instantiate(const AFTemplate *tmpl,
                                               const char *elem_name);

/* ─── Version Management ────────────────────────────────────── */
int  af_template_get_version(const AFTemplate *tmpl);
void af_template_bump_version(AFTemplate *tmpl);

/* ─── Validation ────────────────────────────────────────────── */
/**
 * Validate template completeness. A valid template must:
 * - Have a unique name
 * - Not have circular inheritance
 * - All required attributes must have defaults or data references
 * @param tmpl Template to validate
 * @return Number of validation errors (0 = valid)
 */
int af_template_validate(const AFTemplate *tmpl);

#endif /* AF_TEMPLATE_H */
