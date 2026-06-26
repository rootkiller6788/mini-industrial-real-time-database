/** pi_point_db.h - PI Point Database Management */
#ifndef PI_POINT_DB_H
#define PI_POINT_DB_H
#include "pi_da_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PI_POINT_DB_MAX_POINTS 1000000
#define PI_POINT_DB_MAX_DIGITAL_SETS 256
#define PI_POINT_DB_MAX_STATES_PER_SET 32
typedef struct { char set_name[32]; int32_t num_states; pi_digital_state_t states[PI_POINT_DB_MAX_STATES_PER_SET]; } pi_digital_state_set_t;
typedef struct { pi_point_attributes_t points[PI_POINT_DB_MAX_POINTS]; int32_t point_count, next_point_id; int32_t tag_index[PI_POINT_DB_MAX_POINTS]; int32_t tag_index_count; pi_digital_state_set_t digital_sets[PI_POINT_DB_MAX_DIGITAL_SETS]; int32_t digital_set_count; int64_t point_creates, point_deletes, attribute_edits; pi_timestamp_t last_modified; } pi_point_db_t;
void pi_point_db_init(pi_point_db_t *db);
void pi_point_db_destroy(pi_point_db_t *db);
int pi_point_db_create(pi_point_db_t *db, const pi_point_attributes_t *attrs, int32_t *out_id);
int pi_point_db_delete(pi_point_db_t *db, int32_t point_id);
int pi_point_db_update(pi_point_db_t *db, int32_t point_id, const pi_point_attributes_t *attrs);
const pi_point_attributes_t* pi_point_db_get_by_id(const pi_point_db_t *db, int32_t point_id);
const pi_point_attributes_t* pi_point_db_get_by_tag(const pi_point_db_t *db, const char *tag);
int32_t pi_point_db_get_id_by_tag(const pi_point_db_t *db, const char *tag);
void pi_point_db_rebuild_tag_index(pi_point_db_t *db);
int pi_point_db_find_by_location(const pi_point_db_t *db, const char *loc, int32_t *ids, int max);
int pi_point_db_find_by_source(const pi_point_db_t *db, const char *src, int32_t *ids, int max);
int pi_point_db_list_all(const pi_point_db_t *db, int32_t *ids, int max);
int pi_point_db_add_digital_set(pi_point_db_t *db, const pi_digital_state_set_t *set);
const pi_digital_state_set_t* pi_point_db_get_digital_set(const pi_point_db_t *db, const char *name);
int pi_point_db_find_digital_state(const pi_point_db_t *db, const char *set, int32_t code, char *out, int nmax);
int pi_point_db_validate_attrs(const pi_point_attributes_t *attrs);
int pi_point_db_validate_tag(const char *tag);
int32_t pi_point_db_count(const pi_point_db_t *db);
int32_t pi_point_db_next_available_id(const pi_point_db_t *db);
#ifdef __cplusplus
}
#endif
#endif
