/**
 * @file af_data_reference.h
 * @brief PI Asset Framework - Data Reference Pipeline
 *
 * The data reference pipeline resolves attribute values from their
 * configured sources. This is the core mechanism that connects AF
 * attributes to the PI Data Archive, external data sources, formulas,
 * and analytics.
 *
 * L2 Core Concepts:
 *   - Data reference resolution: attribute → data source → value
 *   - PI Point DR: reads from PI Data Archive tags
 *   - Formula DR: computes values from expressions involving other attributes
 *   - Table Lookup DR: maps input values to output values via lookup tables
 *
 * L3 Engineering Structures:
 *   - Pipeline stages: Resolve → Transform → Validate → Deliver
 *   - Attribute chaining: attribute A references attribute B
 *
 * L5 Algorithms:
 *   - Formula expression parser (infix → RPN → evaluate)
 *   - Table lookup with interpolation
 */

#ifndef AF_DATA_REFERENCE_H
#define AF_DATA_REFERENCE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "af_element.h"
#include "af_attribute.h"
#include "af_template.h"

/* ─── PI Point Data Reference Config ─────────────────────────── */
typedef struct {
    char     pi_server[128];     /* PI Data Archive server name */
    char     pi_tag[256];        /* PI point tag name */
    bool     use_snapshot;       /* Use snapshot vs. archived value */
    char     timestamp_mode[32]; /* "auto", "specific", "at_time" */
    time_t   specific_time;      /* For "specific" timestamp mode */
} af_dr_pi_point_t;

/* ─── Formula Data Reference Config ──────────────────────────── */
typedef struct {
    char     formula[AF_MAX_CONFIG_LEN]; /* Formula expression string */
    char     variable_attrs[16][AF_MAX_ATTR_NAME_LEN]; /* Attribute variables */
    int      variable_count;
} af_dr_formula_t;

/* ─── Table Lookup Data Reference Config ─────────────────────── */
#define AF_MAX_TABLE_ROWS  512
typedef struct {
    double   input;   /* Input key value */
    double   output;  /* Output result value */
} af_table_row_t;

typedef struct {
    af_table_row_t rows[AF_MAX_TABLE_ROWS];
    int    row_count;
    char   input_attr[AF_MAX_ATTR_NAME_LEN];  /* Attribute providing input */
    bool   use_interpolation;  /* Linear interpolation between rows */
} af_dr_table_lookup_t;

/* ─── String Builder Config ──────────────────────────────────── */
typedef struct {
    char     format[AF_MAX_CONFIG_LEN];       /* Format string with {0}, {1}, ... */
    char     component_attrs[16][AF_MAX_ATTR_NAME_LEN];
    int      component_count;
} af_dr_string_builder_t;

/* ─── Analysis Data Reference ────────────────────────────────── */
typedef enum {
    AF_ANALYSIS_ROLLUP_AVG   = 0,
    AF_ANALYSIS_ROLLUP_MIN   = 1,
    AF_ANALYSIS_ROLLUP_MAX   = 2,
    AF_ANALYSIS_ROLLUP_SUM   = 3,
    AF_ANALYSIS_ROLLUP_COUNT = 4,
    AF_ANALYSIS_ROLLUP_STDDEV= 5,
    AF_ANALYSIS_SPC_WESTERN  = 6,
    AF_ANALYSIS_DELTA        = 7,
    AF_ANALYSIS_RATE         = 8
} af_analysis_type_t;

typedef struct {
    af_analysis_type_t  analysis_type;
    char     source_attr[AF_MAX_ATTR_NAME_LEN];
    char     child_template[AF_MAX_TMPL_NAME_LEN];
    double   analysis_param1;
    double   analysis_param2;
    bool     use_child_elements; /* Rollup across children */
} af_dr_analysis_t;

/* ─── Constant Data Reference ────────────────────────────────── */
typedef struct {
    af_value_t constant_value;
} af_dr_constant_t;

/* ─── Attribute Reference ───────────────────────────────────── */
typedef struct {
    char     target_element_path[AF_MAX_PATH_LEN];
    char     target_attr_name[AF_MAX_ATTR_NAME_LEN];
} af_dr_attr_ref_t;

/* ─── Data Reference Context ─────────────────────────────────── */
/**
 * Context passed through the data reference resolution pipeline.
 * Carries the target attribute and optional external value provider.
 */
typedef struct af_dr_context_s {
    AFAttribute        *target_attr;
    const AFElement    *root_element;

    /* External PI Point value provider callback */
    /** Return 0 on success, -1 if point not found */
    int (*pi_value_provider)(const char *pi_server, const char *pi_tag,
                              bool snapshot, af_value_t *out_val);

    /* Logging callback */
    void (*logger)(const char *message);
} af_dr_context_t;

/* ─── Data Reference Pipeline API ────────────────────────────── */
/**
 * Resolve an attribute's value through its configured data reference.
 * Walks the DR pipeline: resolves source → optionally transforms → returns value.
 *
 * @param attr Attribute to resolve (uses attr->dr_type and attr->dr_config)
 * @param ctx Resolution context with external providers
 * @param out_val Output resolved value
 * @return true if resolution succeeded with good quality
 *
 * Resolution flow:
 *   PI_POINT     → calls pi_value_provider callback
 *   FORMULA      → parses expression, evaluates with variable values
 *   TABLE_LOOKUP → looks up input attribute value, finds output row
 *   STRING_BUILDER → formats string from component attributes
 *   CONSTANT     → returns configured constant value
 *   ATTR_REF     → follows reference to another attribute
 *   ANALYSIS     → performs analytical computation
 */
bool af_data_reference_resolve(const AFAttribute *attr,
                                af_dr_context_t *ctx,
                                af_value_t *out_val);

/**
 * Parse the attribute's DR config string into a typed structure.
 * @param attr Attribute with dr_config set
 * @param out_pi Filled if DR type is PI_POINT (may be NULL)
 * @param out_formula Filled if DR type is FORMULA (may be NULL)
 * @param out_table Filled if DR type is TABLE_LOOKUP (may be NULL)
 * @param out_sb Filled if DR type is STRING_BUILDER (may be NULL)
 * @param out_constant Filled if DR type is CONSTANT (may be NULL)
 * @param out_ref Filled if DR type is ATTR_REF (may be NULL)
 * @param out_analysis Filled if DR type is ANALYSIS (may be NULL)
 * @return true if parsing succeeded for the attribute's DR type
 */
bool af_dr_parse_config(const AFAttribute *attr,
                         af_dr_pi_point_t *out_pi,
                         af_dr_formula_t *out_formula,
                         af_dr_table_lookup_t *out_table,
                         af_dr_string_builder_t *out_sb,
                         af_dr_constant_t *out_constant,
                         af_dr_attr_ref_t *out_ref,
                         af_dr_analysis_t *out_analysis);

/* ─── Config Builders ────────────────────────────────────────── */
/** Build PI Point DR config JSON string */
int af_dr_build_pi_point_config(const char *server, const char *tag,
                                  char *buf, size_t bufsz);

/** Build Formula DR config JSON string */
int af_dr_build_formula_config(const char *expression,
                                char *buf, size_t bufsz);

/** Build Table Lookup DR config JSON string */
int af_dr_build_table_config(const char *input_attr, bool interpolate,
                               char *buf, size_t bufsz);

/** Build String Builder DR config JSON string */
int af_dr_build_stringbuilder_config(const char *format,
                                       char *buf, size_t bufsz);

/** Build Constant DR config JSON string */
int af_dr_build_constant_config(const af_value_t *val,
                                  char *buf, size_t bufsz);

/** Build Analysis DR config JSON string */
int af_dr_build_analysis_config(af_analysis_type_t type,
                                  const char *source_attr,
                                  char *buf, size_t bufsz);

#endif /* AF_DATA_REFERENCE_H */
