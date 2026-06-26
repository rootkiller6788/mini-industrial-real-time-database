/**
 * @file af_enumset.h
 * @brief PI Asset Framework - AFEnumSet definition and API
 *
 * AFEnumSet defines enumerated value sets used for constrained
 * attribute values in the PI AF system. Common in industrial
 * applications for equipment status, alarm severity, operating
 * modes, and material codes.
 *
 * L1 Definitions:
 *   - AFEnumSet: Named collection of enumerated values
 *   - AFEnumValue: Single named value in an enumeration set
 *
 * L2 Core Concepts:
 *   - Constrained value domains (process states, equipment modes)
 *   - Enumeration value lookup and validation
 */

#ifndef AF_ENUMSET_H
#define AF_ENUMSET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define AF_MAX_ENUM_NAME_LEN    256
#define AF_MAX_ENUM_DESC_LEN    1024
#define AF_MAX_ENUM_VALUES      256

/* ─── L1: Enumeration Value ─────────────────────────────────── */
typedef struct {
    char     name[AF_MAX_ENUM_NAME_LEN];
    int32_t  value;
    char     description[AF_MAX_ENUM_DESC_LEN];
    bool     is_retired;
} af_enum_value_t;

/* ─── L1: AFEnumSet Structure ───────────────────────────────── */
typedef struct {
    char     id[64];
    char     name[AF_MAX_ENUM_NAME_LEN];
    char     description[AF_MAX_ENUM_DESC_LEN];

    af_enum_value_t values[AF_MAX_ENUM_VALUES];
    size_t   value_count;

    uint64_t created_time;
    uint64_t modified_time;
    int      version;
} af_enumset_t;

/* ─── EnumSet Lifecycle ─────────────────────────────────────── */
af_enumset_t* af_enumset_create(const char *name);
void af_enumset_destroy(af_enumset_t *es);
bool af_enumset_set_description(af_enumset_t *es, const char *desc);

/* ─── Value Management ──────────────────────────────────────── */
/**
 * Add an enumerated value to the set.
 * @return true if added, false if name or value already exists
 */
bool af_enumset_add_value(af_enumset_t *es, const char *name,
                           int32_t value, const char *desc);

/**
 * Remove a value by name
 */
bool af_enumset_remove_value(af_enumset_t *es, const char *name);

/**
 * Find an enumerated value by name.
 * @return Pointer to value, or NULL if not found
 */
const af_enum_value_t* af_enumset_find_by_name(const af_enumset_t *es,
                                                 const char *name);

/**
 * Find an enumerated value by integer value.
 * @return Pointer to value, or NULL if not found
 */
const af_enum_value_t* af_enumset_find_by_value(const af_enumset_t *es,
                                                  int32_t value);

/**
 * Check if a given integer value is valid in this enumeration set.
 * Ignores retired values.
 */
bool af_enumset_is_valid(const af_enumset_t *es, int32_t value);

/**
 * Get the name string for a given integer value.
 * @return Name string, or "UNKNOWN" if not found
 */
const char* af_enumset_value_name(const af_enumset_t *es, int32_t value);

/**
 * Mark a value as retired (soft-delete, not removed).
 */
bool af_enumset_retire_value(af_enumset_t *es, const char *name);

/**
 * Count active (non-retired) values.
 */
size_t af_enumset_active_count(const af_enumset_t *es);

/* ─── Version Management ────────────────────────────────────── */
int  af_enumset_get_version(const af_enumset_t *es);
void af_enumset_bump_version(af_enumset_t *es);

#endif /* AF_ENUMSET_H */
