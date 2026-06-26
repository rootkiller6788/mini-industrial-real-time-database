/**
 * @file af_enumset.c
 * @brief AFEnumSet implementation — enumerated value sets
 *
 * Knowledge coverage:
 * L1: AFEnumSet struct, af_enum_value_t value representation
 * L2: Enumeration lookup (by name, by value), validation
 * L3: Value retirement (soft-delete pattern for versioning)
 * L5: Name/value collision resolution
 *
 * Common PI AF EnumSets: EquipmentStatus, AlarmPriority, MaterialCode,
 * OperatingMode, BatchState
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "af_enumset.h"

static void gen_uuid(char *buf, size_t sz)
{
    static uint64_t counter = 0;
    snprintf(buf, sz, "eset-%016llx-%04llx",
             (unsigned long long)(uintptr_t)buf,
             (unsigned long long)(counter++));
}

/* ─── Lifecycle ─────────────────────────────────────────────── */
af_enumset_t* af_enumset_create(const char *name)
{
    if (!name || name[0] == '\0') return NULL;

    af_enumset_t *es = (af_enumset_t*)calloc(1, sizeof(af_enumset_t));
    if (!es) return NULL;

    gen_uuid(es->id, sizeof(es->id));
    strncpy(es->name, name, AF_MAX_ENUM_NAME_LEN - 1);
    es->name[AF_MAX_ENUM_NAME_LEN - 1] = '\0';
    es->value_count = 0;
    es->version = 1;
    es->created_time = (uint64_t)(time(NULL) * 1000);
    es->modified_time = es->created_time;
    return es;
}

void af_enumset_destroy(af_enumset_t *es)
{
    free(es);
}

bool af_enumset_set_description(af_enumset_t *es, const char *desc)
{
    if (!es || !desc) return false;
    strncpy(es->description, desc, AF_MAX_ENUM_DESC_LEN - 1);
    es->description[AF_MAX_ENUM_DESC_LEN - 1] = '\0';
    return true;
}

/* ─── Value Management ──────────────────────────────────────── */
bool af_enumset_add_value(af_enumset_t *es, const char *name,
                           int32_t value, const char *desc)
{
    if (!es || !name || es->value_count >= AF_MAX_ENUM_VALUES) return false;

    /* Check name uniqueness */
    for (size_t i = 0; i < es->value_count; i++) {
        if (strcmp(es->values[i].name, name) == 0) return false;
    }
    /* Check value uniqueness */
    for (size_t i = 0; i < es->value_count; i++) {
        if (es->values[i].value == value) return false;
    }

    af_enum_value_t *ev = &es->values[es->value_count];
    strncpy(ev->name, name, AF_MAX_ENUM_NAME_LEN - 1);
    ev->name[AF_MAX_ENUM_NAME_LEN - 1] = '\0';
    ev->value = value;
    ev->is_retired = false;
    if (desc) {
        strncpy(ev->description, desc, AF_MAX_ENUM_DESC_LEN - 1);
        ev->description[AF_MAX_ENUM_DESC_LEN - 1] = '\0';
    }
    es->value_count++;
    es->modified_time = (uint64_t)(time(NULL) * 1000);
    return true;
}

bool af_enumset_remove_value(af_enumset_t *es, const char *name)
{
    if (!es || !name) return false;

    for (size_t i = 0; i < es->value_count; i++) {
        if (strcmp(es->values[i].name, name) == 0) {
            for (size_t j = i; j < es->value_count - 1; j++) {
                es->values[j] = es->values[j + 1];
            }
            memset(&es->values[es->value_count - 1], 0, sizeof(af_enum_value_t));
            es->value_count--;
            es->modified_time = (uint64_t)(time(NULL) * 1000);
            return true;
        }
    }
    return false;
}

const af_enum_value_t* af_enumset_find_by_name(const af_enumset_t *es,
                                                 const char *name)
{
    if (!es || !name) return NULL;
    for (size_t i = 0; i < es->value_count; i++) {
        if (strcmp(es->values[i].name, name) == 0 && !es->values[i].is_retired) {
            return &es->values[i];
        }
    }
    return NULL;
}

const af_enum_value_t* af_enumset_find_by_value(const af_enumset_t *es,
                                                  int32_t value)
{
    if (!es) return NULL;
    for (size_t i = 0; i < es->value_count; i++) {
        if (es->values[i].value == value && !es->values[i].is_retired) {
            return &es->values[i];
        }
    }
    return NULL;
}

bool af_enumset_is_valid(const af_enumset_t *es, int32_t value)
{
    return af_enumset_find_by_value(es, value) != NULL;
}

const char* af_enumset_value_name(const af_enumset_t *es, int32_t value)
{
    const af_enum_value_t *ev = af_enumset_find_by_value(es, value);
    if (!ev) {
        /* Also check retired values */
        for (size_t i = 0; i < es->value_count; i++) {
            if (es->values[i].value == value) return es->values[i].name;
        }
        return "UNKNOWN";
    }
    return ev->name;
}

bool af_enumset_retire_value(af_enumset_t *es, const char *name)
{
    if (!es || !name) return false;

    for (size_t i = 0; i < es->value_count; i++) {
        if (strcmp(es->values[i].name, name) == 0) {
            if (es->values[i].is_retired) return false; /* Already retired */
            es->values[i].is_retired = true;
            es->modified_time = (uint64_t)(time(NULL) * 1000);
            return true;
        }
    }
    return false;
}

size_t af_enumset_active_count(const af_enumset_t *es)
{
    if (!es) return 0;
    size_t count = 0;
    for (size_t i = 0; i < es->value_count; i++) {
        if (!es->values[i].is_retired) count++;
    }
    return count;
}

/* ─── Versioning ────────────────────────────────────────────── */
int af_enumset_get_version(const af_enumset_t *es)
{
    return es ? es->version : -1;
}

void af_enumset_bump_version(af_enumset_t *es)
{
    if (!es) return;
    es->version++;
    es->modified_time = (uint64_t)(time(NULL) * 1000);
}
