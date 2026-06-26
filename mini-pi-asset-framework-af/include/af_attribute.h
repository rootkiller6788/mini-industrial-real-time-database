/**
 * @file af_attribute.h
 * @brief PI Asset Framework - AFAttribute definition and API
 *
 * AFAttribute stores the actual data values associated with AF elements.
 * Each attribute has a data reference that defines how its value is resolved:
 * from PI Data Archive tags, formulas, constants, or other sources.
 *
 * L1 Definitions:
 *   - AFAttribute: Named value holder with type, UOM, and data reference
 *   - AFValueType: Data type enumeration (Int32, Float64, String, DateTime, Boolean, Enum)
 *   - AFDataReferenceType: Source of attribute value (PI Point, Formula, Table Lookup, etc.)
 *
 * L2 Core Concepts:
 *   - Data reference pipeline (resolve value through configurable sources)
 *   - Attribute value subscription (PI Data Archive streaming)
 *   - Unit of Measure (UOM) conversion
 */

#ifndef AF_ATTRIBUTE_H
#define AF_ATTRIBUTE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#define AF_MAX_ATTR_NAME_LEN    256
#define AF_MAX_UOM_LEN          32
#define AF_MAX_CONFIG_LEN       1024
#define AF_MAX_DESCRIPTION_LEN  4096
#define AF_MAX_PATH_LEN         1024
#define AF_MAX_TMPL_NAME_LEN    256
#define AF_MAX_CAT_NAME_LEN     256

/* ─── L1: Value Type Enumeration ────────────────────────────── */
typedef enum {
    AF_VAL_INT32     = 0,
    AF_VAL_FLOAT64   = 1,
    AF_VAL_STRING    = 2,
    AF_VAL_DATETIME  = 3,
    AF_VAL_BOOLEAN   = 4,
    AF_VAL_ENUM      = 5,
    AF_VAL_BYTE_ARRAY= 6
} af_value_type_t;

/* ─── L1: Data Reference Type ───────────────────────────────── */
typedef enum {
    AF_DR_NONE          = 0,
    AF_DR_PI_POINT      = 1,
    AF_DR_FORMULA       = 2,
    AF_DR_TABLE_LOOKUP  = 3,
    AF_DR_STRING_BUILDER= 4,
    AF_DR_CONSTANT      = 5,
    AF_DR_ATTR_REF      = 6,
    AF_DR_ANALYSIS      = 7,
    AF_DR_URI_BUILDER   = 8
} af_data_reference_type_t;

/* ─── L1: Attribute Value Union ─────────────────────────────── */
typedef struct {
    af_value_type_t type;
    union {
        int32_t     v_int32;
        double      v_float64;
        char        *v_string;
        time_t      v_datetime;
        bool        v_boolean;
        int32_t     v_enum_val;
    } value;
    char uom[AF_MAX_UOM_LEN];
    uint64_t timestamp;    /* When value was last updated (Unix epoch ms) */
    bool is_good;          /* Quality flag: true = good quality */
} af_value_t;

/* ─── L1: AFAttribute Core Structure ────────────────────────── */
typedef struct af_attribute_s {
    char     id[64];
    char     name[AF_MAX_ATTR_NAME_LEN];
    char     description[AF_MAX_DESCRIPTION_LEN];
    af_value_type_t   value_type;

    /* Current value */
    af_value_t        current_value;

    /* Data reference configuration */
    af_data_reference_type_t dr_type;
    char     dr_config[AF_MAX_CONFIG_LEN];

    /* Default value if data reference fails */
    af_value_t        default_value;
    bool              has_default;

    /* Metadata */
    char     uom[AF_MAX_UOM_LEN];
    bool     is_hidden;
    bool     is_configuration_item;
    char     category[64];

    /* The owning element (back-reference) */
    struct af_element_s *owner_element;
} AFAttribute;

/* ─── Attribute Lifecycle ───────────────────────────────────── */
AFAttribute* af_attribute_create(const char *name, af_value_type_t type);
void af_attribute_destroy(AFAttribute *attr);

/* ─── Value Management ──────────────────────────────────────── */
bool af_attribute_set_value(AFAttribute *attr, const af_value_t *val);
const af_value_t* af_attribute_get_value(const AFAttribute *attr);
bool af_attribute_set_default(AFAttribute *attr, const af_value_t *val);
bool af_attribute_copy_value(const AFAttribute *src, AFAttribute *dst);

/* ─── Data Reference Configuration ──────────────────────────── */
bool af_attribute_set_dr_type(AFAttribute *attr, af_data_reference_type_t dr_type);
bool af_attribute_set_dr_config(AFAttribute *attr, const char *config);
const char* af_attribute_get_dr_config(const AFAttribute *attr);

/* ─── UOM Management ────────────────────────────────────────── */
bool af_attribute_set_uom(AFAttribute *attr, const char *uom);
const char* af_attribute_get_uom(const AFAttribute *attr);

/* ─── Value Conversion ──────────────────────────────────────── */
/**
 * Convert attribute value to different representation.
 * @param attr Attribute
 * @param buf Output string buffer
 * @param bufsz Buffer size
 * @return Number of bytes written (excluding null), or -1 on error
 */
int af_attribute_value_to_string(const AFAttribute *attr, char *buf, size_t bufsz);

/**
 * Parse a string into an attribute value based on the attribute's type.
 * @param attr Attribute (determines target type)
 * @param str Input string
 * @param out_val Output value structure
 * @return true if parse succeeded
 */
bool af_attribute_parse_value(const AFAttribute *attr, const char *str, af_value_t *out_val);

/* ─── Quality ───────────────────────────────────────────────── */
bool af_attribute_set_quality(AFAttribute *attr, bool is_good);
bool af_attribute_get_quality(const AFAttribute *attr);

/* ─── Timestamp ─────────────────────────────────────────────── */
void af_attribute_set_timestamp(AFAttribute *attr, uint64_t ts_ms);
uint64_t af_attribute_get_timestamp(const AFAttribute *attr);

/* ─── Metadata ──────────────────────────────────────────────── */
bool af_attribute_set_hidden(AFAttribute *attr, bool hidden);
bool af_attribute_is_hidden(const AFAttribute *attr);
bool af_attribute_set_config_item(AFAttribute *attr, bool is_cfg);
bool af_attribute_is_config_item(const AFAttribute *attr);

#endif /* AF_ATTRIBUTE_H */
