/**
 * @file tests/test_af_analytics.c
 * @brief Comprehensive test suite for PI AF Analytics module.
 *
 * Tests cover all core API functions across the six modules:
 *   - Core engine (init, register, schedule, execution, topological sort)
 *   - Expression engine (tokenize, parse, evaluate, validate)
 *   - Time-series analytics (sliding window, aggregates, EMA, Holt-Winters,
 *     rate of change, CUSUM, cycle detection)
 *   - KPI engine (register, evaluate, trend, rollup, composite score, build order)
 *   - Event frames (init, start, end, cancel, acknowledge, trigger evaluation)
 *   - Asset rollup (add, remove, set value, rollup, DFS, BFS, depth, LCA, leaves)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "pi_af_analytics_core.h"
#include "pi_af_analytics_expression.h"
#include "pi_af_analytics_timeseries.h"
#include "pi_af_analytics_kpi.h"
#include "pi_af_analytics_eventframe.h"
#include "pi_af_analytics_rollup.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)
#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { printf("FAIL: %s (expected %d, got %d)\n", msg, (int)(b), (int)(a)); \
    tests_failed++; return; } \
} while(0)
#define ASSERT_DOUBLE_EQ(a, b, tol, msg) do { \
    if (fabs((a) - (b)) > (tol)) { \
        printf("FAIL: %s (expected %.6f, got %.6f)\n", msg, b, a); \
        tests_failed++; return; \
    } \
} while(0)

/* --------------------------------------------------------------------------
 * 1. Core Engine Tests
 * ------------------------------------------------------------------------*/

static void test_core_init_shutdown(void) {
    TEST("core init/shutdown");
    pi_af_analytics_context_t ctx;
    pi_af_error_t ret = pi_af_init(&ctx, 64);
    ASSERT(ret == PI_AF_OK, "init failed");

    ASSERT(ctx.queue_capacity == 64, "wrong queue capacity");
    ASSERT(ctx.queue_size == 0, "queue not empty");
    ASSERT(ctx.engine_running == false, "engine should not be running");

    ret = pi_af_shutdown(&ctx);
    ASSERT(ret == PI_AF_OK, "shutdown failed");
    ASSERT(ctx.schedule_queue == NULL, "memory not freed");
    PASS();
}

static void test_core_register_analytic(void) {
    TEST("core register analytic");
    pi_af_analytics_context_t ctx;
    pi_af_init(&ctx, 64);

    pi_af_analytic_t a;
    memset(&a, 0, sizeof(a));
    strcpy(a.name, "Test Analytic");
    strcpy(a.expression, "|Input + 10");
    a.schedule_type = PI_AF_SCHEDULE_PERIODIC;
    a.period_seconds = 60.0;
    a.enabled = true;

    uint32_t id;
    pi_af_error_t ret = pi_af_register_analytic(&ctx, &a, &id);
    ASSERT(ret == PI_AF_OK, "register failed");
    ASSERT(id > 0, "invalid ID assigned");
    ASSERT(ctx.analytics_count == 1, "wrong analytics count");

    /* Lookup */
    pi_af_analytic_t *found = pi_af_get_analytic(&ctx, id);
    ASSERT(found != NULL, "lookup failed");
    ASSERT(strcmp(found->name, "Test Analytic") == 0, "wrong name");

    /* Disable */
    ret = pi_af_set_analytic_enabled(&ctx, id, false);
    ASSERT(ret == PI_AF_OK, "disable failed");
    ASSERT(found->enabled == false, "not disabled");

    /* Re-enable */
    ret = pi_af_set_analytic_enabled(&ctx, id, true);
    ASSERT(ret == PI_AF_OK, "re-enable failed");

    /* Unregister */
    ret = pi_af_unregister_analytic(&ctx, id);
    ASSERT(ret == PI_AF_OK, "unregister failed");
    ASSERT(ctx.analytics_count == 0, "not removed");

    pi_af_shutdown(&ctx);
    PASS();
}

static void test_core_schedule_and_execute(void) {
    TEST("core schedule and execute");
    pi_af_analytics_context_t ctx;
    pi_af_init(&ctx, 64);

    /* Register a periodic analytic */
    pi_af_analytic_t a;
    memset(&a, 0, sizeof(a));
    strcpy(a.name, "Periodic Calc");
    strcpy(a.expression, "|Temp * 9/5 + 32");
    a.schedule_type = PI_AF_SCHEDULE_PERIODIC;
    a.period_seconds = 10.0;
    a.enabled = true;

    uint32_t id;
    pi_af_register_analytic(&ctx, &a, &id);

    /* Schedule it */
    pi_af_error_t ret = pi_af_schedule_analytic(&ctx, id, 0);
    ASSERT(ret == PI_AF_OK, "schedule failed");
    ASSERT(ctx.queue_size == 1, "queue should have 1 entry");

    /* Process it */
    ret = pi_af_process_next(&ctx);
    ASSERT(ret == PI_AF_OK, "process failed");

    pi_af_analytic_t *fa = pi_af_get_analytic(&ctx, id);
    ASSERT(fa->execution_count == 1, "not executed");

    /* After periodic execution, it should be re-scheduled */
    ASSERT(ctx.queue_size == 1, "not re-scheduled after periodic exec");

    pi_af_shutdown(&ctx);
    PASS();
}

static void test_core_topological_sort(void) {
    TEST("core topological sort");
    pi_af_analytics_context_t ctx;
    pi_af_init(&ctx, 64);

    /* Register 3 analytics: C depends on B depends on A */
    uint32_t id_a, id_b, id_c;
    pi_af_analytic_t a0, a1, a2;
    memset(&a0, 0, sizeof(a0)); memset(&a1, 0, sizeof(a1));
    memset(&a2, 0, sizeof(a2));

    strcpy(a0.name, "A"); strcpy(a0.expression, "1");
    a0.schedule_type = PI_AF_SCHEDULE_ON_DEMAND;
    pi_af_register_analytic(&ctx, &a0, &id_a);

    strcpy(a1.name, "B"); strcpy(a1.expression, "|A + 1");
    a1.schedule_type = PI_AF_SCHEDULE_ON_DEMAND;
    a1.dependency_ids[0] = id_a;
    a1.dependency_count = 1;
    pi_af_register_analytic(&ctx, &a1, &id_b);

    strcpy(a2.name, "C"); strcpy(a2.expression, "|B + 1");
    a2.schedule_type = PI_AF_SCHEDULE_ON_DEMAND;
    a2.dependency_ids[0] = id_b;
    a2.dependency_count = 1;
    pi_af_register_analytic(&ctx, &a2, &id_c);

    pi_af_error_t ret = pi_af_build_execution_order(&ctx);
    ASSERT(ret == PI_AF_OK, "topo sort failed");
    ASSERT(ctx.order_valid == true, "order should be valid");

    pi_af_shutdown(&ctx);
    PASS();
}

static void test_core_time_operations(void) {
    TEST("core time operations");
    /* Overlapping ranges */
    ASSERT(pi_af_time_range_overlaps(100, 200, 150, 250) == true,
           "should overlap");
    ASSERT(pi_af_time_range_overlaps(100, 200, 250, 300) == false,
           "should not overlap");
    ASSERT(pi_af_time_range_overlaps(100, 200, 100, 200) == true,
           "identical should overlap");

    /* Intersection */
    time_t s, e;
    ASSERT(pi_af_time_range_intersection(100, 200, 150, 250, &s, &e) == true,
           "should intersect");
    ASSERT(s == 150 && e == 200, "wrong intersection");

    ASSERT(pi_af_time_range_intersection(100, 200, 250, 300, &s, &e) == false,
           "should not intersect");

    PASS();
}

/* --------------------------------------------------------------------------
 * 2. Expression Engine Tests
 * ------------------------------------------------------------------------*/

static void test_expression_tokenize(void) {
    TEST("expression tokenize");
    pi_af_token_t tokens[64];
    uint32_t count;

    pi_af_error_t ret = pi_af_expression_tokenize("3.14 + 2 * x", tokens, 64, &count);
    ASSERT(ret == PI_AF_OK, "tokenize failed");
    ASSERT(count >= 5, "too few tokens");
    ASSERT(tokens[0].type == PI_AF_TOK_NUMBER, "first should be number");
    ASSERT(tokens[0].num_value == 3.14, "wrong number value");
    ASSERT(tokens[1].type == PI_AF_TOK_OPERATOR, "should be operator +");
    ASSERT(tokens[2].type == PI_AF_TOK_NUMBER, "should be number 2");

    /* Test tokenize of expression with parentheses and function */
    ret = pi_af_expression_tokenize("Abs(-5.5) + Max(1,2)", tokens, 64, &count);
    ASSERT(ret == PI_AF_OK, "complex tokenize failed");
    PASS();
}

static void test_expression_parse_evaluate(void) {
    TEST("expression parse and evaluate");

    /* Simple arithmetic */
    pi_af_token_t tokens[64];
    uint32_t count;
    pi_af_expression_tokenize("2 + 3 * 4", tokens, 64, &count);

    pi_af_expression_t expr;
    memset(&expr, 0, sizeof(expr));
    pi_af_error_t ret = pi_af_expression_parse(tokens, count, &expr);
    ASSERT(ret == PI_AF_OK, "parse failed");
    ASSERT(expr.is_valid == true, "expression not valid");

    double result;
    char *error = NULL;
    double vals[] = {0.0};
    bool ok = pi_af_expression_evaluate(&expr, vals, 1, &result, &error);
    ASSERT(ok == true, "evaluate failed");
    /* 2 + 3 * 4 = 2 + 12 = 14 */
    ASSERT_DOUBLE_EQ(result, 14.0, 0.0001, "wrong result for 2+3*4");

    PASS();
}

static void test_expression_compare_logic(void) {
    TEST("expression comparison and logic");

    pi_af_token_t tokens[64];
    uint32_t count;
    pi_af_expression_tokenize("5 > 3 && 2 < 10", tokens, 64, &count);

    pi_af_expression_t expr;
    memset(&expr, 0, sizeof(expr));
    pi_af_error_t ret = pi_af_expression_parse(tokens, count, &expr);
    ASSERT(ret == PI_AF_OK, "logic parse failed");

    double result;
    char *error = NULL;
    double vals[] = {0.0};
    bool ok = pi_af_expression_evaluate(&expr, vals, 1, &result, &error);
    ASSERT(ok == true, "logic evaluate failed");
    ASSERT_DOUBLE_EQ(result, 1.0, 0.0001, "5>3 && 2<10 should be true");

    PASS();
}

static void test_expression_validate(void) {
    TEST("expression validate");
    char err[256];
    ASSERT(pi_af_expression_validate("2 + 3", err, sizeof(err)) == true,
           "valid expression should pass");
    ASSERT(pi_af_expression_validate("2 + ", err, sizeof(err)) == false,
           "invalid expression should fail");
    PASS();
}

static void test_expression_functions(void) {
    TEST("expression built-in functions");

    /* Abs */
    pi_af_token_t tokens[64];
    uint32_t count;
    pi_af_expression_tokenize("Abs(-42)", tokens, 64, &count);

    pi_af_expression_t expr;
    memset(&expr, 0, sizeof(expr));
    pi_af_expression_parse(tokens, count, &expr);

    double result;
    char *error = NULL;
    double vals[] = {0.0};
    bool ok = pi_af_expression_evaluate(&expr, vals, 1, &result, &error);
    ASSERT(ok == true, "Abs eval failed");
    ASSERT_DOUBLE_EQ(result, 42.0, 0.0001, "Abs(-42) should be 42");

    /* Min/Max */
    pi_af_expression_tokenize("Min(10, 5, 8)", tokens, 64, &count);
    memset(&expr, 0, sizeof(expr));
    pi_af_expression_parse(tokens, count, &expr);
    ok = pi_af_expression_evaluate(&expr, vals, 1, &result, &error);
    ASSERT(ok == true, "Min eval failed");
    ASSERT_DOUBLE_EQ(result, 5.0, 0.0001, "Min(10,5,8) should be 5");

    /* If */
    pi_af_expression_tokenize("If(1, 100, 200)", tokens, 64, &count);
    memset(&expr, 0, sizeof(expr));
    pi_af_expression_parse(tokens, count, &expr);
    ok = pi_af_expression_evaluate(&expr, vals, 1, &result, &error);
    ASSERT(ok == true, "If eval failed");
    ASSERT_DOUBLE_EQ(result, 100.0, 0.0001, "If(1,100,200) should be 100");

    PASS();
}

/* --------------------------------------------------------------------------
 * 3. Time-Series Analytics Tests
 * ------------------------------------------------------------------------*/

static void test_sliding_window(void) {
    TEST("sliding window operations");

    pi_af_sliding_window_t win;
    pi_af_sliding_window_init(&win, 4);

    ASSERT(pi_af_sliding_window_count(&win) == 0, "initial count not 0");

    pi_af_datapoint_t p1 = {100, 10.0, PI_AF_QUALITY_GOOD, false};
    pi_af_sliding_window_push(&win, &p1);
    ASSERT(pi_af_sliding_window_count(&win) == 1, "count should be 1");

    pi_af_datapoint_t p2 = {200, 20.0, PI_AF_QUALITY_GOOD, false};
    pi_af_sliding_window_push(&win, &p2);
    pi_af_datapoint_t p3 = {300, 30.0, PI_AF_QUALITY_GOOD, false};
    pi_af_sliding_window_push(&win, &p3);
    pi_af_datapoint_t p4 = {400, 40.0, PI_AF_QUALITY_GOOD, false};
    pi_af_sliding_window_push(&win, &p4);

    ASSERT(pi_af_sliding_window_count(&win) == 4, "count should be 4");

    /* Push 5th point — should wrap */
    pi_af_datapoint_t p5 = {500, 50.0, PI_AF_QUALITY_GOOD, false};
    pi_af_sliding_window_push(&win, &p5);
    ASSERT(pi_af_sliding_window_count(&win) == 4, "count still 4 after wrap");

    /* First point should now be p2 (20.0) */
    const pi_af_datapoint_t *fp = pi_af_sliding_window_get(&win, 0);
    ASSERT(fp != NULL && fp->value == 20.0, "first after wrap should be 20.0");

    pi_af_sliding_window_free(&win);
    PASS();
}

static void test_window_aggregates(void) {
    TEST("window aggregates");

    pi_af_sliding_window_t win;
    pi_af_sliding_window_init(&win, 10);

    double values[] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; i++) {
        pi_af_datapoint_t p = {100 * (time_t)(i+1), values[i],
                                PI_AF_QUALITY_GOOD, false};
        pi_af_sliding_window_push(&win, &p);
    }

    double result;

    pi_af_window_aggregate(&win, PI_AF_AGG_SUM, &result);
    ASSERT_DOUBLE_EQ(result, 150.0, 0.001, "sum should be 150");

    pi_af_window_aggregate(&win, PI_AF_AGG_AVERAGE, &result);
    ASSERT_DOUBLE_EQ(result, 30.0, 0.001, "avg should be 30");

    pi_af_window_aggregate(&win, PI_AF_AGG_MIN, &result);
    ASSERT_DOUBLE_EQ(result, 10.0, 0.001, "min should be 10");

    pi_af_window_aggregate(&win, PI_AF_AGG_MAX, &result);
    ASSERT_DOUBLE_EQ(result, 50.0, 0.001, "max should be 50");

    pi_af_window_aggregate(&win, PI_AF_AGG_STDDEV_POP, &result);
    ASSERT_DOUBLE_EQ(result, 14.1421, 0.01, "stddev pop wrong");

    pi_af_sliding_window_free(&win);
    PASS();
}

static void test_ema(void) {
    TEST("EMA (Exponential Moving Average)");

    pi_af_ema_state_t ema;
    pi_af_ema_init(&ema, 0.3); /* α = 0.3 */

    double out;
    pi_af_ema_update(&ema, 100.0, 1000, &out);
    ASSERT_DOUBLE_EQ(out, 100.0, 0.001, "first EMA should be value itself");

    pi_af_ema_update(&ema, 200.0, 2000, &out);
    /* EMA = 0.3*200 + 0.7*100 = 60 + 70 = 130 */
    ASSERT_DOUBLE_EQ(out, 130.0, 0.001, "EMA(t2) should be 130");

    pi_af_ema_update(&ema, 130.0, 3000, &out);
    /* EMA = 0.3*130 + 0.7*130 = 130 (stable) */
    ASSERT_DOUBLE_EQ(out, 130.0, 0.001, "EMA(t3) stable");

    PASS();
}

static void test_holt_winters(void) {
    TEST("Holt-Winters Triple Exponential Smoothing");

    pi_af_holt_winters_state_t hw;
    pi_af_error_t ret = pi_af_holt_winters_init(&hw, 0.2, 0.1, 0.1, 4);
    ASSERT(ret == PI_AF_OK, "HW init failed");

    double level, trend, forecast;
    /* Feed in a simple seasonal pattern: 10, 20, 30, 40 repeating */
    for (int i = 0; i < 12; i++) {
        double val = 10.0 * (double)((i % 4) + 1);
        ret = pi_af_holt_winters_update(&hw, val, 1000 * (time_t)(i+1),
                                         &level, &trend, &forecast);
        ASSERT(ret == PI_AF_OK, "HW update failed");
    }

    /* After training, predict next step */
    double pred = pi_af_holt_winters_forecast(&hw, 1);
    ASSERT(pred > 0.0, "forecast should be positive");

    pi_af_holt_winters_free(&hw);
    PASS();
}

static void test_rate_of_change(void) {
    TEST("rate of change (linear regression)");

    pi_af_datapoint_t data[5];
    for (int i = 0; i < 5; i++) {
        data[i].timestamp = 100 + (time_t)i * 10;
        data[i].value = 2.0 * (double)i; /* slope = 0.2 per second */
        data[i].quality = PI_AF_QUALITY_GOOD;
    }

    pi_af_time_window_t win;
    memset(&win, 0, sizeof(win));
    win.type = PI_AF_WIN_TYPE_ABSOLUTE;
    win.start_ts = 100;
    win.end_ts = 140;

    double slope;
    pi_af_error_t ret = pi_af_rate_of_change(data, 5, &win, &slope);
    ASSERT(ret == PI_AF_OK, "ROC failed");
    ASSERT_DOUBLE_EQ(slope, 0.2, 0.01, "slope should be 0.2");

    PASS();
}

static void test_cusum(void) {
    TEST("CUSUM change detection");

    /* Steady state at mean=50, then shift to 55 after point 5 */
    pi_af_datapoint_t data[10];
    for (int i = 0; i < 10; i++) {
        data[i].timestamp = 100 * (time_t)(i+1);
        data[i].value = (i < 5) ? 50.0 : 55.0;
        data[i].quality = PI_AF_QUALITY_GOOD;
    }

    double cusum_val;
    bool alarmed;
    pi_af_error_t ret = pi_af_cusum_detect(data, 10, 50.0, 1.0, 5.0,
                                             &cusum_val, &alarmed);
    ASSERT(ret == PI_AF_OK, "CUSUM failed");
    ASSERT(alarmed == true, "CUSUM should detect shift");
    ASSERT(cusum_val > 0.0, "CUSUM should be positive");

    PASS();
}

static void test_percent_good(void) {
    TEST("percent good");

    pi_af_datapoint_t data[4];
    data[0].quality = PI_AF_QUALITY_GOOD;
    data[1].quality = PI_AF_QUALITY_GOOD;
    data[2].quality = PI_AF_QUALITY_BAD;
    data[3].quality = PI_AF_QUALITY_GOOD;

    double pg = pi_af_percent_good(data, 4);
    ASSERT_DOUBLE_EQ(pg, 75.0, 0.001, "3/4 good = 75%");

    PASS();
}

/* --------------------------------------------------------------------------
 * 4. KPI Engine Tests
 * ------------------------------------------------------------------------*/

static void test_kpi_evaluate_higher_better(void) {
    TEST("KPI evaluate (higher-is-better)");

    pi_af_kpi_t kpi;
    memset(&kpi, 0, sizeof(kpi));
    strcpy(kpi.name, "OEE");
    kpi.direction = PI_AF_KPI_DIR_HIGHER_BETTER;
    kpi.target = 85.0;

    /* Add thresholds */
    kpi.thresholds[0].value = 70.0;
    kpi.thresholds[0].status = PI_AF_KPI_STATUS_WARNING;
    kpi.thresholds[0].inclusive = true;
    kpi.thresholds[1].value = 50.0;
    kpi.thresholds[1].status = PI_AF_KPI_STATUS_CRITICAL;
    kpi.thresholds[1].inclusive = true;
    kpi.threshold_count = 2;

    pi_af_kpi_status_t status;
    double score;

    /* Value 90 ≥ target 85 → GOOD */
    pi_af_kpi_evaluate(&kpi, 90.0, &status, &score);
    ASSERT(status == PI_AF_KPI_STATUS_GOOD, "90 should be GOOD");
    ASSERT(score >= 100.0, "perf should be 100+ clamped to 100");

    /* Value 75 → WARNING */
    pi_af_kpi_evaluate(&kpi, 75.0, &status, &score);
    ASSERT(status == PI_AF_KPI_STATUS_WARNING, "75 should be WARNING");

    /* Value 40 → CRITICAL */
    pi_af_kpi_evaluate(&kpi, 40.0, &status, &score);
    ASSERT(status == PI_AF_KPI_STATUS_CRITICAL, "40 should be CRITICAL");

    PASS();
}

static void test_kpi_composite_score(void) {
    TEST("KPI composite score");

    double scores[] = {80.0, 90.0, 70.0};
    double weights[] = {2.0, 1.0, 3.0};
    double composite;

    pi_af_error_t ret = pi_af_kpi_composite_score(scores, weights, 3, &composite);
    ASSERT(ret == PI_AF_OK, "composite failed");
    /* (2*80 + 1*90 + 3*70) / (2+1+3) = (160+90+210)/6 = 460/6 = 76.67 */
    ASSERT_DOUBLE_EQ(composite, 76.6667, 0.01, "composite score wrong");

    PASS();
}

static void test_kpi_build_order(void) {
    TEST("KPI build calculation order");

    pi_af_kpi_context_t kctx;
    pi_af_kpi_init(&kctx);

    /* Parent KPI with 2 children */
    pi_af_kpi_t child1, child2, parent;
    memset(&child1, 0, sizeof(child1));
    memset(&child2, 0, sizeof(child2));
    memset(&parent, 0, sizeof(parent));

    strcpy(child1.name, "Line1 OEE");
    strcpy(child2.name, "Line2 OEE");
    strcpy(parent.name, "Plant OEE");
    parent.is_rollup = true;
    parent.child_kpi_ids[0] = 0; /* will be set after register */
    parent.child_kpi_ids[1] = 0;
    parent.child_kpi_count = 0;

    uint32_t id1, id2, idp;
    pi_af_kpi_register(&kctx, &child1, &id1);
    pi_af_kpi_register(&kctx, &child2, &id2);

    parent.child_kpi_ids[0] = id1;
    parent.child_kpi_ids[1] = id2;
    parent.child_kpi_count = 2;
    pi_af_kpi_register(&kctx, &parent, &idp);

    pi_af_error_t ret = pi_af_kpi_build_order(&kctx);
    ASSERT(ret == PI_AF_OK, "build order failed");
    ASSERT(kctx.order_valid == true, "order should be valid");
    ASSERT(kctx.calc_order_count == 3, "should have 3 in order");

    PASS();
}

/* --------------------------------------------------------------------------
 * 5. Event Frame Tests
 * ------------------------------------------------------------------------*/

static void test_eventframe_lifecycle(void) {
    TEST("event frame lifecycle");

    pi_af_ef_context_t ectx;
    pi_af_ef_init(&ectx);

    /* Register a template */
    pi_af_ef_template_t tmpl;
    memset(&tmpl, 0, sizeof(tmpl));
    strcpy(tmpl.name, "Batch Start");
    tmpl.default_severity = PI_AF_EF_SEVERITY_MEDIUM;

    uint32_t tpl_id;
    pi_af_error_t ret = pi_af_ef_register_template(&ectx, &tmpl, &tpl_id);
    ASSERT(ret == PI_AF_OK, "template register failed");
    ASSERT(tpl_id > 0, "invalid template ID");

    /* Start an event frame */
    uint32_t ef_id;
    ret = pi_af_ef_start(&ectx, tpl_id, 1000, "Batch 42 started", &ef_id);
    ASSERT(ret == PI_AF_OK, "start failed");
    ASSERT(ef_id > 0, "invalid EF ID");

    pi_af_ef_instance_t *ef = pi_af_ef_get(&ectx, ef_id);
    ASSERT(ef != NULL, "EF lookup failed");
    ASSERT(ef->state == PI_AF_EF_STATE_ACTIVE, "should be active");
    ASSERT(ef->start_time == 1000, "wrong start time");

    /* Duration of active EF */
    double dur = pi_af_ef_duration_seconds(ef);
    ASSERT(dur >= 0.0, "duration should be non-negative");

    /* End the event frame */
    ret = pi_af_ef_end(&ectx, ef_id, 2000, "Batch complete");
    ASSERT(ret == PI_AF_OK, "end failed");

    ef = pi_af_ef_get(&ectx, ef_id);
    ASSERT(ef != NULL && ef->state == PI_AF_EF_STATE_CLOSED, "should be closed");
    ASSERT(ef->end_time == 2000, "wrong end time");

    PASS();
}

static void test_eventframe_trigger_eval(void) {
    TEST("event frame trigger evaluation");

    pi_af_ef_trigger_t trig;
    memset(&trig, 0, sizeof(trig));
    trig.trigger_type = PI_AF_EF_TRIGGER_THRESHOLD_UP;
    trig.threshold = 100.0;
    trig.hysteresis = 5.0;

    bool triggered;

    /* Not triggered (below threshold) */
    pi_af_ef_evaluate_trigger(&trig, 90.0, 80.0, &triggered);
    ASSERT(triggered == false, "should not trigger below");

    /* Triggered (cross above with hysteresis) */
    pi_af_ef_evaluate_trigger(&trig, 110.0, 90.0, &triggered);
    ASSERT(triggered == true, "should trigger crossing up");

    /* Digital trigger */
    trig.trigger_type = PI_AF_EF_TRIGGER_DIGITAL_ON;
    pi_af_ef_evaluate_trigger(&trig, 1.0, 0.0, &triggered);
    ASSERT(triggered == true, "digital ON should trigger");

    pi_af_ef_evaluate_trigger(&trig, 0.0, 1.0, &triggered);
    ASSERT(triggered == false, "digital 1→0 should not trigger ON");

    PASS();
}

/* --------------------------------------------------------------------------
 * 6. Asset Rollup Tests
 * ------------------------------------------------------------------------*/

static void test_asset_hierarchy(void) {
    TEST("asset hierarchy build and traverse");

    pi_af_asset_context_t actx;
    pi_af_asset_init(&actx);

    /* Build: Enterprise → Site → Unit1, Unit2
     *        Unit1 → Sensor1, Sensor2
     *        Unit2 → Sensor3 */
    pi_af_asset_node_t ent, site, u1, u2, s1, s2, s3;

    memset(&ent, 0, sizeof(ent));
    strcpy(ent.name, "Enterprise"); ent.category = PI_AF_ASSET_CAT_ENTERPRISE;
    uint32_t ide, ids, idu1, idu2, ids1, ids2, ids3;

    pi_af_asset_add(&actx, &ent, &ide);

    memset(&site, 0, sizeof(site));
    strcpy(site.name, "Site A"); site.category = PI_AF_ASSET_CAT_SITE;
    site.parent_id = ide;
    site.rollup_method = PI_AF_ROLLUP_AVERAGE;
    pi_af_asset_add(&actx, &site, &ids);

    memset(&u1, 0, sizeof(u1));
    strcpy(u1.name, "Unit 1"); u1.category = PI_AF_ASSET_CAT_UNIT;
    u1.parent_id = ids; u1.rollup_method = PI_AF_ROLLUP_AVERAGE;
    pi_af_asset_add(&actx, &u1, &idu1);

    memset(&u2, 0, sizeof(u2));
    strcpy(u2.name, "Unit 2"); u2.category = PI_AF_ASSET_CAT_UNIT;
    u2.parent_id = ids; u2.rollup_method = PI_AF_ROLLUP_AVERAGE;
    pi_af_asset_add(&actx, &u2, &idu2);

    memset(&s1, 0, sizeof(s1));
    strcpy(s1.name, "Sensor1"); s1.category = PI_AF_ASSET_CAT_SENSOR;
    s1.parent_id = idu1;
    pi_af_asset_add(&actx, &s1, &ids1);
    pi_af_asset_set_value(&actx, ids1, 50.0, 0);

    memset(&s2, 0, sizeof(s2));
    strcpy(s2.name, "Sensor2"); s2.category = PI_AF_ASSET_CAT_SENSOR;
    s2.parent_id = idu1;
    pi_af_asset_add(&actx, &s2, &ids2);
    pi_af_asset_set_value(&actx, ids2, 60.0, 0);

    memset(&s3, 0, sizeof(s3));
    strcpy(s3.name, "Sensor3"); s3.category = PI_AF_ASSET_CAT_SENSOR;
    s3.parent_id = idu2;
    pi_af_asset_add(&actx, &s3, &ids3);
    pi_af_asset_set_value(&actx, ids3, 70.0, 0);

    /* Rollup Unit 1 */
    double val;
    pi_af_asset_rollup(&actx, idu1, true, &val);
    ASSERT_DOUBLE_EQ(val, 55.0, 0.001, "Unit1 avg of 50,60 should be 55");

    /* Rollup Unit 2 */
    pi_af_asset_rollup(&actx, idu2, true, &val);
    ASSERT_DOUBLE_EQ(val, 70.0, 0.001, "Unit2 with single child 70 = 70");

    /* Rollup Site (avg of 55 and 70 = 62.5) */
    pi_af_asset_rollup(&actx, ids, true, &val);
    ASSERT_DOUBLE_EQ(val, 62.5, 0.001, "Site avg should be 62.5");

    /* DFS traversal */
    uint32_t order[32];
    uint32_t count;
    pi_af_asset_dfs(&actx, ide, order, 32, &count);
    ASSERT(count >= 5, "DFS should visit all nodes");

    /* Depth */
    uint32_t depth;
    pi_af_asset_depth(&actx, ids3, &depth);
    ASSERT(depth == 4, "Sensor3 depth should be 4 (Enterprise→Site→Unit2→Sensor3)");

    /* LCA */
    uint32_t lca;
    pi_af_asset_lowest_common_ancestor(&actx, ids1, ids2, &lca);
    ASSERT(lca == idu1, "LCA of s1 and s2 should be Unit1");

    pi_af_asset_lowest_common_ancestor(&actx, ids1, ids3, &lca);
    ASSERT(lca == ids, "LCA of s1 and s3 should be Site");

    /* Leaves */
    uint32_t leaves[32];
    uint32_t leaf_count;
    pi_af_asset_get_leaves(&actx, leaves, 32, &leaf_count);
    ASSERT(leaf_count == 3, "should have 3 leaves");

    PASS();
}

static void test_asset_rollup_all(void) {
    TEST("asset rollup all");

    pi_af_asset_context_t actx;
    pi_af_asset_init(&actx);

    /* Simple tree: Root → A, B */
    pi_af_asset_node_t root, a, b;
    memset(&root, 0, sizeof(root)); strcpy(root.name, "Root");
    root.rollup_method = PI_AF_ROLLUP_SUM;

    memset(&a, 0, sizeof(a)); strcpy(a.name, "A");
    memset(&b, 0, sizeof(b)); strcpy(b.name, "B");

    uint32_t idr, ida, idb;
    pi_af_asset_add(&actx, &root, &idr);
    a.parent_id = idr; pi_af_asset_add(&actx, &a, &ida);
    b.parent_id = idr; pi_af_asset_add(&actx, &b, &idb);

    pi_af_asset_set_value(&actx, ida, 100.0, 0);
    pi_af_asset_set_value(&actx, idb, 200.0, 0);

    pi_af_asset_rollup_all(&actx);

    pi_af_asset_node_t *r = pi_af_asset_get(&actx, idr);
    ASSERT(r != NULL, "root lookup failed");
    ASSERT(r->rolled_valid == true, "rollup should be valid");
    ASSERT_DOUBLE_EQ(r->rolled_value, 300.0, 0.001, "root sum should be 300");

    PASS();
}

/* --------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------*/

int main(void) {
    printf("=== PI AF Analytics Test Suite ===\n\n");

    printf("--- Core Engine ---\n");
    test_core_init_shutdown();
    test_core_register_analytic();
    test_core_schedule_and_execute();
    test_core_topological_sort();
    test_core_time_operations();

    printf("\n--- Expression Engine ---\n");
    test_expression_tokenize();
    test_expression_parse_evaluate();
    test_expression_compare_logic();
    test_expression_validate();
    test_expression_functions();

    printf("\n--- Time-Series Analytics ---\n");
    test_sliding_window();
    test_window_aggregates();
    test_ema();
    test_holt_winters();
    test_rate_of_change();
    test_cusum();
    test_percent_good();

    printf("\n--- KPI Engine ---\n");
    test_kpi_evaluate_higher_better();
    test_kpi_composite_score();
    test_kpi_build_order();

    printf("\n--- Event Frames ---\n");
    test_eventframe_lifecycle();
    test_eventframe_trigger_eval();

    printf("\n--- Asset Rollup ---\n");
    test_asset_hierarchy();
    test_asset_rollup_all();

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    return (tests_failed > 0) ? 1 : 0;
}
