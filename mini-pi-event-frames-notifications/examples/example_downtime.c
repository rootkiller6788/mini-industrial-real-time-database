/**
 * @file example_downtime.c
 * @brief Complete example: Equipment downtime tracking with Event Frames
 *
 * Demonstrates the full Event Frame lifecycle for a real industrial scenario:
 * tracking equipment downtime on a production line. Shows template creation,
 * event frame instantiation, attribute population, start/close transitions,
 * acknowledgment workflow, and active set management.
 *
 * L6 Canonical Problem: Equipment downtime capture per ISA-106
 * L7 Application: OSIsoft PI / Siemens WinCC downtime tracking
 *
 * Reference: ISA-106 "Procedure Automation for Continuous Process Operations"
 */

#include "event_frame.h"
#include "event_template.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>

/* Simulate retrieving current tag value (PI Data Archive equivalent) */

static void print_event_summary(const event_frame_t *ef) {
    char buf[512];
    ef_summary(ef, buf, sizeof(buf));
    printf("  %s\n", buf);

    printf("  Attributes:\n");
    for (int i = 0; i < ef->attr_count; i++) {
        if (ef->attributes[i].is_set) {
            printf("    %s = ", ef->attributes[i].name);
            switch (ef->attributes[i].type) {
                case EF_ATTR_FLOAT64:
                    printf("%.2f\n", ef->attributes[i].value.float_val);
                    break;
                case EF_ATTR_INT32:
                    printf("%d\n", ef->attributes[i].value.int_val);
                    break;
                case EF_ATTR_STRING:
                    printf("\"%s\"\n", ef->attributes[i].value.str_val);
                    break;
                default:
                    printf("(type %d)\n", ef->attributes[i].type);
            }
        }
    }
    printf("\n");
}

int main(void) {
    printf("============================================================\n");
    printf("  Equipment Downtime Tracking - PI Event Frames\n");
    printf("  Scenario: Compressor K-101 downtime on Process Line A\n");
    printf("============================================================\n\n");

    /* ── Step 1: Define a Downtime Event Frame Template ── */
    printf("Step 1: Define 'EquipmentDowntime' template\n");

    ef_template_t tmpl;
    ef_tmpl_init(&tmpl, "EquipmentDowntime",
                 "Captures equipment downtime events with reason categorization");

    /* Define required attributes */
    ef_template_attr_def_t attr;

    memset(&attr, 0, sizeof(attr));
    strcpy(attr.name, "EquipmentID");
    attr.type = EF_ATTR_STRING;
    attr.required = 1;
    strcpy(attr.description, "Equipment tag identifier");
    ef_tmpl_add_attr_def(&tmpl, &attr);

    memset(&attr, 0, sizeof(attr));
    strcpy(attr.name, "DowntimeReason");
    attr.type = EF_ATTR_STRING;
    attr.required = 1;
    strcpy(attr.description, "Root cause category");
    ef_tmpl_add_attr_def(&tmpl, &attr);

    memset(&attr, 0, sizeof(attr));
    strcpy(attr.name, "ProductionLoss_kg");
    attr.type = EF_ATTR_FLOAT64;
    attr.required = 1;
    strcpy(attr.description, "Estimated production loss");
    ef_tmpl_add_attr_def(&tmpl, &attr);

    memset(&attr, 0, sizeof(attr));
    strcpy(attr.name, "OperatorID");
    attr.type = EF_ATTR_STRING;
    attr.required = 0;
    strcpy(attr.description, "Operator on shift");
    ef_tmpl_add_attr_def(&tmpl, &attr);

    /* Set trigger: threshold trigger on compressor speed = 0 */
    ef_tmpl_set_trigger(&tmpl, EF_TRIGGER_THRESHOLD,
                        "COMPRESSOR_SPEED < 10", 0.0, 5000);
    tmpl.default_severity = EF_SEVERITY_SIGNIFICANT;

    printf("  Template '%s' created with %d attribute definitions\n",
           tmpl.name, tmpl.attr_def_count);

    /* ── Step 2: Register template ── */
    printf("\nStep 2: Register template in registry\n");

    ef_template_registry_t registry;
    ef_tmpl_registry_init(&registry);
    assert(ef_tmpl_registry_register(&registry, &tmpl) == 0);
    printf("  Registry now has %d template(s)\n", registry.count);

    /* ── Step 3: Instantiate downtime event ── */
    printf("\nStep 3: Instantiate downtime event for Compressor K-101\n");

    event_frame_t downtime_event;
    assert(ef_tmpl_instantiate(&tmpl, &downtime_event,
                               "K-101 Downtime - June 2026") == 0);
    printf("  Event frame created: %s\n", downtime_event.id);

    /* ── Step 4: Populate attributes ── */
    printf("\nStep 4: Populate event attributes\n");

    const char *equip_id = "COMP-K101-A";
    ef_set_attribute(&downtime_event, "EquipmentID", EF_ATTR_STRING, equip_id);

    const char *reason = "Mechanical Seal Failure";
    ef_set_attribute(&downtime_event, "DowntimeReason", EF_ATTR_STRING, reason);

    double prod_loss = 1450.5;
    ef_set_attribute(&downtime_event, "ProductionLoss_kg", EF_ATTR_FLOAT64, &prod_loss);

    const char *operator = "J. Smith";
    ef_set_attribute(&downtime_event, "OperatorID", EF_ATTR_STRING, operator);

    printf("  Set %d attributes\n", downtime_event.attr_count);

    /* ── Step 5: Validate template completeness ── */
    printf("\nStep 5: Validate required attributes\n");

    int valid = ef_tmpl_validate(&tmpl, &downtime_event);
    printf("  Template validation: %s\n", valid == 0 ? "PASSED" : "FAILED");

    /* ── Step 6: Start the downtime event ── */
    printf("\nStep 6: Start downtime event (compressor stops)\n");

    assert(ef_start(&downtime_event) == 0);
    printf("  Event started at: %s", ctime(&downtime_event.start_time));
    printf("  Status: ACTIVE\n");

    /* ── Step 7: Add to active set ── */
    printf("\nStep 7: Add to active event set for monitoring\n");

    ef_active_set_t active_set;
    ef_active_set_init(&active_set, 100);
    ef_active_set_add(&active_set, &downtime_event);
    printf("  Active set now has %d event(s)\n", active_set.count);

    /* ── Step 8: Simulate downtime duration ── */
    printf("\nStep 8: Simulate downtime (45 minutes)... [shortened for demo]\n");

    /* In real PI system, this would run while equipment is down.
     * Mock: set start time 45 min ago for demonstration */
    downtime_event.start_time = time(NULL) - 2700;  /* 45 min ago */

    double elapsed = ef_duration_seconds(&downtime_event);
    printf("  Downtime elapsed: %.0f seconds (%.1f minutes)\n",
           elapsed, elapsed / 60.0);

    /* ── Step 9: Close the event (equipment restarts) ── */
    printf("\nStep 9: Close downtime event (compressor restarts)\n");

    assert(ef_close(&downtime_event) == 0);
    double total_dur = ef_duration_seconds(&downtime_event);
    printf("  Total downtime: %.1f minutes\n", total_dur / 60.0);
    printf("  Production loss: %.1f kg\n", prod_loss);

    /* ── Step 10: Acknowledge per ISA-18.2 ── */
    printf("\nStep 10: Acknowledge downtime event\n");

    assert(ef_acknowledge(&downtime_event, "shift_supervisor",
                          "Confirmed - maintenance notified, seal replacement ordered") == 0);
    printf("  Acknowledged by: %s\n", downtime_event.acked_by);
    printf("  Status: ACKED\n");

    /* ── Step 11: Show event summary ── */
    printf("\n============================================================\n");
    printf("  Final Event Summary\n");
    printf("============================================================\n");
    print_event_summary(&downtime_event);

    /* ── Step 12: Active set statistics ── */
    printf("Active Set Statistics:\n");
    int active, closed;
    double avg_dur;
    ef_active_set_stats(&active_set, &active, &closed, &avg_dur);
    printf("  Active: %d, Closed: %d, Avg Duration: %.0fs\n",
           active, closed, avg_dur);

    printf("============================================================\n");
    printf("  Example Complete - Equipment Downtime Tracked\n");
    printf("============================================================\n");

    return 0;
}
