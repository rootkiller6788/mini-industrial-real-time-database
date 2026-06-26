/**
 * @file event_template.h
 * @brief PI Event Frame Templates - structure-preserving event blueprints
 *
 * Templates define the schema for event frames: which attributes exist,
 * what triggers generate them, and how they relate to AF elements.
 *
 * Knowledge Levels:
 *   L1 Definitions:  ef_template_t, ef_template_attribute_def_t
 *   L2 Core Concepts: Template instantiation, inheritance, attribute typing
 *   L3 Engineering:   Static template registry with name-based hash lookup
 *   L4 Standards:     ISA-106 section 5.1 Event Frame Template definition
 *   L7 Application:   OSIsoft PI AF Template to C struct mapping
 *
 * RWTH Aachen - Industrial Control Systems: Template-based configuration
 * CMU 24-677 - Structured data modeling for process events
 */

#ifndef EVENT_TEMPLATE_H
#define EVENT_TEMPLATE_H

#include "event_frame.h"
#include <stdint.h>
#include <stddef.h>

#define EF_TMPL_MAX_ATTR_DEFS    128
#define EF_TMPL_MAX_NAME         128
#define EF_TMPL_MAX_ELEMENT_PATH 512

typedef struct {
    char           name[128];
    ef_attr_type_t type;
    int            required;
    int            has_default;
    union {
        int32_t     int_val;
        double      float_val;
        int         bool_val;
        int64_t     ts_val;
        char        str_val[256];
        ef_guid_t   guid_val;
        int32_t     enum_val;
    } default_value;
    char           uom[32];
    char           description[256];
} ef_template_attr_def_t;

typedef struct {
    char           name[EF_TMPL_MAX_NAME];
    char           description[1024];
    char           element_path[EF_TMPL_MAX_ELEMENT_PATH];
    ef_trigger_type_t trigger_type;
    char           trigger_expression[1024];
    char           trigger_tag[128];
    double         trigger_threshold;
    int            trigger_delay_ms;
    ef_severity_t  default_severity;
    ef_template_attr_def_t attr_defs[EF_TMPL_MAX_ATTR_DEFS];
    int            attr_def_count;
    char           parent_template_name[EF_TMPL_MAX_NAME];
    time_t         created_at;
    time_t         modified_at;
    int            is_active;
    int            max_concurrent;
    int            auto_ack_enabled;
    int            retention_days;
    char           created_by[64];
    char           modified_by[64];
} ef_template_t;

#define EF_TMPL_REGISTRY_SIZE  512

typedef struct {
    ef_template_t  *templates[EF_TMPL_REGISTRY_SIZE];
    int             count;
    uint64_t        version;
} ef_template_registry_t;

int ef_tmpl_init(ef_template_t *tmpl, const char *name, const char *description);
int ef_tmpl_add_attr_def(ef_template_t *tmpl, const ef_template_attr_def_t *attr);
int ef_tmpl_set_trigger(ef_template_t *tmpl, ef_trigger_type_t trigger_type,
                        const char *expression, double threshold, int delay_ms);
int ef_tmpl_set_parent(ef_template_t *tmpl, const char *parent_name);
int ef_tmpl_instantiate(const ef_template_t *tmpl, event_frame_t *ef,
                        const char *instance_name);
int ef_tmpl_validate(const ef_template_t *tmpl, const event_frame_t *ef);
int ef_tmpl_registry_init(ef_template_registry_t *reg);
int ef_tmpl_registry_register(ef_template_registry_t *reg, ef_template_t *tmpl);
ef_template_t *ef_tmpl_registry_find(const ef_template_registry_t *reg, const char *name);
int ef_tmpl_registry_list(const ef_template_registry_t *reg, const char **names, int max_names);
int ef_tmpl_registry_remove(ef_template_registry_t *reg, const char *name);

#endif
