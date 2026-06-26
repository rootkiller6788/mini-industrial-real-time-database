/**
 * pi_point_db.c - PI Point Database (PIPOINT) Implementation
 * CRUD operations, tag indexing, digital state management, validation.
 * Knowledge: L1-L6  Stanford ENGR205  Purdue ME 575  RWTH Aachen
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/pi_point_db.h"

void pi_point_db_init(pi_point_db_t *db) {
    if (!db) return;
    memset(db, 0, sizeof(*db));
    db->next_point_id = 1;
}

void pi_point_db_destroy(pi_point_db_t *db) {
    if (!db) return;
    memset(db, 0, sizeof(*db));
}

static int find_slot(const pi_point_db_t *db, int32_t point_id) {
    int i;
    for (i = 0; i < db->point_count; i++)
        if (db->points[i].point_id == point_id) return i;
    return -1;
}

int pi_point_db_create(pi_point_db_t *db, const pi_point_attributes_t *attrs, int32_t *out_id) {
    if (!db || !attrs || !out_id) return -1;
    if (db->point_count >= PI_POINT_DB_MAX_POINTS) return -2;
    if (pi_point_db_get_by_tag(db, attrs->tag) != NULL) return -3;
    if (pi_point_db_validate_attrs(attrs) != 0) return -4;
    memcpy(&db->points[db->point_count], attrs, sizeof(pi_point_attributes_t));
    db->points[db->point_count].point_id = db->next_point_id;
    *out_id = db->next_point_id;
    db->next_point_id++;
    db->point_count++;
    db->point_creates++;
    pi_timestamp_now(&db->last_modified);
    return 0;
}

int pi_point_db_delete(pi_point_db_t *db, int32_t point_id) {
    if (!db) return -1;
    int idx = find_slot(db, point_id);
    if (idx < 0) return -2;
    if (idx < db->point_count - 1)
        memcpy(&db->points[idx], &db->points[db->point_count - 1], sizeof(pi_point_attributes_t));
    db->point_count--;
    db->point_deletes++;
    pi_timestamp_now(&db->last_modified);
    return 0;
}

int pi_point_db_update(pi_point_db_t *db, int32_t point_id, const pi_point_attributes_t *new_attrs) {
    if (!db || !new_attrs) return -1;
    int idx = find_slot(db, point_id);
    if (idx < 0) return -2;
    if (pi_point_db_validate_attrs(new_attrs) != 0) return -3;
    int32_t old_id = db->points[idx].point_id;
    memcpy(&db->points[idx], new_attrs, sizeof(pi_point_attributes_t));
    db->points[idx].point_id = old_id;
    db->attribute_edits++;
    pi_timestamp_now(&db->last_modified);
    return 0;
}

const pi_point_attributes_t* pi_point_db_get_by_id(const pi_point_db_t *db, int32_t point_id) {
    if (!db) return NULL;
    int idx = find_slot(db, point_id);
    return idx >= 0 ? &db->points[idx] : NULL;
}

const pi_point_attributes_t* pi_point_db_get_by_tag(const pi_point_db_t *db, const char *tag) {
    if (!db || !tag) return NULL;
    int i;
    for (i = 0; i < db->point_count; i++)
        if (strncmp(db->points[i].tag, tag, PI_MAX_TAG_LEN) == 0)
            return &db->points[i];
    return NULL;
}

int32_t pi_point_db_get_id_by_tag(const pi_point_db_t *db, const char *tag) {
    const pi_point_attributes_t *p = pi_point_db_get_by_tag(db, tag);
    return p ? p->point_id : -1;
}

void pi_point_db_rebuild_tag_index(pi_point_db_t *db) {
    if (!db) return;
    int i;
    db->tag_index_count = 0;
    for (i = 0; i < db->point_count; i++)
        db->tag_index[db->tag_index_count++] = i;
    /* Simple insertion sort by tag */
    for (i = 1; i < db->tag_index_count; i++) {
        int j = i;
        while (j > 0 && strcmp(db->points[db->tag_index[j-1]].tag,
                                 db->points[db->tag_index[j]].tag) > 0) {
            int32_t tmp = db->tag_index[j];
            db->tag_index[j] = db->tag_index[j-1];
            db->tag_index[j-1] = tmp;
            j--;
        }
    }
}

int pi_point_db_find_by_location(const pi_point_db_t *db, const char *loc, int32_t *ids, int max) {
    if (!db || !loc || !ids || max <= 0) return 0;
    int i, count = 0;
    for (i = 0; i < db->point_count && count < max; i++) {
        if (strstr(db->points[i].location1, loc) ||
            strstr(db->points[i].location2, loc) ||
            strstr(db->points[i].location3, loc) ||
            strstr(db->points[i].location4, loc) ||
            strstr(db->points[i].location5, loc)) {
            ids[count++] = db->points[i].point_id;
        }
    }
    return count;
}

int pi_point_db_find_by_source(const pi_point_db_t *db, const char *ps, int32_t *ids, int max) {
    if (!db || !ps || !ids || max <= 0) return 0;
    int i, count = 0;
    for (i = 0; i < db->point_count && count < max; i++)
        if (strncmp(db->points[i].point_source, ps, PI_MAX_POINTSOURCE_LEN) == 0)
            ids[count++] = db->points[i].point_id;
    return count;
}

int pi_point_db_list_all(const pi_point_db_t *db, int32_t *ids, int max) {
    if (!db || !ids || max <= 0) return 0;
    int i, count = 0;
    for (i = 0; i < db->point_count && count < max; i++)
        ids[count++] = db->points[i].point_id;
    return count;
}

int pi_point_db_add_digital_set(pi_point_db_t *db, const pi_digital_state_set_t *set) {
    if (!db || !set) return -1;
    if (db->digital_set_count >= PI_POINT_DB_MAX_DIGITAL_SETS) return -1;
    memcpy(&db->digital_sets[db->digital_set_count], set, sizeof(pi_digital_state_set_t));
    db->digital_set_count++;
    return 0;
}

const pi_digital_state_set_t* pi_point_db_get_digital_set(const pi_point_db_t *db, const char *name) {
    if (!db || !name) return NULL;
    int i;
    for (i = 0; i < db->digital_set_count; i++)
        if (strcmp(db->digital_sets[i].set_name, name) == 0)
            return &db->digital_sets[i];
    return NULL;
}

int pi_point_db_find_digital_state(const pi_point_db_t *db, const char *set_name, int32_t code, char *out, int nmax) {
    if (!db || !set_name || !out) return -1;
    const pi_digital_state_set_t *s = pi_point_db_get_digital_set(db, set_name);
    if (!s) return -2;
    int i;
    for (i = 0; i < s->num_states; i++)
        if (s->states[i].code == code) {
            strncpy(out, s->states[i].name, nmax - 1);
            out[nmax - 1] = 0;
            return 0;
        }
    return -3;
}

int pi_point_db_validate_attrs(const pi_point_attributes_t *attrs) {
    if (!attrs) return -1;
    if (!pi_point_db_validate_tag(attrs->tag)) return -2;
    if (attrs->point_type < 0 || attrs->point_type > PI_POINT_BLOB) return -3;
    if (attrs->span <= 0.0) return -4;
    if (attrs->exc_dev < 0.0 || attrs->comp_dev < 0.0) return -5;
    if (attrs->comp_min > attrs->comp_max && attrs->comp_max > 0) return -6;
    return 0;
}

int pi_point_db_validate_tag(const char *tag) {
    if (!tag || tag[0] == 0) return 0;
    int len = (int)strlen(tag);
    if (len >= PI_MAX_TAG_LEN) return 0;
    if (!isalpha((unsigned char)tag[0])) return 0;
    int i;
    for (i = 0; i < len; i++)
        if (!isalnum((unsigned char)tag[i]) && tag[i] != '_' && tag[i] != '.')
            return 0;
    return 1;
}

int32_t pi_point_db_count(const pi_point_db_t *db) {
    return db ? db->point_count : 0;
}

int32_t pi_point_db_next_available_id(const pi_point_db_t *db) {
    return db ? db->next_point_id : 0;
}
