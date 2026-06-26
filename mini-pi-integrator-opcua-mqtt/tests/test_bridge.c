/* test_bridge.c — OPC UA Bridge API tests */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "../include/pi_opcua_bridge.h"

static int passed = 0, failed = 0;
#define T(n) printf("  TEST: %s ... ", n)
#define P() do { printf("PASS\n"); passed++; } while(0)
#define F(m) do { printf("FAIL: %s\n", m); failed++; } while(0)
#define C(c) do { if (c) P(); else F(#c); } while(0)
#define EQ(a,b) do { if ((a)==(b)) P(); else { printf("FAIL: %d != %d\n", (int)(a),(int)(b)); failed++; } } while(0)
#define STREQ(a,b) do { if (a && b && strcmp(a,b)==0) P(); else F("string mismatch"); } while(0)

int main(void) {
    printf("=== OPC UA Bridge Tests ===\n");

    /* L1: NodeId creation */
    T("NodeId numeric");
    pi_opcua_node_id_t *n1 = pi_opcua_node_id_new_numeric(0, 2258);
    C(n1 != NULL); pi_opcua_node_id_free(n1);

    T("NodeId string");
    pi_opcua_node_id_t *n2 = pi_opcua_node_id_new_string(1, "MyVariable");
    C(n2 != NULL); EQ(n2->namespace_index, 1); pi_opcua_node_id_free(n2);

    T("NodeId equality");
    pi_opcua_node_id_t *a = pi_opcua_node_id_new_numeric(0, 1234);
    pi_opcua_node_id_t *b = pi_opcua_node_id_new_numeric(0, 1234);
    C(pi_opcua_node_id_equals(a, b));
    pi_opcua_node_id_free(a); pi_opcua_node_id_free(b);

    T("NodeId inequality");
    pi_opcua_node_id_t *c = pi_opcua_node_id_new_numeric(0, 100);
    pi_opcua_node_id_t *d = pi_opcua_node_id_new_numeric(0, 200);
    C(!pi_opcua_node_id_equals(c, d));
    pi_opcua_node_id_free(c); pi_opcua_node_id_free(d);

    T("NodeId clone");
    pi_opcua_node_id_t *orig = pi_opcua_node_id_new_string(2, "Temp");
    pi_opcua_node_id_t *cpy = pi_opcua_node_id_clone(orig);
    C(pi_opcua_node_id_equals(orig, cpy));
    pi_opcua_node_id_free(orig); pi_opcua_node_id_free(cpy);

    T("NodeId hash consistency");
    pi_opcua_node_id_t *x = pi_opcua_node_id_new_numeric(1, 42);
    pi_opcua_node_id_t *y = pi_opcua_node_id_new_numeric(1, 42);
    EQ(pi_opcua_node_id_hash(x), pi_opcua_node_id_hash(y));
    pi_opcua_node_id_free(x); pi_opcua_node_id_free(y);

    /* L1: Variant */
    T("Variant double");
    pi_opcua_variant_t var;
    pi_opcua_variant_init(&var);
    EQ(var.type, PI_OPCUA_TYPE_NULL);
    pi_opcua_variant_set_double(&var, 3.14);
    EQ(var.type, PI_OPCUA_TYPE_DOUBLE);
    C(fabs(pi_opcua_variant_as_double(&var) - 3.14) < 1e-9);
    pi_opcua_variant_clear(&var);

    T("Variant int32");
    pi_opcua_variant_set_int32(&var, 42);
    EQ(var.type, PI_OPCUA_TYPE_INT32);
    EQ((int)pi_opcua_variant_as_double(&var), 42);
    pi_opcua_variant_clear(&var);

    T("Variant string");
    EQ(pi_opcua_variant_set_string(&var, "hello"), PI_BRIDGE_OK);
    EQ(var.type, PI_OPCUA_TYPE_STRING);
    pi_opcua_variant_clear(&var);

    T("Variant bool");
    pi_opcua_variant_set_bool(&var, true);
    EQ(var.type, PI_OPCUA_TYPE_BOOLEAN);
    EQ((int)pi_opcua_variant_as_double(&var), 1);
    pi_opcua_variant_clear(&var);

    /* L1: DateTime */
    T("UA datetime roundtrip");
    int64_t unix = 1712345678;
    int64_t ua = pi_opcua_datetime_from_unix(unix);
    EQ(pi_opcua_datetime_to_unix(ua), unix);

    /* L1: Quality conversion */
    T("Quality to Status roundtrip");
    EQ(pi_quality_to_opcua_status(PI_QUALITY_GOOD), 0x00000000);
    EQ(pi_opcua_status_to_quality(0x00000000), PI_QUALITY_GOOD);

    /* L2: Session */
    T("Session config init");
    pi_opcua_session_config_t cfg;
    pi_opcua_session_config_init(&cfg);
    EQ(cfg.session_timeout_ms, 3600000);

    T("Session connect");
    cfg.endpoint_url = "opc.tcp://localhost:4840";
    cfg.application_name = "pi-integrator-test";
    pi_opcua_session_t *sess = pi_opcua_session_connect(&cfg);
    C(sess != NULL);
    EQ(pi_opcua_session_get_state(sess), PI_OPCUA_SESSION_ACTIVATED);

    T("Session read value");
    pi_opcua_node_id_t *node = pi_opcua_node_id_new_numeric(2, 12345);
    pi_opcua_data_value_t dv;
    EQ(pi_opcua_read_value(sess, node, &dv), PI_BRIDGE_OK);
    EQ(dv.status_code, 0);
    pi_opcua_data_value_clear(&dv); pi_opcua_node_id_free(node);

    pi_opcua_session_disconnect(sess);

    /* L1: PI data point */
    T("PI data point");
    pi_data_point_t *dp = pi_data_point_new("SINUSOID", 99.5, 1712345678, PI_QUALITY_GOOD);
    C(dp != NULL); STREQ(dp->tag_name, "SINUSOID");
    C(fabs(dp->value - 99.5) < 1e-9); EQ(dp->quality, PI_QUALITY_GOOD);
    pi_data_point_free(dp);

    printf("\nResults: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
