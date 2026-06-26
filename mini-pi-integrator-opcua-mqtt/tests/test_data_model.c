/* test_data_model.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "../include/pi_integrator_core.h"
static int p=0,f=0;
#define T(n) printf("  TEST: %s ... ", n)
#define P() do { printf("PASS\n"); p++; } while(0)
#define FM(m) do { printf("FAIL: %s\n", m); f++; } while(0)
#define C(c) do { if (c) P(); else FM(#c); } while(0)
#define EQ(a,b) do { if ((a)==(b)) P(); else { printf("FAIL: %d != %d\n",(int)(a),(int)(b)); f++; } } while(0)
int main(void){printf("=== Data Model Tests ===\n");
T("Point cfg");pi_point_config_t pc;pi_point_config_init(&pc);C(pc.point_type==PI_POINT_TYPE_FLOAT64);pi_point_config_free(&pc);
T("AF Elem");pi_af_element_t*e=pi_af_element_new("R1","Reactor",PI_AF_ELEMENT_TYPE_EQUIPMENT);C(e);C(strcmp(e->name,"R1")==0);pi_af_element_free(e);
T("AF Attr");pi_af_attribute_t*a=pi_af_attribute_new("T",PI_AF_ATTR_TYPE_DOUBLE,"degC");C(a);pi_af_attribute_free(a);
T("AF DataRef");pi_af_data_reference_t dr;pi_af_data_reference_init(&dr);C(!dr.is_valid);pi_af_data_reference_free(&dr);
T("Collective");pi_collective_data_t*cd=pi_collective_data_new("S",3);
pi_collective_data_add_value(cd,100,10.0,PI_COLLECTIVE_TYPE_INTERPOLATED);
pi_collective_data_add_value(cd,200,20.0,PI_COLLECTIVE_TYPE_INTERPOLATED);
pi_collective_data_add_value(cd,300,30.0,PI_COLLECTIVE_TYPE_INTERPOLATED);
pi_collective_data_compute_stats(cd);C(cd->is_complete);C(fabs(cd->avg_value-20.0)<0.01);pi_collective_data_free(cd);
T("DTO");pi_transfer_object_t*dto=pi_dto_new("PI","MQTT",PI_DTO_DIRECTION_TO_MQTT,PI_DTO_FORMAT_JSON);C(dto);
pi_dto_set_json_payload(dto,"{\"x\":1}");C(strstr(dto->payload,"x")!=NULL);pi_dto_free(dto);
T("Type reg");pi_type_mapping_registry_t*r=pi_type_mapping_registry_new();C(r);
int ot=-1;EQ(pi_type_map_pi_to_opcua(r,PI_POINT_TYPE_FLOAT64,&ot),PI_MAP_OK);EQ(ot,11);pi_type_mapping_registry_free(r);
T("Timestamp");pi_timestamp_t ts=pi_timestamp_now();C(ts.is_valid);int64_t ms=pi_timestamp_to_unix_ms(ts);C(ms>0);
T("Scale");C(fabs(pi_scale_convert(12.0,4.0,16.0,0.0,100.0)-50.0)<0.01);
T("Conv");pi_conversion_spec_t cs;pi_conversion_spec_init(&cs);cs.coeff_b=2.0;C(fabs(pi_conversion_apply(&cs,5.0)-10.0)<0.01);
cs.conv_type=PI_CONV_SQRT;cs.coeff_c=0;cs.coeff_b=1.0;C(fabs(pi_conversion_apply(&cs,100.0)-10.0)<0.01);pi_conversion_spec_free(&cs);
T("Pipeline");pi_pipeline_t*pp=pi_pipeline_new("p");C(pp);pi_pipeline_add_stage(pp,PI_STAGE_TRANSFORM,"t",NULL,NULL);
C(pp->num_stages==1);pi_pipeline_start(pp);C(pi_pipeline_get_state(pp)==PI_PIPELINE_STAGE_RUNNING);pi_pipeline_free(pp);
T("Context");pi_integrator_context_t*ctx=pi_integrator_context_new("ctx");C(ctx);pi_integrator_context_free(ctx);
T("PQ");pi_priority_queue_t*pq=pi_priority_queue_new(10,true);C(pq);
C(pi_priority_queue_push(pq,(void*)1,50));C(pi_priority_queue_push(pq,(void*)2,10));C(pi_priority_queue_pop(pq)==(void*)2);pi_priority_queue_free(pq);
T("RingBuf");pi_ring_buffer_t*rb=pi_ring_buffer_new(16,false);C(rb);uint8_t d[]={1,2,3};C(pi_ring_buffer_write(rb,d,3));EQ(pi_ring_buffer_available(rb),3);pi_ring_buffer_free(rb);
T("Rate lim");pi_rate_limiter_t rl;pi_rate_limiter_init(&rl,10.0,20.0);C(pi_rate_limiter_consume(&rl,1.0));
T("CB");pi_circuit_breaker_t cb;pi_circuit_breaker_init(&cb,"x",3,2,10000);C(pi_circuit_breaker_allow(&cb));pi_circuit_breaker_failure(&cb);pi_circuit_breaker_failure(&cb);pi_circuit_breaker_failure(&cb);C(pi_circuit_breaker_get_state(&cb)==PI_CB_STATE_OPEN);
T("Backpressure");pi_backpressure_ctrl_t bp;pi_backpressure_init(&bp,0.8,0.5,0.5,1000);C(pi_backpressure_should_throttle(&bp,0.9));
T("Adaptive");pi_adaptive_batch_ctrl_t abc;pi_adaptive_batch_init(&abc,16,256,100,5000,100.0,0.3);C(abc.enabled);pi_adaptive_batch_feedback(&abc,120.0,1000.0);C(pi_adaptive_batch_get_size(&abc)<=136);
T("Timer wheel");pi_timer_wheel_t*tw=pi_timer_wheel_new();C(tw);pi_timer_wheel_free(tw);
T("Health");pi_health_report_t hr;pi_health_report_init(&hr);C(pi_health_report_overall(&hr)==PI_HEALTH_UNKNOWN);
printf("\nResults: %d passed, %d failed\n",p,f);return f>0?1:0;}
