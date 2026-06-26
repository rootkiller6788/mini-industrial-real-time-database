/** pi_security.h - PI Security Model */
#ifndef PI_SECURITY_H
#define PI_SECURITY_H
#include "pi_da_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PI_SECURITY_MAX_IDENTITIES 10000
#define PI_SECURITY_MAX_GROUPS 256
#define PI_SECURITY_MAX_MAPPINGS 50000
#define PI_SECURITY_IDENTITY_MAX_LEN 64
typedef enum { PI_ACCESS_NONE=0, PI_ACCESS_READ=1, PI_ACCESS_WRITE=2, PI_ACCESS_ADMIN=4, PI_ACCESS_DATA_ADMIN=8 } pi_access_level_t;
typedef enum { PI_IDENTITY_TYPE_USER=0, PI_IDENTITY_TYPE_GROUP=1, PI_IDENTITY_TYPE_ROLE=2 } pi_identity_type_t;
typedef struct { char name[PI_SECURITY_IDENTITY_MAX_LEN]; pi_identity_type_t type; int32_t id; int32_t enabled; pi_timestamp_t created; pi_timestamp_t last_login; } pi_identity_t;
typedef struct { int32_t identity_id; int32_t point_id; pi_access_level_t access; pi_timestamp_t granted; int32_t granted_by; } pi_access_mapping_t;
typedef struct { pi_identity_t identities[PI_SECURITY_MAX_IDENTITIES]; int32_t identity_count; pi_access_mapping_t mappings[PI_SECURITY_MAX_MAPPINGS]; int32_t mapping_count; int32_t next_identity_id; int32_t audit_enabled; int64_t access_grants; int64_t access_denials; } pi_security_db_t;
void pi_security_init(pi_security_db_t *sec);
void pi_security_destroy(pi_security_db_t *sec);
int pi_security_create_identity(pi_security_db_t *sec, const char *name, pi_identity_type_t type, int32_t *out_id);
int pi_security_delete_identity(pi_security_db_t *sec, int32_t identity_id);
int pi_security_get_identity(const pi_security_db_t *sec, int32_t id, pi_identity_t *out);
int pi_security_find_identity(const pi_security_db_t *sec, const char *name, int32_t *out_id);
int pi_security_grant_access(pi_security_db_t *sec, int32_t identity_id, int32_t point_id, pi_access_level_t level, int32_t grantor);
int pi_security_revoke_access(pi_security_db_t *sec, int32_t identity_id, int32_t point_id);
pi_access_level_t pi_security_check_access(const pi_security_db_t *sec, int32_t identity_id, int32_t point_id);
int pi_security_has_access(const pi_security_db_t *sec, int32_t identity_id, int32_t point_id, pi_access_level_t required);
int pi_security_enable_identity(pi_security_db_t *sec, int32_t identity_id, int enabled);
int pi_security_is_enabled(const pi_security_db_t *sec, int32_t identity_id);
int64_t pi_security_identity_count(const pi_security_db_t *sec);
int64_t pi_security_mapping_count(const pi_security_db_t *sec);
double pi_security_denial_rate(const pi_security_db_t *sec);
#ifdef __cplusplus
}
#endif
#endif
