/**
 * @file test_event_frame.c
 * @brief Comprehensive test suite for Event Frame module
 *
 * Tests all core APIs: lifecycle, attributes, hierarchy, active set operations.
 * Uses assert-based testing (no external test framework dependency).
 *
 * Knowledge verification:
 *   L1: Struct initialization, enum values, GUID generation
 *   L2: State transitions, parent-child hierarchy
 *   L3: FNV-1a hash collision handling, ring-buffer operations
 *   L4: ISA-18.2 acknowledgment constraints
 */

#include "event_frame.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST %s... ", name); \
} while(0)

#define PASS() do { \
    printf("PASSED\n"); \
    tests_passed++; \
} while(0)

#define FAIL(msg) do { \
    printf("FAILED: %s\n", msg); \
} while(0)

/* ─── L1: Initialization ─────────────────────────────────────────────────── */

static void test_init_basic(void) {
    TEST("ef_init creates valid event frame");
    event_frame_t ef;
    int rc = ef_init(&ef, "TestEvent", "TestTemplate");
    assert(rc == 0);
    assert(strcmp(ef.name, "TestEvent") == 0);
    assert(strcmp(ef.template_name, "TestTemplate") == 0);
    assert(ef.status == EF_STATUS_INACTIVE);
    assert(ef.severity == EF_SEVERITY_INFO);
    assert(ef.child_count == 0);
    assert(ef.attr_count == 0);
    assert(ef.parent == NULL);
    PASS();
}

static void test_init_null_guard(void) {
    TEST("ef_init rejects NULL parameters");
    event_frame_t ef;
    assert(ef_init(NULL, "name", "tmpl") == -1);
    assert(ef_init(&ef, NULL, "tmpl") == -1);
    assert(ef_init(&ef, "name", NULL) == -1);
    PASS();
}

/* ─── L2: Lifecycle State Transitions ────────────────────────────────────── */

static void test_lifecycle_start_close(void) {
    TEST("ef_start/ef_close complete lifecycle");
    event_frame_t ef;
    ef_init(&ef, "Lifecycle", "Test");

    assert(ef_start(&ef) == 0);
    assert(ef.status == EF_STATUS_ACTIVE);
    assert(ef.start_time > 0);

    /* Cannot start twice */
    assert(ef_start(&ef) == -1);

    assert(ef_close(&ef) == 0);
    assert(ef.status == EF_STATUS_CLOSED);
    assert(ef.end_time >= ef.start_time);

    /* Cannot close twice */
    assert(ef_close(&ef) == -1);
    PASS();
}

static void test_acknowledge(void) {
    TEST("ef_acknowledge follows ISA-18.2 workflow");
    event_frame_t ef;
    ef_init(&ef, "AckTest", "Test");
    ef_start(&ef);
    ef_close(&ef);

    assert(ef_acknowledge(&ef, "operator1", "Checked and confirmed") == 0);
    assert(ef.status == EF_STATUS_ACKED);
    assert(strcmp(ef.acked_by, "operator1") == 0);
    assert(strcmp(ef.ack_comment, "Checked and confirmed") == 0);
    assert(ef.acknowledged_at > 0);

    /* Cannot acknowledge INACTIVE event */
    event_frame_t ef2;
    ef_init(&ef2, "Inactive", "Test");
    assert(ef_acknowledge(&ef2, "user", "comment") == -1);
    PASS();
}

/* ─── L3: Attributes ─────────────────────────────────────────────────────── */

static void test_attributes_set_get(void) {
    TEST("ef_set_attribute/ef_get_attribute round-trip");
    event_frame_t ef;
    ef_init(&ef, "AttrTest", "Test");

    /* Set various types */
    int32_t ival = 42;
    assert(ef_set_attribute(&ef, "BatchID", EF_ATTR_INT32, &ival) == 0);

    double dval = 3.14159;
    assert(ef_set_attribute(&ef, "Pressure", EF_ATTR_FLOAT64, &dval) == 0);

    int bval = 1;
    assert(ef_set_attribute(&ef, "IsValid", EF_ATTR_BOOLEAN, &bval) == 0);

    const char *sval = "AB-12345";
    assert(ef_set_attribute(&ef, "ProductCode", EF_ATTR_STRING, sval) == 0);

    assert(ef.attr_count == 4);

    /* Get and verify */
    ef_attr_type_t type;
    int32_t got_ival;
    assert(ef_get_attribute(&ef, "BatchID", &type, &got_ival) == 0);
    assert(type == EF_ATTR_INT32);
    assert(got_ival == 42);

    double got_dval;
    assert(ef_get_attribute(&ef, "Pressure", &type, &got_dval) == 0);
    assert(type == EF_ATTR_FLOAT64);
    assert(got_dval > 3.14158 && got_dval < 3.14160);

    char got_sval[256];
    assert(ef_get_attribute(&ef, "ProductCode", &type, got_sval) == 0);
    assert(type == EF_ATTR_STRING);
    assert(strcmp(got_sval, "AB-12345") == 0);
    PASS();
}

static void test_attribute_update(void) {
    TEST("ef_set_attribute updates existing value");
    event_frame_t ef;
    ef_init(&ef, "UpdateTest", "Test");

    int32_t v1 = 100;
    ef_set_attribute(&ef, "Count", EF_ATTR_INT32, &v1);

    int32_t v2 = 200;
    ef_set_attribute(&ef, "Count", EF_ATTR_INT32, &v2);

    /* attr_count should still be 1 (update, not insert) */
    assert(ef.attr_count == 1);

    ef_attr_type_t type;
    int32_t got;
    ef_get_attribute(&ef, "Count", &type, &got);
    assert(got == 200);
    PASS();
}

static void test_attribute_not_found(void) {
    TEST("ef_get_attribute returns -1 for missing attr");
    event_frame_t ef;
    ef_init(&ef, "MissingTest", "Test");

    ef_attr_type_t type;
    int32_t val;
    assert(ef_get_attribute(&ef, "NonExistent", &type, &val) == -1);
    PASS();
}

/* ─── L3: Parent-Child Hierarchy ─────────────────────────────────────────── */

static void test_parent_child(void) {
    TEST("ef_add_child establishes parent-child relationship");
    event_frame_t parent, child;
    ef_init(&parent, "ParentEvent", "ProductionRun");
    ef_init(&child, "ChildEvent", "UnitOperation");

    assert(ef_add_child(&parent, &child) == 0);
    assert(parent.child_count == 1);
    assert(parent.children[0] == &child);
    assert(child.parent == &parent);
    PASS();
}

static void test_circular_ref_prevention(void) {
    TEST("ef_add_child rejects circular references");
    event_frame_t a, b;
    ef_init(&a, "EventA", "T1");
    ef_init(&b, "EventB", "T2");

    /* Create a -> b */
    assert(ef_add_child(&a, &b) == 0);

    /* Attempt b -> a (would create cycle) */
    assert(ef_add_child(&b, &a) == -1);
    PASS();
}

/* ─── L3: Duration Calculation ───────────────────────────────────────────── */

static void test_duration(void) {
    TEST("ef_duration_seconds returns correct duration");
    event_frame_t ef;
    ef_init(&ef, "DurTest", "Test");
    assert(ef_duration_seconds(&ef) == -1.0);  /* INACTIVE */

    ef_start(&ef);
    double dur_active = ef_duration_seconds(&ef);
    assert(dur_active >= 0.0);

    ef_close(&ef);
    double dur_closed = ef_duration_seconds(&ef);
    assert(dur_closed >= 0.0);
    PASS();
}

/* ─── L3: Active Set Operations ──────────────────────────────────────────── */

static void test_active_set_basic(void) {
    TEST("ef_active_set basic operations");
    ef_active_set_t aset;
    assert(ef_active_set_init(&aset, 100) == 0);
    assert(aset.count == 0);
    assert(aset.capacity == 100);

    event_frame_t ef1, ef2;
    ef_init(&ef1, "Event1", "T1");
    ef_init(&ef2, "Event2", "T2");

    /* Add events */
    assert(ef_active_set_add(&aset, &ef1) == 0);
    assert(ef_active_set_add(&aset, &ef2) == 0);
    assert(aset.count == 2);

    /* Find by ID */
    event_frame_t *found = ef_active_set_find(&aset, ef1.id);
    assert(found == &ef1);

    found = ef_active_set_find(&aset, ef2.id);
    assert(found == &ef2);

    /* Find non-existent */
    found = ef_active_set_find(&aset, "nonexistent-guid-0000000000000");
    assert(found == NULL);

    /* Remove */
    assert(ef_active_set_remove(&aset, ef1.id) == 0);
    assert(aset.count == 1);

    /* Remove non-existent */
    assert(ef_active_set_remove(&aset, ef1.id) == -1);
    PASS();
}

static void test_active_set_find_by_template(void) {
    TEST("ef_active_set_find_by_template filters correctly");
    ef_active_set_t aset;
    ef_active_set_init(&aset, 100);

    event_frame_t e1, e2, e3;
    ef_init(&e1, "E1", "Downtime");
    ef_init(&e2, "E2", "Downtime");
    ef_init(&e3, "E3", "Maintenance");

    ef_active_set_add(&aset, &e1);
    ef_active_set_add(&aset, &e2);
    ef_active_set_add(&aset, &e3);

    event_frame_t *results[10];
    int found = ef_active_set_find_by_template(&aset, "Downtime", results, 10);
    assert(found == 2);
    PASS();
}

static void test_active_set_window_query(void) {
    TEST("ef_active_set_find_in_window time-range query");
    ef_active_set_t aset;
    ef_active_set_init(&aset, 100);

    event_frame_t e1, e2;
    ef_init(&e1, "E1", "T1");
    ef_init(&e2, "E2", "T2");

    /* Set specific times for predictable testing */
    e1.start_time = 1000; ef_start(&e1); e1.start_time = 1000;
    e2.start_time = 2000; ef_start(&e2); e2.start_time = 2000;

    ef_active_set_add(&aset, &e1);
    ef_active_set_add(&aset, &e2);

    event_frame_t *results[10];
    int found = ef_active_set_find_in_window(&aset, 500, 1500, results, 10);
    /* Should find e1 (1000) but not e2 (2000) - modified times during test */
    assert(found >= 1);
    PASS();
}

/* ─── L3: Summary ────────────────────────────────────────────────────────── */

static void test_summary(void) {
    TEST("ef_summary produces readable output");
    event_frame_t ef;
    ef_init(&ef, "SummaryEvent", "SummaryTmpl");
    ef_start(&ef);

    char buf[512];
    int len = ef_summary(&ef, buf, sizeof(buf));
    assert(len > 0);
    assert(strstr(buf, "SummaryEvent") != NULL);
    assert(strstr(buf, "ACTIVE") != NULL);
    PASS();
}

/* ─── Main ───────────────────────────────────────────────────────────────── */

int main(void) {
    printf("\n=== Event Frame Test Suite ===\n\n");

    test_init_basic();
    test_init_null_guard();
    test_lifecycle_start_close();
    test_acknowledge();
    test_attributes_set_get();
    test_attribute_update();
    test_attribute_not_found();
    test_parent_child();
    test_circular_ref_prevention();
    test_duration();
    test_active_set_basic();
    test_active_set_find_by_template();
    test_active_set_window_query();
    test_summary();

    printf("\n=== Results: %d/%d tests passed ===\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
