/* test_mqtt.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../include/pi_mqtt_stream.h"
static int p=0,f=0;
#define T(n) printf("  TEST: %s ... ", n)
#define P() do { printf("PASS\n"); p++; } while(0)
#define FM(m) do { printf("FAIL: %s\n", m); f++; } while(0)
#define C(c) do { if (c) P(); else FM(#c); } while(0)
#define EQ(a,b) do { if ((a)==(b)) P(); else { printf("FAIL: %d != %d\n",(int)(a),(int)(b)); f++; } } while(0)
static void cb(void*u,const char*t,const uint8_t*p2,size_t pl,pi_mqtt_qos_t q,bool r){(void)u;(void)t;(void)p2;(void)pl;(void)q;(void)r;}
int main(void){printf("=== MQTT Tests ===\n");
T("Config");pi_mqtt_config_t cfg;pi_mqtt_config_init(&cfg);EQ(cfg.broker_port,1883);
T("Connect");cfg.broker_host="localhost";pi_mqtt_client_t*c=pi_mqtt_client_connect(&cfg);C(c);
T("Topic filter");C(pi_mqtt_topic_filter_is_valid("a/+/b"));C(!pi_mqtt_topic_filter_is_valid("#/b"));
T("Topic match");C(pi_mqtt_topic_matches("a/b","a/+"));
T("Publish");C(pi_mqtt_publish(c,"t",(const uint8_t*)"x",1,PI_MQTT_QOS_0,false)>0);
T("Subscribe");C(pi_mqtt_subscribe(c,"a/#",PI_MQTT_QOS_1,cb,NULL)>0);
T("JSON");pi_data_point_t*d=pi_data_point_new("X",1,1,PI_QUALITY_GOOD);
pi_mqtt_json_payload_t*j=pi_mqtt_serialize_to_json(d);C(j&&j->is_valid);pi_mqtt_json_payload_free(j);
T("CSV");pi_data_point_t da[1];da[0].tag_name="A";da[0].value=1;da[0].timestamp=1;da[0].quality=PI_QUALITY_GOOD;
pi_mqtt_csv_payload_t*cp=pi_mqtt_serialize_to_csv(da,1);C(cp);pi_mqtt_csv_payload_free(cp);
T("Binary");size_t bl;uint8_t*bn=pi_mqtt_serialize_to_binary(d,&bl);C(bn);free(bn);
T("Sparkplug");size_t sl;uint8_t*sp=pi_mqtt_serialize_sparkplug_b("G","E",d,&sl);C(sp);free(sp);
T("Tag map");pi_mqtt_tag_mapping_t*m=pi_mqtt_map_pi_tag(c,"S","pi/S",PI_MQTT_QOS_1,0.1,1.0,1000,"json");C(m);pi_mqtt_start_streaming(c,m);C(m->is_streaming);
pi_mqtt_stop_streaming(c,m);C(!m->is_streaming);
T("Store-fwd");pi_mqtt_store_forward_init(c,NULL);EQ((int)pi_mqtt_store_forward_queue_size(c),0);
T("Batch");pi_mqtt_batch_config_t bc;memset(&bc,0,sizeof(bc));bc.enable_batching=true;pi_mqtt_configure_batching(c,&bc);
T("Will");pi_mqtt_will_t w;memset(&w,0,sizeof(w));w.topic="s";w.payload=(uint8_t*)"o";w.payload_len=1;w.qos=PI_MQTT_QOS_1;w.is_set=true;pi_mqtt_configure_will(c,&w);
T("Stats");pi_mqtt_stream_stats_t s;pi_mqtt_get_stats(c,&s);C(s.total_published>0);
pi_data_point_free(d);pi_mqtt_client_disconnect(c);
printf("\nResults: %d passed, %d failed\n",p,f);return f>0?1:0;}
