/** pi_audit.c - PI Audit Trail & Change Log */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/pi_da_types.h"

#define PI_AUDIT_MAX_ENTRIES 10000
typedef struct {
    pi_timestamp_t timestamp;
    int32_t user_id;
    char action[64];
    int32_t point_id;
    char detail[256];
} pi_audit_entry_t;
static pi_audit_entry_t g_audit[PI_AUDIT_MAX_ENTRIES];
static int32_t g_audit_count = 0;

void pi_audit_init(void) { g_audit_count = 0; memset(g_audit,0,sizeof(g_audit)); }

int pi_audit_log(int32_t user_id, const char *action, int32_t point_id, const char *detail) {
    if (!action || g_audit_count >= PI_AUDIT_MAX_ENTRIES) return -1;
    pi_audit_entry_t *e = &g_audit[g_audit_count];
    pi_timestamp_now(&e->timestamp);
    e->user_id = user_id;
    strncpy(e->action, action, 63); e->action[63] = 0;
    e->point_id = point_id;
    if (detail) { strncpy(e->detail, detail, 255); e->detail[255] = 0; }
    g_audit_count++;
    return 0;
}

int pi_audit_get_recent(pi_audit_entry_t *entries, int max_entries) {
    if (!entries || max_entries <= 0) return 0;
    int count = g_audit_count < max_entries ? g_audit_count : max_entries;
    int i, start = g_audit_count - count;
    for (i = 0; i < count; i++) memcpy(&entries[i], &g_audit[start + i], sizeof(pi_audit_entry_t));
    return count;
}

int pi_audit_get_by_user(int32_t user_id, pi_audit_entry_t *entries, int max_entries) {
    if (!entries || max_entries <= 0) return 0;
    int i, count = 0;
    for (i = 0; i < g_audit_count && count < max_entries; i++)
        if (g_audit[i].user_id == user_id) memcpy(&entries[count++], &g_audit[i], sizeof(pi_audit_entry_t));
    return count;
}

int pi_audit_get_by_point(int32_t point_id, pi_audit_entry_t *entries, int max_entries) {
    if (!entries || max_entries <= 0) return 0;
    int i, count = 0;
    for (i = 0; i < g_audit_count && count < max_entries; i++)
        if (g_audit[i].point_id == point_id) memcpy(&entries[count++], &g_audit[i], sizeof(pi_audit_entry_t));
    return count;
}

int32_t pi_audit_count(void) { return g_audit_count; }

void pi_audit_print_recent(int n) {
    int count = n < g_audit_count ? n : g_audit_count;
    int i, start = g_audit_count - count;
    printf("=== PI Audit Trail (last %d entries) ===\n", count);
    for (i = start; i < g_audit_count; i++)
        printf("[%s] User:%d %s Point:%d %s\n",
            pi_timestamp_to_iso(&g_audit[i].timestamp),
            g_audit[i].user_id, g_audit[i].action,
            g_audit[i].point_id, g_audit[i].detail);
}

int pi_audit_clear_older_than(double age_seconds) {
    pi_timestamp_t now; pi_timestamp_now(&now);
    int removed = 0, i, j = 0;
    for (i = 0; i < g_audit_count; i++) {
        double age = pi_timestamp_diff_seconds(&g_audit[i].timestamp, &now);
        if (age > age_seconds) { removed++; }
        else { if (j != i) memcpy(&g_audit[j], &g_audit[i], sizeof(pi_audit_entry_t)); j++; }
    }
    g_audit_count = j;
    return removed;
}
