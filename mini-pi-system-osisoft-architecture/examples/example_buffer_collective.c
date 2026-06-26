/** example_buffer_collective.c - Buffer and Collective HA demo */
#include <stdio.h>
#include <string.h>
#include "../include/pi_da_types.h"
#include "../include/pi_buffer.h"
#include "../include/pi_collective.h"

int main(void) {
    printf("=== PI Buffer & Collective Demo ===

");
    pi_buffer_t buf; pi_buffer_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_events = 1000;
    pi_buffer_init(&buf, &cfg);

    /* Enqueue values */
    pi_value_t v; pi_timestamp_t ts; int i;
    for (i = 0; i < 10; i++) {
        pi_timestamp_now(&ts);
        pi_value_set_float64(&v, (double)i * 10.0, ts);
        pi_buffer_enqueue(&buf, 1, &v, 1);
    }
    printf("Buffered: %d events
", pi_buffer_queue_size(&buf));

    /* Simulate disconnect */
    pi_buffer_set_disconnected(&buf);
    printf("Connected: %s
", pi_buffer_is_connected(&buf) ? "yes" : "no");

    /* Reconnect and flush */
    pi_buffer_set_connected(&buf);
    int sent = pi_buffer_flush(&buf, 100);
    printf("Flushed: %d events
", sent);

    /* Collective setup */
    pi_collective_t col; pi_collective_init(&col, 0);
    pi_collective_add_member(&col, "pi-primary", 5450, 10);
    pi_collective_add_member(&col, "pi-backup", 5450, 5);
    pi_collective_mark_online(&col, 0, PI_COLLECTIVE_PRIMARY);
    pi_collective_mark_online(&col, 1, PI_COLLECTIVE_SECONDARY);
    pi_collective_elect_primary(&col);
    printf("Primary: %d  Members: %d
",
           pi_collective_get_primary(&col), pi_collective_member_count(&col));

    pi_buffer_destroy(&buf);
    pi_collective_destroy(&col);
    printf("
Done.
");
    return 0;
}
