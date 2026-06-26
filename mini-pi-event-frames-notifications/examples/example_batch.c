/**
 * @file example_batch.c
 * @brief Batch process event capture using hierarchical Event Frames
 *
 * Demonstrates ISA-88 / ISA-106 batch process event modeling:
 * a ProductionRun parent event containing multiple Batch child events,
 * each with their own unit operation events. Shows full hierarchy,
 * attribute propagation, and batch reporting.
 *
 * L6 Canonical Problem: Batch production event hierarchy (ISA-88)
 * L7 Application: Rockwell FactoryTalk Batch / Siemens SIMATIC BATCH
 *
 * Reference: ISA-88 "Batch Control" Part 1-4
 *            ISA-106 "Procedure Automation for Continuous Process Operations"
 */

#include "event_frame.h"
#include "event_template.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>

static void print_event_tree(const event_frame_t *ef, int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
    char buf[256];
    ef_summary(ef, buf, sizeof(buf));
    printf("|- %s\n", buf);

    for (int i = 0; i < ef->child_count; i++) {
        print_event_tree(ef->children[i], depth + 1);
    }
}

int main(void) {
    printf("===============================================================\n");
    printf("  Batch Process Event Hierarchy - PI Event Frames\n");
    printf("  Scenario: Pharmaceutical API Production Batch PR-2026-0642\n");
    printf("  ISA-88 Procedural Model: Procedure -> Unit Procedure -> Operation\n");
    printf("===============================================================\n\n");

    /* ── Define Templates ── */
    printf("Step 1: Define batch event templates\n");

    /* Production Run template */
    ef_template_t tmpl_run;
    ef_tmpl_init(&tmpl_run, "ProductionRun",
                 "Top-level production run capturing entire batch campaign");

    ef_template_attr_def_t attr;
    memset(&attr, 0, sizeof(attr));
    strcpy(attr.name, "ProductCode");
    attr.type = EF_ATTR_STRING;
    attr.required = 1;
    ef_tmpl_add_attr_def(&tmpl_run, &attr);

    memset(&attr, 0, sizeof(attr));
    strcpy(attr.name, "TargetYield_kg");
    attr.type = EF_ATTR_FLOAT64;
    attr.required = 1;
    ef_tmpl_add_attr_def(&tmpl_run, &attr);

    memset(&attr, 0, sizeof(attr));
    strcpy(attr.name, "BatchID");
    attr.type = EF_ATTR_STRING;
    attr.required = 1;
    ef_tmpl_add_attr_def(&tmpl_run, &attr);

    tmpl_run.default_severity = EF_SEVERITY_INFO;

    /* Unit Operation template */
    ef_template_t tmpl_unit;
    ef_tmpl_init(&tmpl_unit, "UnitOperation",
                 "Single unit operation within a batch");

    memset(&attr, 0, sizeof(attr));
    strcpy(attr.name, "UnitName");
    attr.type = EF_ATTR_STRING;
    attr.required = 1;
    ef_tmpl_add_attr_def(&tmpl_unit, &attr);

    memset(&attr, 0, sizeof(attr));
    strcpy(attr.name, "OperationType");
    attr.type = EF_ATTR_STRING;
    attr.required = 1;
    ef_tmpl_add_attr_def(&tmpl_unit, &attr);

    memset(&attr, 0, sizeof(attr));
    strcpy(attr.name, "SetpointTemp_C");
    attr.type = EF_ATTR_FLOAT64;
    attr.required = 0;
    ef_tmpl_add_attr_def(&tmpl_unit, &attr);

    printf("  Created templates: '%s', '%s'\n", tmpl_run.name, tmpl_unit.name);

    /* ── Register templates ── */
    ef_template_registry_t registry;
    ef_tmpl_registry_init(&registry);
    ef_tmpl_registry_register(&registry, &tmpl_run);
    ef_tmpl_registry_register(&registry, &tmpl_unit);
    printf("  Registered %d templates\n", 2);

    /* ── Step 2: Create Production Run (root event) ── */
    printf("\nStep 2: Create Production Run event (root)\n");

    event_frame_t run_event;
    ef_tmpl_instantiate(&tmpl_run, &run_event, "PR-2026-0642 API Synthesis");

    const char *product = "API-PHARMA-XR7";
    ef_set_attribute(&run_event, "ProductCode", EF_ATTR_STRING, product);

    const char *batch = "PR-2026-0642";
    ef_set_attribute(&run_event, "BatchID", EF_ATTR_STRING, batch);

    double target = 250.0;
    ef_set_attribute(&run_event, "TargetYield_kg", EF_ATTR_FLOAT64, &target);

    ef_start(&run_event);

    printf("  Production Run started: %s", ctime(&run_event.start_time));

    /* ── Step 3: Create Unit Operation child events ── */
    printf("\nStep 3: Create Unit Operation events (children)\n");

    /* Reactor charging */
    event_frame_t op_charge;
    ef_tmpl_instantiate(&tmpl_unit, &op_charge, "Reactor R-101 Charge");
    ef_set_attribute(&op_charge, "UnitName", EF_ATTR_STRING, "R-101");
    ef_set_attribute(&op_charge, "OperationType", EF_ATTR_STRING, "Charge");
    double tmp1 = 25.0;
    ef_set_attribute(&op_charge, "SetpointTemp_C", EF_ATTR_FLOAT64, &tmp1);
    ef_start(&op_charge);
    /* Simulate charge duration */
    op_charge.start_time = run_event.start_time + 60;
    ef_add_child(&run_event, &op_charge);

    /* Reaction */
    event_frame_t op_react;
    ef_tmpl_instantiate(&tmpl_unit, &op_react, "Reactor R-101 Reaction");
    ef_set_attribute(&op_react, "UnitName", EF_ATTR_STRING, "R-101");
    ef_set_attribute(&op_react, "OperationType", EF_ATTR_STRING, "Reaction");
    double tmp2 = 85.0;
    ef_set_attribute(&op_react, "SetpointTemp_C", EF_ATTR_FLOAT64, &tmp2);
    ef_start(&op_react);
    op_react.start_time = run_event.start_time + 1800;
    ef_add_child(&run_event, &op_react);

    /* Crystallization */
    event_frame_t op_crystal;
    ef_tmpl_instantiate(&tmpl_unit, &op_crystal, "Crystallizer CR-201");
    ef_set_attribute(&op_crystal, "UnitName", EF_ATTR_STRING, "CR-201");
    ef_set_attribute(&op_crystal, "OperationType", EF_ATTR_STRING, "Crystallization");
    double tmp3 = 5.0;
    ef_set_attribute(&op_crystal, "SetpointTemp_C", EF_ATTR_FLOAT64, &tmp3);
    ef_start(&op_crystal);
    op_crystal.start_time = run_event.start_time + 7200;
    ef_add_child(&run_event, &op_crystal);

    printf("  Created %d child operations under Production Run\n", run_event.child_count);

    /* ── Step 4: Close operations in sequence ── */
    printf("\nStep 4: Complete operations in sequence\n");

    /* Charge complete */
    op_charge.end_time = op_charge.start_time + 1500;
    ef_close(&op_charge);
    printf("  [x] Reactor Charge completed (%.0f min)\n",
           ef_duration_seconds(&op_charge) / 60.0);

    /* Reaction complete */
    op_react.end_time = op_react.start_time + 4800;
    ef_close(&op_react);
    printf("  [x] Reaction completed (%.0f min)\n",
           ef_duration_seconds(&op_react) / 60.0);

    /* Crystallization complete */
    op_crystal.end_time = op_crystal.start_time + 3600;
    ef_close(&op_crystal);
    printf("  [x] Crystallization completed (%.0f min)\n",
           ef_duration_seconds(&op_crystal) / 60.0);

    /* ── Step 5: Close production run ── */
    printf("\nStep 5: Close Production Run\n");

    /* Update actual yield */
    double actual = 238.7;
    ef_set_attribute(&run_event, "TargetYield_kg", EF_ATTR_FLOAT64, &actual);

    run_event.end_time = op_crystal.end_time + 300;
    ef_close(&run_event);
    printf("  Production Run completed: %.0f hours total\n",
           ef_duration_seconds(&run_event) / 3600.0);
    printf("  Yield: %.1f / %.1f kg (%.1f%%)\n",
           actual, target, (actual / target) * 100.0);

    /* ── Step 6: Acknowledge all ── */
    printf("\nStep 6: Acknowledge batch completion\n");
    ef_acknowledge(&run_event, "batch_manager", "Batch PR-2026-0642 complete");
    printf("  Batch acknowledged by batch_manager\n");

    /* ── Step 7: Event hierarchy display ── */
    printf("\n===============================================================\n");
    printf("  Batch Event Hierarchy (ISA-88 Procedure Model)\n");
    printf("===============================================================\n");
    print_event_tree(&run_event, 0);

    printf("===============================================================\n");
    printf("  Example Complete - Batch Events Captured\n");
    printf("===============================================================\n");

    return 0;
}
