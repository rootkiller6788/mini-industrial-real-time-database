/**
 * pi_security.c - PI System Security Implementation
 * Identity management, access control lists, permission checking.
 * Knowledge: L1-L5  L4 Standards (ISA/IEC 62443)  Purdue ME 575
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/pi_security.h"

void pi_security_init(pi_security_db_t *sec) {
    if (!sec) return;
    memset(sec, 0, sizeof(*sec));
    sec->next_identity_id = 1;
    sec->audit_enabled = 1;
}

void pi_security_destroy(pi_security_db_t *sec) {
    if (!sec) return;
    memset(sec, 0, sizeof(*sec));
}

int pi_security_create_identity(pi_security_db_t *sec, const char *name, pi_identity_type_t type, int32_t *out_id) {
    if (!sec || !name || !out_id) return -1;
    if (sec->identity_count >= PI_SECURITY_MAX_IDENTITIES) return -2;
    int i;
    for (i = 0; i < sec->identity_count; i++)
        if (strcmp(sec->identities[i].name, name) == 0) return -3;
    pi_identity_t *id = &sec->identities[sec->identity_count];
    memset(id, 0, sizeof(*id));
    strncpy(id->name, name, PI_SECURITY_IDENTITY_MAX_LEN - 1);
    id->name[PI_SECURITY_IDENTITY_MAX_LEN - 1] = 0;
    id->type = type;
    id->id = sec->next_identity_id;
    id->enabled = 1;
    pi_timestamp_now(&id->created);
    *out_id = sec->next_identity_id;
    sec->next_identity_id++;
    sec->identity_count++;
    return 0;
}

int pi_security_delete_identity(pi_security_db_t *sec, int32_t identity_id) {
    if (!sec) return -1;
    int i, idx = -1;
    for (i = 0; i < sec->identity_count; i++)
        if (sec->identities[i].id == identity_id) { idx = i; break; }
    if (idx < 0) return -2;
    if (idx < sec->identity_count - 1)
        memmove(&sec->identities[idx], &sec->identities[idx+1],
                (sec->identity_count - idx - 1) * sizeof(pi_identity_t));
    sec->identity_count--;
    /* Also remove all access mappings for this identity */
    int j = 0;
    for (i = 0; i < sec->mapping_count; i++)
        if (sec->mappings[i].identity_id != identity_id)
            sec->mappings[j++] = sec->mappings[i];
    sec->mapping_count = j;
    return 0;
}

int pi_security_get_identity(const pi_security_db_t *sec, int32_t id, pi_identity_t *out) {
    if (!sec || !out) return -1;
    int i;
    for (i = 0; i < sec->identity_count; i++)
        if (sec->identities[i].id == id) {
            memcpy(out, &sec->identities[i], sizeof(pi_identity_t));
            return 0;
        }
    return -2;
}

int pi_security_find_identity(const pi_security_db_t *sec, const char *name, int32_t *out_id) {
    if (!sec || !name || !out_id) return -1;
    int i;
    for (i = 0; i < sec->identity_count; i++)
        if (strcmp(sec->identities[i].name, name) == 0) {
            *out_id = sec->identities[i].id;
            return 0;
        }
    return -2;
}

int pi_security_grant_access(pi_security_db_t *sec, int32_t identity_id, int32_t point_id, pi_access_level_t level, int32_t grantor) {
    if (!sec) return -1;
    if (sec->mapping_count >= PI_SECURITY_MAX_MAPPINGS) return -2;
    /* Check if mapping already exists - update if so */
    int i;
    for (i = 0; i < sec->mapping_count; i++)
        if (sec->mappings[i].identity_id == identity_id && sec->mappings[i].point_id == point_id) {
            sec->mappings[i].access = level;
            sec->mappings[i].granted_by = grantor;
            pi_timestamp_now(&sec->mappings[i].granted);
            sec->access_grants++;
            return 0;
        }
    pi_access_mapping_t *m = &sec->mappings[sec->mapping_count];
    memset(m, 0, sizeof(*m));
    m->identity_id = identity_id; m->point_id = point_id;
    m->access = level; m->granted_by = grantor;
    pi_timestamp_now(&m->granted);
    sec->mapping_count++;
    sec->access_grants++;
    return 0;
}

int pi_security_revoke_access(pi_security_db_t *sec, int32_t identity_id, int32_t point_id) {
    if (!sec) return -1;
    int i, idx = -1;
    for (i = 0; i < sec->mapping_count; i++)
        if (sec->mappings[i].identity_id == identity_id && sec->mappings[i].point_id == point_id) { idx = i; break; }
    if (idx < 0) return -2;
    if (idx < sec->mapping_count - 1)
        memmove(&sec->mappings[idx], &sec->mappings[idx+1],
                (sec->mapping_count - idx - 1) * sizeof(pi_access_mapping_t));
    sec->mapping_count--;
    return 0;
}

pi_access_level_t pi_security_check_access(const pi_security_db_t *sec, int32_t identity_id, int32_t point_id) {
    if (!sec) return PI_ACCESS_NONE;
    /* Check if identity is enabled */
    int found = 0, i;
    for (i = 0; i < sec->identity_count; i++)
        if (sec->identities[i].id == identity_id) { found = sec->identities[i].enabled; break; }
    if (!found) return PI_ACCESS_NONE;
    /* Check access mappings */
    pi_access_level_t max_level = PI_ACCESS_NONE;
    for (i = 0; i < sec->mapping_count; i++)
        if (sec->mappings[i].identity_id == identity_id && sec->mappings[i].point_id == point_id)
            if (sec->mappings[i].access > max_level) max_level = sec->mappings[i].access;
    /* Note: access_denials not updated in const context */
    return max_level;
}

int pi_security_has_access(const pi_security_db_t *sec, int32_t identity_id, int32_t point_id, pi_access_level_t required) {
    pi_access_level_t granted = pi_security_check_access(sec, identity_id, point_id);
    return granted >= required ? 1 : 0;
}

int pi_security_enable_identity(pi_security_db_t *sec, int32_t identity_id, int enabled) {
    if (!sec) return -1;
    int i;
    for (i = 0; i < sec->identity_count; i++)
        if (sec->identities[i].id == identity_id) {
            sec->identities[i].enabled = enabled ? 1 : 0;
            if (enabled) pi_timestamp_now(&sec->identities[i].last_login);
            return 0;
        }
    return -2;
}

int pi_security_is_enabled(const pi_security_db_t *sec, int32_t identity_id) {
    if (!sec) return 0;
    int i;
    for (i = 0; i < sec->identity_count; i++)
        if (sec->identities[i].id == identity_id) return sec->identities[i].enabled;
    return 0;
}

int64_t pi_security_identity_count(const pi_security_db_t *sec) {
    return sec ? sec->identity_count : 0;
}

int64_t pi_security_mapping_count(const pi_security_db_t *sec) {
    return sec ? sec->mapping_count : 0;
}

double pi_security_denial_rate(const pi_security_db_t *sec) {
    if (!sec) return 0.0;
    int64_t total = sec->access_grants + sec->access_denials;
    return total > 0 ? (double)sec->access_denials / (double)total : 0.0;
}

/* ─── Role-Based Access ──────────────────────────────────────────── */
int pi_security_create_role(pi_security_db_t *sec, const char *role_name) {
    if (!sec || !role_name) return -1;
    int32_t role_id;
    return pi_security_create_identity(sec, role_name, PI_IDENTITY_TYPE_ROLE, &role_id);
}

int pi_security_assign_role(pi_security_db_t *sec, int32_t user_id, int32_t role_id, int32_t point_id, pi_access_level_t level, int32_t grantor) {
    if (!sec) return -1;
    /* Grant access to the role for the point */
    int rc = pi_security_grant_access(sec, role_id, point_id, level, grantor);
    if (rc != 0) return rc;
    /* In a real system, users would inherit from roles */
    (void)user_id;
    return 0;
}
