/**
 * @file af_template.c
 * @brief AFTemplate implementation — template inheritance and instantiation
 *
 * Knowledge coverage:
 * L1: AFTemplate struct and af_attr_template_t operations
 * L2: Template inheritance chain (base → derived template)
 * L3: Effective attribute resolution (merge base + derived)
 * L4: Template version management (semantics of immutable templates)
 * L5: Cycle detection in inheritance graph (DFS-based)
 * L5: Template instantiation algorithm (apply to element)
 *
 * Reference: OSIsoft PI AF SDK Template Concepts
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "af_template.h"
#include "af_element.h"
#include "af_attribute.h"

/* ─── UUID ───────────────────────────────────────────────────── */
static void gen_uuid(char *buf, size_t sz)
{
    static uint64_t counter = 0;
    snprintf(buf, sz, "tmpl-%016llx-%04llx",
             (unsigned long long)(uintptr_t)buf,
             (unsigned long long)(counter++));
}

/* ─── Template Lifecycle ────────────────────────────────────── */
AFTemplate* af_template_create(const char *name)
{
    if (!name || name[0] == '\0') return NULL;

    AFTemplate *tmpl = (AFTemplate*)calloc(1, sizeof(AFTemplate));
    if (!tmpl) return NULL;

    gen_uuid(tmpl->id, sizeof(tmpl->id));
    strncpy(tmpl->name, name, AF_MAX_TMPL_NAME_LEN - 1);
    tmpl->name[AF_MAX_TMPL_NAME_LEN - 1] = '\0';
    tmpl->base_template = NULL;
    tmpl->inherit_depth = 0;
    tmpl->attr_tmpl_count = 0;
    tmpl->version = 1;
    tmpl->created_time = (uint64_t)(time(NULL) * 1000);
    tmpl->modified_time = tmpl->created_time;
    strncpy(tmpl->created_by, "af_system", sizeof(tmpl->created_by) - 1);
    return tmpl;
}

void af_template_destroy(AFTemplate *tmpl)
{
    if (!tmpl) return;

    /* Free default values for string type attributes */
    for (size_t i = 0; i < tmpl->attr_tmpl_count; i++) {
        af_attr_template_t *at = &tmpl->attr_templates[i];
        if (at->has_default && at->value_type == AF_VAL_STRING &&
            at->default_value.value.v_string) {
            free(at->default_value.value.v_string);
            at->default_value.value.v_string = NULL;
        }
    }

    free(tmpl);
}

bool af_template_set_description(AFTemplate *tmpl, const char *desc)
{
    if (!tmpl || !desc) return false;
    strncpy(tmpl->description, desc, AF_MAX_DESCRIPTION_LEN - 1);
    tmpl->description[AF_MAX_DESCRIPTION_LEN - 1] = '\0';
    tmpl->modified_time = (uint64_t)(time(NULL) * 1000);
    return true;
}

/* ─── Attribute Template Management ─────────────────────────── */
bool af_template_add_attr(AFTemplate *tmpl, const char *name,
                           af_value_type_t type, const char *uom)
{
    if (!tmpl || !name || tmpl->attr_tmpl_count >= AF_MAX_ATTR_TEMPLATES)
        return false;

    /* Check for duplicate */
    for (size_t i = 0; i < tmpl->attr_tmpl_count; i++) {
        if (strcmp(tmpl->attr_templates[i].name, name) == 0) return false;
    }

    af_attr_template_t *at = &tmpl->attr_templates[tmpl->attr_tmpl_count];
    memset(at, 0, sizeof(*at));
    strncpy(at->name, name, AF_MAX_ATTR_NAME_LEN - 1);
    at->value_type = type;
    if (uom) {
        strncpy(at->uom, uom, AF_MAX_UOM_LEN - 1);
    }
    at->has_default = false;
    at->is_hidden = false;
    at->is_key_attribute = false;
    at->is_required = false;
    tmpl->attr_tmpl_count++;
    tmpl->modified_time = (uint64_t)(time(NULL) * 1000);
    return true;
}

bool af_template_remove_attr(AFTemplate *tmpl, const char *name)
{
    if (!tmpl || !name) return false;

    for (size_t i = 0; i < tmpl->attr_tmpl_count; i++) {
        if (strcmp(tmpl->attr_templates[i].name, name) == 0) {
            /* Shift remaining */
            for (size_t j = i; j < tmpl->attr_tmpl_count - 1; j++) {
                tmpl->attr_templates[j] = tmpl->attr_templates[j + 1];
            }
            memset(&tmpl->attr_templates[tmpl->attr_tmpl_count - 1], 0,
                   sizeof(af_attr_template_t));
            tmpl->attr_tmpl_count--;
            tmpl->modified_time = (uint64_t)(time(NULL) * 1000);
            return true;
        }
    }
    return false;
}

const af_attr_template_t* af_template_find_attr(const AFTemplate *tmpl,
                                                  const char *name)
{
    if (!tmpl || !name) return NULL;

    for (size_t i = 0; i < tmpl->attr_tmpl_count; i++) {
        if (strcmp(tmpl->attr_templates[i].name, name) == 0) {
            return &tmpl->attr_templates[i];
        }
    }
    return NULL;
}

size_t af_template_attr_count(const AFTemplate *tmpl)
{
    return tmpl ? tmpl->attr_tmpl_count : 0;
}

bool af_template_set_attr_default(AFTemplate *tmpl, const char *attr_name,
                                   const af_value_t *default_val)
{
    if (!tmpl || !attr_name || !default_val) return false;

    /* Find attribute template by name */
    for (size_t i = 0; i < tmpl->attr_tmpl_count; i++) {
        if (strcmp(tmpl->attr_templates[i].name, attr_name) == 0) {
            af_attr_template_t *at = &tmpl->attr_templates[i];
            /* Free old string default */
            if (at->has_default && at->value_type == AF_VAL_STRING &&
                at->default_value.value.v_string) {
                free(at->default_value.value.v_string);
                at->default_value.value.v_string = NULL;
            }
            /* Copy default value */
            at->default_value = *default_val;
            at->has_default = true;
            /* Deep copy string */
            if (default_val->type == AF_VAL_STRING && default_val->value.v_string) {
                at->default_value.value.v_string = strdup(default_val->value.v_string);
            }
            tmpl->modified_time = (uint64_t)(time(NULL) * 1000);
            return true;
        }
    }
    return false;
}

bool af_template_set_attr_dr(AFTemplate *tmpl, const char *attr_name,
                              af_data_reference_type_t dr_type,
                              const char *dr_config)
{
    if (!tmpl || !attr_name) return false;

    for (size_t i = 0; i < tmpl->attr_tmpl_count; i++) {
        if (strcmp(tmpl->attr_templates[i].name, attr_name) == 0) {
            af_attr_template_t *at = &tmpl->attr_templates[i];
            at->dr_type = dr_type;
            if (dr_config) {
                strncpy(at->dr_config, dr_config, AF_MAX_CONFIG_LEN - 1);
                at->dr_config[AF_MAX_CONFIG_LEN - 1] = '\0';
            } else {
                at->dr_config[0] = '\0';
            }
            tmpl->modified_time = (uint64_t)(time(NULL) * 1000);
            return true;
        }
    }
    return false;
}

/* ─── Inheritance ───────────────────────────────────────────── */
bool af_template_set_base(AFTemplate *tmpl, AFTemplate *base)
{
    if (!tmpl || !base) return false;
    if (tmpl == base) return false;

    /* Cycle detection: DFS from base through inheritance chain.
     * If we find tmpl in base's ancestry, we have a cycle. */
    const AFTemplate *curr = base;
    int max_depth = AF_MAX_INHERIT_DEPTH;
    while (curr && max_depth-- > 0) {
        if (curr == tmpl) return false; /* Cycle detected! */
        curr = curr->base_template;
    }
    if (max_depth <= 0) return false; /* Chain too deep */

    tmpl->base_template = base;
    tmpl->inherit_depth = base->inherit_depth + 1;
    tmpl->modified_time = (uint64_t)(time(NULL) * 1000);
    return true;
}

const af_attr_template_t* af_template_resolve_attr(const AFTemplate *tmpl,
                                                      const char *attr_name)
{
    if (!tmpl || !attr_name) return NULL;

    /* Walk the inheritance chain: local first, then base */
    const AFTemplate *curr = tmpl;
    int max_depth = AF_MAX_INHERIT_DEPTH;

    while (curr && max_depth-- > 0) {
        for (size_t i = 0; i < curr->attr_tmpl_count; i++) {
            if (strcmp(curr->attr_templates[i].name, attr_name) == 0) {
                return &curr->attr_templates[i];
            }
        }
        curr = curr->base_template;
    }
    return NULL;
}

size_t af_template_list_effective_attrs(const AFTemplate *tmpl,
                                         char names[][AF_MAX_ATTR_NAME_LEN],
                                         size_t *count)
{
    if (!tmpl || !names || !count) return 0;

    size_t total = 0;
    *count = 0;

    /* Walk inheritance chain from most-derived to base.
     * Later (base) names are only added if not already present.
     * We iterate derived-first for "override" behavior:
     * locally defined attrs take precedence. */
    const AFTemplate *chain[AF_MAX_INHERIT_DEPTH + 1];
    int chain_len = 0;
    const AFTemplate *curr = tmpl;
    while (curr && chain_len <= AF_MAX_INHERIT_DEPTH) {
        chain[chain_len++] = curr;
        curr = curr->base_template;
    }

    /* Count total unique attributes across chain */
    typedef struct { char name[AF_MAX_ATTR_NAME_LEN]; int found; } unique_t;
    unique_t unique[AF_MAX_ATTR_TEMPLATES * 2];
    int unique_count = 0;

    for (int c = 0; c < chain_len; c++) {
        for (size_t i = 0; i < chain[c]->attr_tmpl_count; i++) {
            const char *nm = chain[c]->attr_templates[i].name;
            /* Check if already in unique list */
            int found = 0;
            for (int u = 0; u < unique_count; u++) {
                if (strcmp(unique[u].name, nm) == 0) { found = 1; break; }
            }
            if (!found && unique_count < (int)(AF_MAX_ATTR_TEMPLATES * 2)) {
                strncpy(unique[unique_count].name, nm, AF_MAX_ATTR_NAME_LEN - 1);
                unique[unique_count].name[AF_MAX_ATTR_NAME_LEN - 1] = '\0';
                unique[unique_count].found = 1;
                unique_count++;
                total++;
                if (total <= AF_MAX_ATTR_TEMPLATES && names) {
                    strncpy(names[*count], nm, AF_MAX_ATTR_NAME_LEN - 1);
                    names[*count][AF_MAX_ATTR_NAME_LEN - 1] = '\0';
                    (*count)++;
                }
            }
        }
    }
    return total;
}

bool af_template_is_descendant(const AFTemplate *descendant,
                                const AFTemplate *ancestor)
{
    if (!descendant || !ancestor) return false;

    const AFTemplate *curr = descendant;
    int max_depth = AF_MAX_INHERIT_DEPTH;
    while (curr && max_depth-- > 0) {
        if (curr == ancestor) return true;
        curr = curr->base_template;
    }
    return false;
}

/* ─── Instantiation ─────────────────────────────────────────── */
AFElement* af_template_instantiate(const AFTemplate *tmpl,
                                    const char *elem_name)
{
    if (!tmpl || !elem_name) return NULL;

    AFElement *elem = af_element_create(elem_name, AF_ELEM_TYPE_ASSET);
    if (!elem) return NULL;

    int created = af_element_apply_template(elem, tmpl);
    if (created < 0) {
        af_element_destroy(elem);
        return NULL;
    }

    return elem;
}

/* ─── Version Management ────────────────────────────────────── */
int af_template_get_version(const AFTemplate *tmpl)
{
    return tmpl ? tmpl->version : -1;
}

void af_template_bump_version(AFTemplate *tmpl)
{
    if (!tmpl) return;
    tmpl->version++;
    tmpl->modified_time = (uint64_t)(time(NULL) * 1000);
}

/* ─── Validation ────────────────────────────────────────────── */
int af_template_validate(const AFTemplate *tmpl)
{
    if (!tmpl) return -1;

    int errors = 0;

    /* Must have a name */
    if (tmpl->name[0] == '\0') errors++;

    /* Check inheritance depth */
    if (tmpl->inherit_depth > AF_MAX_INHERIT_DEPTH) errors++;

    /* Check for required attributes without defaults or DR */
    for (size_t i = 0; i < tmpl->attr_tmpl_count; i++) {
        const af_attr_template_t *at = &tmpl->attr_templates[i];
        if (at->is_required && !at->has_default &&
            at->dr_type == AF_DR_NONE) {
            errors++;
        }
    }

    return errors;
}
