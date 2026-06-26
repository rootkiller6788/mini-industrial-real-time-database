/** example_snapshot_archive.c - Demonstrate snapshot and archive pipeline */
#include <stdio.h>
#include <string.h>
#include "../include/pi_da_types.h"
#include "../include/pi_snapshot.h"
#include "../include/pi_archive.h"

int main(void) {
    printf("=== PI Snapshot & Archive Demo ===

");
    pi_snapshot_t snap; pi_snapshot_init(&snap);
    pi_archive_t arch; pi_archive_init(&arch);

    /* Feed data points into the pipeline */
    pi_value_t val; pi_timestamp_t ts;
    int i;
    for (i = 0; i < 100; i++) {
        pi_timestamp_now(&ts);
        pi_value_set_float64(&val, 50.0 + 30.0 * sin((double)i * 0.1), ts);
        pi_snapshot_put(&snap, 1, &val);
        if (pi_snapshot_exception_test(&snap, 1, &val, 2.0)) {
            pi_archive_event_t ev; memset(&ev, 0, sizeof(ev));
            ev.timestamp = ts;
            memcpy(&ev.value, &val, sizeof(pi_value_t));
            pi_archive_store_event(&arch, &ev);
        }
    }

    printf("Snapshot entries: %d
", pi_snapshot_size(&snap));
    printf("Archive events: %lld
", (long long)pi_archive_total_events(&arch));
    printf("Exception rate: %.1f%%
", 100.0 * pi_snapshot_exception_rate(&snap));

    const pi_snapshot_entry_t *e = pi_snapshot_get(&snap, 1);
    if (e) printf("Current value: %.3f
", pi_value_get_float64(&e->current_value));

    pi_snapshot_destroy(&snap);
    pi_archive_destroy(&arch);
    printf("
Done.
");
    return 0;
}
