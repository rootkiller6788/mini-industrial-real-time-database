/**
 * example_event_frame.c - Event frame monitoring demo
 * L6: Process event tracking with AF Event Frames
 * L7: OSIsoft PI Event Frames for batch monitoring
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../include/af_event_frame.h"
#include "../include/af_element.h"
#include "../include/af_attribute.h"

int main(void)
{
    printf("=== PI AF Event Frame Monitoring ===\n\n");

    /* Create a reactor element with temperature attribute */
    AFElement *reactor = af_element_create("Reactor_R201", AF_ELEM_TYPE_UNIT);
    AFAttribute *temp = af_attribute_create("Temperature", AF_VAL_FLOAT64);
    af_attribute_set_uom(temp, "degC");
    af_value_t normal_temp = { .type = AF_VAL_FLOAT64, .value.v_float64 = 80.0, .is_good = true };
    af_attribute_set_value(temp, &normal_temp);
    af_element_add_attribute(reactor, temp);

    /* Create event frame for over-temperature detection */
    af_event_frame_t *ef = af_ef_create("OverTemp_Reactor_R201");
    af_ef_set_description(ef, "Over-temperature event for Reactor R201");
    af_ef_set_source(ef, reactor);
    af_ef_set_severity(ef, AF_EF_SEVERITY_MAJOR);
    af_ef_set_start_trigger(ef, "Temperature", AF_TRIGGER_GT, 150.0, 5.0);
    af_ef_set_end_trigger(ef, "Temperature", AF_TRIGGER_LT, 130.0, 5.0);

    printf("Event Frame: %s\n", ef->name);
    printf("  Severity: %s\n", af_ef_severity_string(ef->severity));
    printf("  Trigger: Temperature > 150.0 degC\n");
    printf("  End:     Temperature < 130.0 degC\n\n");

    /* Simulate normal operation - no event */
    printf("T=80:  evaluate=%s, active=%s\n",
           af_ef_evaluate(ef) ? "triggered" : "no",
           af_ef_is_active(ef) ? "yes" : "no");

    /* Simulate temperature spike */
    af_value_t high_temp = { .type = AF_VAL_FLOAT64, .value.v_float64 = 160.0, .is_good = true };
    af_attribute_set_value(temp, &high_temp);

    printf("T=160: evaluate=%s, active=%s",
           af_ef_evaluate(ef) ? "triggered" : "no",
           af_ef_is_active(ef) ? "yes" : "no");
    if (af_ef_is_active(ef)) {
        printf(", start_time=%lld", (long long)ef->start_time);
    }
    printf("\n");

    /* Capture attributes during event */
    const char *capture_names[] = {"Temperature"};
    af_ef_capture_attributes(ef, capture_names, 1);
    printf("Captured attributes: %d\n", ef->captured_count);

    /* Temperature returns to normal - event ends */
    af_attribute_set_value(temp, &normal_temp);
    printf("T=80:  evaluate=%s, active=%s",
           af_ef_evaluate(ef) ? "triggered" : "no",
           af_ef_is_active(ef) ? "yes" : "no");
    if (!af_ef_is_active(ef)) {
        printf(", duration=%.1fs", af_ef_get_duration(ef));
    }
    printf("\n");

    /* Acknowledge the event */
    printf("\nStatus: %s\n", af_ef_status_string(ef->status));
    af_ef_acknowledge(ef, "operator1");
    printf("After ack: %s (by %s)\n",
           af_ef_status_string(ef->status), ef->acknowledged_by);

    /* Test event notes */
    af_ef_add_note(ef, "Temperature spike during startup");
    printf("Notes: %s\n", ef->notes);

    af_ef_destroy(ef);
    af_element_destroy(reactor);
    printf("\nEvent frame demo complete.\n");
    return 0;
}
