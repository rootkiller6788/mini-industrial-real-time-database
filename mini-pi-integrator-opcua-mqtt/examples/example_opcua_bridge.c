/* example_opcua_bridge.c — PI to OPC UA bridge demo (L6 Canonical Problem + L7 Application) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/pi_opcua_bridge.h"
int main(void) {
    printf("=== PI to OPC UA Bridge Demo ===\n");
    pi_opcua_session_config_t cfg; pi_opcua_session_config_init(&cfg);
    cfg.endpoint_url = "opc.tcp://192.168.1.100:4840";
    cfg.application_name = "pi-integrator-demo";
    pi_opcua_session_t *sess = pi_opcua_session_connect(&cfg);
    if (!sess) { printf("Connection failed\n"); return 1; }
    printf("Session state: %d\n", (int)pi_opcua_session_get_state(sess));
    pi_data_point_t *dp = pi_data_point_new("SINUSOID", 42.5, 1712345678, PI_QUALITY_GOOD);
    printf("PI Point: %s = %.2f @ %lld\n", dp->tag_name, dp->value, (long long)dp->timestamp);
    pi_opcua_node_id_t *node = pi_opcua_node_id_new_string(2, "SINUSOID");
    pi_opcua_pi_mapping_t *map = pi_opcua_pi_map_tag(sess, "SINUSOID", node, 1000, 0.1, true);
    if (map) {
        printf("Mapping established\n");
        pi_bridge_result_t r = pi_opcua_pi_write_to_opcua(sess, map, dp);
        printf("Write result: %d\n", (int)r);
        pi_data_point_t rb; memset(&rb, 0, sizeof(rb));
        r = pi_opcua_pi_read_from_opcua(sess, map, &rb);
        printf("Read back: value=%.2f, quality=%d\n", rb.value, (int)rb.quality);
        free(rb.tag_name);
        pi_opcua_pi_mapping_free(map);
    }
    /* Historical data access — L4 IEC 62541-11 HA profile */
    pi_opcua_data_value_t *hist = NULL;
    int64_t t0 = 1712345678 - 3600, t1 = 1712345678;
    int n = pi_opcua_history_read_raw(sess, node, t0, t1, 10, &hist);
    printf("Historical values: %d\n", n);
    for (int i = 0; i < n; i++) pi_opcua_data_value_clear(&hist[i]);
    free(hist);
    /* Processed history — L5 aggregate algorithm */
    pi_opcua_data_value_t *proc = NULL;
    int np = pi_opcua_history_read_processed(sess, node, t0, t1, 60000.0, 1, &proc);
    printf("Processed (avg) intervals: %d\n", np);
    for (int i = 0; i < np; i++) pi_opcua_data_value_clear(&proc[i]);
    free(proc);
    pi_opcua_node_id_free(node); pi_data_point_free(dp);
    pi_opcua_session_disconnect(sess);
    printf("=== Demo complete ===\n");
    return 0;
}
