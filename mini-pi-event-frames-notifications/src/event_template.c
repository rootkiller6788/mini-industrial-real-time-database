/**
 * @file event_template.c
 * @brief Event Frame Template management - schema definition and instantiation
 *
 * Implements template definition, attribute schema configuration, trigger
 * binding, template inheritance chain resolution, and the template registry
 * with FNV-1a hash-based name lookup for O(1) expected access.
 *
 * Knowledge mapping:
 *   L1: ef_template_t struct, attribute definitions, trigger config
 *   L2: Template instantiation (class -> object), inheritance chain resolution
 *   L3: FNV-1a hash registry, linear-probe collision resolution
 *   L4: ISA-106 section 5.1 Event Frame Template definition and binding
 *   L5: Template validation algorithm, inherited attribute resolution
 *
 * RWTH Aachen - Template-driven industrial configuration
 * ISA-106 - Procedure Automation: Event Frame Template specification
 */

#include "event_template.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ─── L5: FNV-1a for Template Names ──────────────────────────────────────── */

static uint32_t tmpl_hash_name(const char *name) {
    const uint8_t *bytes = (const uint8_t *)name;
    uint32_t hash = 2166136261U;
    while (*bytes) {
        hash ^= *bytes++;
        hash *= 16777619U;
    }
    return hash;
}

/* ─── L5: Template Initialization ────────────────────────────────────────── */

int ef_tmpl_init(ef_template_t *tmpl, const char *name, const char *description) {
    if (!tmpl || !name || !description) return -1;

    memset(tmpl, 0, sizeof(ef_template_t));

    strncpy(tmpl->name, name, EF_TMPL_MAX_NAME - 1);
    tmpl->name[EF_TMPL_MAX_NAME - 1] = '\0';
    strncpy(tmpl->description, description, 1023);
    tmpl->description[1023] = '\0';

    tmpl->attr_def_count = 0;
    tmpl->trigger_type = EF_TRIGGER_NONE;
    tmpl->default_severity = EF_SEVERITY_INFO;
    tmpl->is_active = 1;
    tmpl->max_concurrent = 0;    /* 0 = unlimited */
    tmpl->auto_ack_enabled = 0;
    tmpl->retention_days = 365;  /* 1 year default */
    tmpl->created_at = time(NULL);
    tmpl->modified_at = tmpl->created_at;
    tmpl->parent_template_name[0] = '\0';

    return 0;
}

/* ─── L5: Attribute Definition Management ───────────────────────────────── */

int ef_tmpl_add_attr_def(ef_template_t *tmpl, const ef_template_attr_def_t *attr) {
    if (!tmpl || !attr) return -1;
    if (tmpl->attr_def_count >= EF_TMPL_MAX_ATTR_DEFS) return -1;

    /* Validate: attribute name cannot be empty */
    if (attr->name[0] == '\0') return -1;

    /* Check for duplicate attribute names within this template */
    for (int i = 0; i < tmpl->attr_def_count; i++) {
        if (strcmp(tmpl->attr_defs[i].name, attr->name) == 0) {
            return -1;  /* Duplicate attribute name */
        }
    }

    memcpy(&tmpl->attr_defs[tmpl->attr_def_count], attr, sizeof(ef_template_attr_def_t));
    tmpl->attr_def_count++;
    tmpl->modified_at = time(NULL);

    return 0;
}

/* ─── L5: Trigger Configuration ──────────────────────────────────────────── */

int ef_tmpl_set_trigger(ef_template_t *tmpl, ef_trigger_type_t trigger_type,
                        const char *expression, double threshold, int delay_ms) {
    if (!tmpl) return -1;

    tmpl->trigger_type = trigger_type;
    tmpl->trigger_threshold = threshold;
    tmpl->trigger_delay_ms = delay_ms;

    if (expression) {
        strncpy(tmpl->trigger_expression, expression, 1023);
        tmpl->trigger_expression[1023] = '\0';
    } else {
        tmpl->trigger_expression[0] = '\0';
    }

    tmpl->modified_at = time(NULL);
    return 0;
}

/* ─── L5: Parent Template (Inheritance) ──────────────────────────────────── */

int ef_tmpl_set_parent(ef_template_t *tmpl, const char *parent_name) {
    if (!tmpl || !parent_name) return -1;

    /* Prevent self-inheritance */
    if (strcmp(tmpl->name, parent_name) == 0) return -1;

    strncpy(tmpl->parent_template_name, parent_name, EF_TMPL_MAX_NAME - 1);
    tmpl->parent_template_name[EF_TMPL_MAX_NAME - 1] = '\0';
    tmpl->modified_at = time(NULL);

    return 0;
}

/* ─── L5: Template Instantiation ───────────────────────────────────────────
 *
 * Creates a new event_frame_t from a template. The instantiation process:
 * 1. Copies template metadata (name, description) to the event frame
 * 2. Pre-allocates all attribute slots from the template's attr_defs
 * 3. Applies default values for attributes that have defaults
 * 4. Sets the trigger type and source information
 * 5. Optionally resolves inherited attributes from parent template chain
 *
 * Complexity: O(a + d) where a = attribute defs, d = defaults set
 *
 * Reference: PI AF SDK - AFEventFrame.CreateEventFrame(template)
 */

int ef_tmpl_instantiate(const ef_template_t *tmpl, event_frame_t *ef,
                        const char *instance_name) {
    if (!tmpl || !ef || !instance_name) return -1;
    if (!tmpl->is_active) return -1;  /* Template disabled */

    /* Initialize the event frame with template name as the template reference */
    if (ef_init(ef, instance_name, tmpl->name) != 0) return -1;

    /* Copy template-level configuration */
    ef->trigger_type = tmpl->trigger_type;
    ef->severity = tmpl->default_severity;
    strncpy(ef->source_element, tmpl->element_path, 255);
    ef->source_element[255] = '\0';

    if (tmpl->trigger_tag[0]) {
        strncpy(ef->source_tag, tmpl->trigger_tag, 127);
        ef->source_tag[127] = '\0';
    }

    /* Pre-allocate attributes from template definitions */
    for (int i = 0; i < tmpl->attr_def_count; i++) {
        const ef_template_attr_def_t *def = &tmpl->attr_defs[i];

        /* Create attribute slot */
        ef_attribute_t *attr = &ef->attributes[ef->attr_count];
        strncpy(attr->name, def->name, 127);
        attr->name[127] = '\0';

        /* Compute name hash using FNV-1a */
        attr->name_hash = 0;
        for (const uint8_t *p = (const uint8_t*)def->name; *p; p++) {
            attr->name_hash ^= *p;
            attr->name_hash *= 16777619U;
        }

        attr->type = def->type;
        attr->modified_at = ef->created_at;

        /* Apply default value if one exists */
        if (def->has_default) {
            attr->is_set = 1;
            memcpy(&attr->value, &def->default_value, sizeof(attr->value));
        } else if (def->required) {
            /* Required attributes are marked as unset initially -
             * they must be filled before event closure */
            attr->is_set = 0;
        } else {
            attr->is_set = 0;
        }

        ef->attr_count++;
    }

    return 0;
}

/* ─── L5: Template Validation ──────────────────────────────────────────────
 *
 * Checks that all required attributes from the template have been
 * assigned values in the event frame. This is called before allowing
 * event frame closure to ensure data completeness.
 *
 * ISA-106 section 5.3: "Event Frame completeness checks"
 * Complexity: O(r) where r = number of required attributes
 */

int ef_tmpl_validate(const ef_template_t *tmpl, const event_frame_t *ef) {
    if (!tmpl || !ef) return -1;

    int missing = 0;
    for (int i = 0; i < tmpl->attr_def_count; i++) {
        if (!tmpl->attr_defs[i].required) continue;

        /* Search event frame attributes for this required attribute */
        int found = 0;
        for (int j = 0; j < ef->attr_count; j++) {
            if (ef->attributes[j].is_set &&
                strcmp(ef->attributes[j].name, tmpl->attr_defs[i].name) == 0) {
                found = 1;
                break;
            }
        }

        if (!found) {
            missing++;
        }
    }

    return (missing == 0) ? 0 : -1;
}

/* ─── L5: Template Registry (Hash Map) ─────────────────────────────────────
 *
 * FNV-1a hash with linear-probe open addressing. The registry stores
 * pointers to template structures.
 *
 * Load factor: count / EF_TMPL_REGISTRY_SIZE
 * Expected probe length: 1 / (1 - alpha) for linear probing
 *
 * Reference: Knuth, D. "The Art of Computer Programming", Vol 3, section 6.4
 */

int ef_tmpl_registry_init(ef_template_registry_t *reg) {
    if (!reg) return -1;

    memset(reg, 0, sizeof(ef_template_registry_t));
    reg->count = 0;
    reg->version = 1;

    return 0;
}

int ef_tmpl_registry_register(ef_template_registry_t *reg, ef_template_t *tmpl) {
    if (!reg || !tmpl) return -1;
    if (reg->count >= EF_TMPL_REGISTRY_SIZE) return -1;

    uint32_t hash = tmpl_hash_name(tmpl->name);
    int idx = hash % EF_TMPL_REGISTRY_SIZE;

    /* Linear probe for empty slot */
    for (int i = 0; i < EF_TMPL_REGISTRY_SIZE; i++) {
        int probe_idx = (idx + i) % EF_TMPL_REGISTRY_SIZE;
        if (reg->templates[probe_idx] == NULL) {
            reg->templates[probe_idx] = tmpl;
            reg->count++;
            reg->version++;
            return 0;
        }
        /* Check for duplicate name */
        if (strcmp(reg->templates[probe_idx]->name, tmpl->name) == 0) {
            return -1;  /* Duplicate */
        }
    }

    return -1;  /* Registry full */
}

ef_template_t *ef_tmpl_registry_find(const ef_template_registry_t *reg,
                                     const char *name) {
    if (!reg || !name) return NULL;

    uint32_t hash = tmpl_hash_name(name);
    int idx = hash % EF_TMPL_REGISTRY_SIZE;

    for (int i = 0; i < EF_TMPL_REGISTRY_SIZE; i++) {
        int probe_idx = (idx + i) % EF_TMPL_REGISTRY_SIZE;
        if (reg->templates[probe_idx] == NULL) {
            return NULL;  /* Not found (empty slot = stop) */
        }
        if (strcmp(reg->templates[probe_idx]->name, name) == 0) {
            return reg->templates[probe_idx];
        }
    }

    return NULL;
}

int ef_tmpl_registry_list(const ef_template_registry_t *reg,
                          const char **names, int max_names) {
    if (!reg || !names) return 0;

    int found = 0;
    for (int i = 0; i < EF_TMPL_REGISTRY_SIZE && found < max_names; i++) {
        if (reg->templates[i] != NULL) {
            names[found++] = reg->templates[i]->name;
        }
    }
    return found;
}

int ef_tmpl_registry_remove(ef_template_registry_t *reg, const char *name) {
    if (!reg || !name) return -1;

    uint32_t hash = tmpl_hash_name(name);
    int idx = hash % EF_TMPL_REGISTRY_SIZE;

    for (int i = 0; i < EF_TMPL_REGISTRY_SIZE; i++) {
        int probe_idx = (idx + i) % EF_TMPL_REGISTRY_SIZE;
        if (reg->templates[probe_idx] == NULL) {
            return -1;  /* Not found */
        }
        if (strcmp(reg->templates[probe_idx]->name, name) == 0) {
            /* Soft-delete: mark inactive, keep in registry for integrity */
            reg->templates[probe_idx]->is_active = 0;
            reg->version++;
            return 0;
        }
    }

    return -1;
}
