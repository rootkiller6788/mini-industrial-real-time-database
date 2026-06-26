/* example_mqtt_streaming.c — PI to MQTT streaming demo (L6 Canonical + L7 App + L8 Sparkplug B) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/pi_mqtt_stream.h"
int main(void) {
    printf("=== PI to MQTT Streaming Demo ===\n");
    pi_mqtt_config_t cfg; pi_mqtt_config_init(&cfg);
    cfg.broker_host = "mqtt-broker.plant.local";
    cfg.client_id = "pi-integrator-demo";
    pi_mqtt_client_t *c = pi_mqtt_client_connect(&cfg);
    if (!c) { printf("Connection failed\n"); return 1; }
    printf("MQTT state: %d\n", (int)pi_mqtt_client_get_state(c));
    /* Store-and-forward for reliability — L5 Algorithm */
    pi_mqtt_sf_config_t sf; memset(&sf, 0, sizeof(sf));
    sf.max_entries = 1000; sf.max_total_bytes = 10*1024*1024;
    sf.max_retries = 5; sf.retry_base_delay_ms = 1000;
    sf.backoff_multiplier = 2.0; sf.max_retry_delay_ms = 60000;
    pi_mqtt_store_forward_init(c, &sf);
    /* Batching — L5 Algorithm */
    pi_mqtt_batch_config_t bc; memset(&bc, 0, sizeof(bc));
    bc.enable_batching = true; bc.max_batch_size = 50; bc.batch_window_ms = 1000;
    pi_mqtt_configure_batching(c, &bc);
    /* Stream PI data points — L6 Canonical Problem */
    const char *tags[] = {"SINUSOID","TEMPERATURE","PRESSURE","FLOW_RATE"};
    for (int i = 0; i < 4; i++) {
        pi_data_point_t *dp = pi_data_point_new(tags[i], 20.0+i*10.0, 1712345678+i*60, PI_QUALITY_GOOD);
        char topic[256]; snprintf(topic, sizeof(topic), "plant/area1/%s", tags[i]);
        pi_mqtt_map_pi_tag(c, tags[i], topic, PI_MQTT_QOS_1, 0.5, 1.0, 500.0, "json");
        uint16_t pid = pi_mqtt_publish_pi_data_point(c, topic, dp, PI_MQTT_QOS_1, true);
        printf("Published %s to %s (pid=%u)\n", tags[i], topic, (unsigned)pid);
        pi_data_point_free(dp);
    }
    /* Sparkplug B — L8 Advanced: IT/OT convergence */
    pi_data_point_t *dp = pi_data_point_new("SINUSOID", 42.5, 1712345678, PI_QUALITY_GOOD);
    size_t sl = 0;
    uint8_t *sp = pi_mqtt_serialize_sparkplug_b("PlantGroup", "EdgeNode001", dp, &sl);
    if (sp) {
        printf("Sparkplug B message: %zu bytes\n", sl);
        pi_mqtt_publish(c, "spBv1.0/PlantGroup/DBIRTH/EdgeNode001", sp, sl, PI_MQTT_QOS_1, false);
        free(sp);
    }
    /* Deserialize Sparkplug B — verify roundtrip */
    uint8_t *sp2 = pi_mqtt_serialize_sparkplug_b("G", "E1", dp, &sl);
    if (sp2) {
        pi_data_point_t *dps_out = NULL; size_t cnt = 0;
        int r = pi_mqtt_deserialize_sparkplug_b(sp2, sl, &dps_out, &cnt);
        if (r == 0) printf("Sparkplug B deserialized: %zu points\n", cnt);
        if (dps_out) { free(dps_out->tag_name); free(dps_out); }
        free(sp2);
    }
    pi_data_point_free(dp);
    /* Drain store-and-forward */
    int drained = pi_mqtt_store_forward_drain(c);
    printf("Store-forward drained: %d messages\n", drained);
    /* Stats */
    pi_mqtt_stream_stats_t s; pi_mqtt_get_stats(c, &s);
    printf("Stats: %llu published, %llu bytes sent\n",
           (unsigned long long)s.total_published, (unsigned long long)s.total_bytes_sent);
    pi_mqtt_client_disconnect(c);
    printf("=== Demo complete ===\n");
    return 0;
}
