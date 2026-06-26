/** pi_collective.h - PI Collective HA */
#ifndef PI_COLLECTIVE_H
#define PI_COLLECTIVE_H
#include "pi_da_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PI_COLLECTIVE_MAX_MEMBERS 12
#define PI_COLLECTIVE_SYNC_TIMEOUT_MS 30000
typedef enum { PI_COLLECTIVE_PRIMARY=0, PI_COLLECTIVE_SECONDARY=1, PI_COLLECTIVE_SYNCHRONIZING=2, PI_COLLECTIVE_OFFLINE=3 } pi_collective_member_state_t;
typedef struct { char hostname[256]; int32_t port; pi_collective_member_state_t state; int32_t priority; int64_t event_count; pi_timestamp_t last_heartbeat; int32_t lag_events; } pi_collective_member_t;
typedef enum { PI_SYNC_MODE_FULL=0, PI_SYNC_MODE_INCREMENTAL=1, PI_SYNC_MODE_RECONCILE=2 } pi_collective_sync_mode_t;
typedef struct { pi_collective_member_t members[PI_COLLECTIVE_MAX_MEMBERS]; int32_t num_members, primary_index, local_member_index; int32_t sync_active; pi_collective_sync_mode_t sync_mode; int64_t total_synced_events; pi_timestamp_t last_sync_time; } pi_collective_t;
void pi_collective_init(pi_collective_t *col, int32_t local_member_index);
void pi_collective_destroy(pi_collective_t *col);
int pi_collective_add_member(pi_collective_t *col, const char *hostname, int32_t port, int32_t priority);
int pi_collective_remove_member(pi_collective_t *col, int32_t member_index);
int pi_collective_elect_primary(pi_collective_t *col);
int pi_collective_sync_events(pi_collective_t *col, const pi_archive_event_t *events, int n);
int pi_collective_reconcile(pi_collective_t *col, int32_t point_id, pi_timestamp_t since);
int pi_collective_get_primary(const pi_collective_t *col);
int pi_collective_is_primary(const pi_collective_t *col);
const pi_collective_member_t* pi_collective_get_member(const pi_collective_t *col, int32_t idx);
int32_t pi_collective_member_count(const pi_collective_t *col);
int pi_collective_healthy_member_count(const pi_collective_t *col);
void pi_collective_mark_offline(pi_collective_t *col, int32_t idx);
void pi_collective_mark_online(pi_collective_t *col, int32_t idx, pi_collective_member_state_t st);
#ifdef __cplusplus
}
#endif
#endif
