/* example_integrator_pipeline.c — Full integrator pipeline demo (L6 Canonical + L7 App) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/pi_integrator_core.h"
static void scale_stage(pi_pipeline_stage_t *s, void *d, void *u) {
    (void)u; pi_data_point_t *dp = (pi_data_point_t *)d;
    dp->value = pi_scale_convert(dp->value, 0.0, 100.0, 4.0, 20.0); s->items_processed++;
}
static void filter_stage(pi_pipeline_stage_t *s, void *d, void *u) {
    (void)u; pi_data_point_t *dp = (pi_data_point_t *)d;
    if (dp->quality == PI_QUALITY_BAD) dp->value = 0.0; s->items_processed++;
}
int main(void) {
    printf("=== PI Integrator Pipeline Demo ===\n");
    /* Create integrator context — L2 Core Concept */
    pi_integrator_context_t *ctx = pi_integrator_context_new("plant-demo");
    printf("Integrator: %s\n", ctx->integrator_name);
    /* Type mapping — L3 Engineering Structure */
    pi_type_mapping_registry_t *reg = pi_type_mapping_registry_new();
    int ot = -1; pi_type_map_pi_to_opcua(reg, PI_POINT_TYPE_FLOAT64, &ot);
    printf("PI Float64 -> OPC UA type %d\n", ot);
    pi_type_mapping_registry_free(reg);
    ctx->type_registry = pi_type_mapping_registry_new();
    /* Create pipeline — L2 Core Concept */
    pi_pipeline_t *pipe = pi_pipeline_new("process-pipeline");
    pi_pipeline_set_backpressure(pipe, 1000, 0.8);
    pi_pipeline_add_stage(pipe, PI_STAGE_SOURCE_PI, "source", NULL, NULL);
    pi_pipeline_add_stage(pipe, PI_STAGE_TRANSFORM, "scale", scale_stage, ctx);
    pi_pipeline_add_stage(pipe, PI_STAGE_FILTER, "filter", filter_stage, ctx);
    pi_pipeline_add_stage(pipe, PI_STAGE_ENCODE, "encode", NULL, NULL);
    pi_pipeline_add_stage(pipe, PI_STAGE_DELIVER_MQTT, "mqtt", NULL, NULL);
    printf("Pipeline: %d stages\n", pipe->num_stages);
    pi_integrator_context_add_pipeline(ctx, pipe);
    pi_integrator_start(ctx);
    /* Push data — L5 algorithm demonstration */
    for (int i = 0; i < 5; i++) {
        pi_data_point_t *dp = pi_data_point_new("SINUSOID", 25.0+i*15.0, 1712345678+i*60,
                                                 (i==3)?PI_QUALITY_BAD:PI_QUALITY_GOOD);
        int r = pi_pipeline_push(pipe, dp);
        printf("  Point %d: val=%.1f, stages_ok=%d\n", i, dp->value, r);
        pi_data_point_free(dp);
    }
    /* Health check — L2 Core Concept */
    pi_health_report_t h; pi_health_collect(ctx, &h);
    printf("Health: pipeline=%d, overall=%d\n", (int)h.pipeline_health, (int)pi_health_report_overall(&h));
    /* Rate limiter — L5 Algorithm */
    pi_rate_limiter_t rl; pi_rate_limiter_init(&rl, 100.0, 200.0);
    printf("Rate limiter: ");
    for (int i = 0; i < 5; i++) printf("%s ", pi_rate_limiter_consume(&rl, 1.0)?"Y":"N");
    printf("\n");
    /* Circuit breaker — L5 Algorithm */
    pi_circuit_breaker_t cb; pi_circuit_breaker_init(&cb, "mqtt-cb", 3, 2, 30000.0);
    printf("Circuit breaker state: %d\n", (int)pi_circuit_breaker_get_state(&cb));
    /* Adaptive batch — L8 Advanced */
    pi_adaptive_batch_ctrl_t abc; pi_adaptive_batch_init(&abc, 16, 256, 100, 5000, 100.0, 0.3);
    for (int i = 0; i < 3; i++) {
        pi_adaptive_batch_feedback(&abc, 80.0+i*30.0, 1000.0);
        printf("Adaptive batch %d: size=%u\n", i, (unsigned)pi_adaptive_batch_get_size(&abc));
    }
    pi_integrator_stop(ctx); pi_integrator_context_free(ctx);
    printf("=== Demo complete ===\n");
    return 0;
}
