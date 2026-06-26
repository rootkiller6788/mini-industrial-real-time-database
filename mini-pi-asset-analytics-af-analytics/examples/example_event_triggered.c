/**
 * @file examples/example_event_triggered.c
 * @brief Event-Triggered Analytics — Batch Process Monitoring
 *
 * Demonstrates:
 *   - Event frame template registration for batch operations
 *   - Event frame lifecycle: START → ACTIVE → END → CLOSED → ACKNOWLEDGED
 *   - Trigger evaluation for batch start/end conditions
 *   - Analytics execution at event boundaries
 *   - Active event frame tracking
 *
 * Use Case:
 *   A chemical batch reactor. Event frames capture each batch run.
 *   Start: Reactor temperature > 50°C AND agitator ON
 *   End:   Reactor temperature < 30°C (cooling complete)
 *   Analytics: Calculate batch duration, total energy consumed
 *
 * @see ISA-88 Part 1 — Batch process models
 * @see ISA-95 Part 3 — Production event recording
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pi_af_analytics_eventframe.h"
#include "pi_af_analytics_core.h"

int main(void) {
    printf("=== PI AF Analytics — Batch Event Frame Monitor ===\n\n");

    /* Step 1: Initialize the event frame engine */
    pi_af_ef_context_t ectx;
    pi_af_ef_init(&ectx);

    /* Step 2: Register a "Batch Run" template */
    pi_af_ef_template_t batch_tmpl;
    memset(&batch_tmpl, 0, sizeof(batch_tmpl));
    strcpy(batch_tmpl.name, "Chemical Batch Run");
    strcpy(batch_tmpl.description, "Tracks each production batch from start to finish");
    batch_tmpl.default_severity = PI_AF_EF_SEVERITY_MEDIUM;

    /* Start trigger: temperature crosses above 50°C */
    batch_tmpl.start_triggers[0].trigger_type = PI_AF_EF_TRIGGER_THRESHOLD_UP;
    batch_tmpl.start_triggers[0].threshold = 50.0;
    batch_tmpl.start_triggers[0].hysteresis = 2.0;
    strcpy(batch_tmpl.start_triggers[0].attribute_path, "Reactor.Temperature");
    batch_tmpl.start_trigger_count = 1;

    /* End trigger: temperature crosses below 30°C (cooling done) */
    batch_tmpl.end_triggers[0].trigger_type = PI_AF_EF_TRIGGER_THRESHOLD_DN;
    batch_tmpl.end_triggers[0].threshold = 30.0;
    batch_tmpl.end_triggers[0].hysteresis = 2.0;
    strcpy(batch_tmpl.end_triggers[0].attribute_path, "Reactor.Temperature");
    batch_tmpl.end_trigger_count = 1;

    uint32_t batch_tmpl_id;
    pi_af_ef_register_template(&ectx, &batch_tmpl, &batch_tmpl_id);
    printf("Registered template [%u]: '%s'\n", batch_tmpl_id, batch_tmpl.name);

    /* Step 3: Simulate three batches over time */
    printf("\n--- Simulating 3 Batch Runs ---\n\n");

    for (int batch_num = 1; batch_num <= 3; batch_num++) {
        time_t batch_start = (time_t)(1000 * batch_num);
        time_t batch_end   = batch_start + (time_t)(4800 + 600 * batch_num);

        /* Start the batch (event frame activation) */
        uint32_t ef_id;
        char reason[128];
        snprintf(reason, sizeof(reason), "Batch #%d started — reactor reached 52°C",
                 batch_num);

        pi_af_error_t ret = pi_af_ef_start(&ectx, batch_tmpl_id,
                                             batch_start, reason, &ef_id);
        if (ret != PI_AF_OK) {
            printf("  ERROR starting batch %d: %s\n", batch_num,
                   pi_af_error_string(ret));
            continue;
        }

        pi_af_ef_instance_t *ef = pi_af_ef_get(&ectx, ef_id);
        printf("  [EF-%u] %s\n", ef_id, ef->name);
        printf("         Started:  %s", ctime(&ef->start_time));
        printf("         Reason:   %s\n", ef->start_reason);
        printf("         Severity: %s\n", pi_af_ef_severity_name(ef->severity));

        /* Simulate the batch running */
        double active_dur = pi_af_ef_duration_seconds(ef);
        printf("         Duration so far: %.0f seconds\n", active_dur);

        /* End the batch */
        snprintf(reason, sizeof(reason), "Batch #%d complete — cooled to 28°C",
                 batch_num);
        ret = pi_af_ef_end(&ectx, ef_id, batch_end, reason);
        if (ret != PI_AF_OK) {
            printf("  ERROR ending batch %d: %s\n", batch_num,
                   pi_af_error_string(ret));
            continue;
        }

        ef = pi_af_ef_get(&ectx, ef_id);
        double duration = pi_af_ef_duration_seconds(ef);
        printf("         Ended:    %s", ctime(&ef->end_time));
        printf("         Duration: %.0f seconds (%.1f minutes)\n",
               duration, duration / 60.0);
        printf("         State:    %s\n", pi_af_ef_state_name(ef->state));

        /* Acknowledge the batch */
        char op_name[32];
        snprintf(op_name, sizeof(op_name), "Operator%d", batch_num);
        pi_af_ef_acknowledge(&ectx, ef_id, op_name);

        ef = pi_af_ef_get(&ectx, ef_id);
        printf("         Ack'd by: %s\n", ef->acknowledged_by);
        printf("\n");
    }

    /* Step 4: Statistics */
    printf("--- Event Frame Statistics ---\n");
    printf("  Total Created:       %llu\n",
           (unsigned long long)ectx.total_created);
    printf("  Total Closed:        %llu\n",
           (unsigned long long)ectx.total_closed);
    printf("  Total Acknowledged:  %llu\n",
           (unsigned long long)ectx.total_acknowledged);
    printf("  Total Canceled:      %llu\n",
           (unsigned long long)ectx.total_canceled);
    printf("  Active Event Frames: %u\n",
           pi_af_ef_active_count(&ectx));

    printf("\n=== Event Frame Monitor Complete ===\n");
    return 0;
}
