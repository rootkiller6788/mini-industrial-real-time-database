/** test_pi_system.c - PI System Architecture Test Suite */
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include "../include/pi_da_types.h"
#include "../include/pi_snapshot.h"
#include "../include/pi_archive.h"
#include "../include/pi_point_db.h"
#include "../include/pi_buffer.h"
#include "../include/pi_collective.h"
#include "../include/pi_security.h"
#include "../include/pi_system_mgmt.h"
static int tests_run=0,tests_passed=0;
#define TEST(n) do{tests_run++;printf("  TEST %s... ",n);}while(0)
#define PASS() do{tests_passed++;printf("PASS\n");}while(0)
#define CHECK(c) do{if(!(c)){printf("FAIL %s:%d\n",__FILE__,__LINE__);return;}}while(0)
#define CLOSE(a,b,t) do{if(fabs((a)-(b))>(t)){printf("FAIL %g!=%g\n",(a),(b));return;}}while(0)

static void test_ts(void) {
    TEST("timestamp_now"); pi_timestamp_t ts; pi_timestamp_now(&ts); CHECK(ts.seconds>0); PASS();
    TEST("timestamp_cmp"); pi_timestamp_t a={100,0},b={200,0};
    CHECK(pi_timestamp_compare(&a,&b)==-1); CHECK(pi_timestamp_compare(&b,&a)==1);
    CHECK(pi_timestamp_compare(&a,&a)==0); PASS();
    TEST("timestamp_diff"); CLOSE(pi_timestamp_diff_seconds(&a,&b),100.0,0.001); PASS();
}

static void test_pt(void) {
    TEST("ptype_name"); CHECK(strcmp(pi_point_type_name(PI_POINT_FLOAT64),"Float64")==0); PASS();
    TEST("ptype_size"); CHECK(pi_point_type_size(PI_POINT_FLOAT64)==8); PASS();
    TEST("ptype_num"); CHECK(pi_point_type_is_numeric(PI_POINT_FLOAT64)==1); PASS();
    TEST("stat_name"); CHECK(strcmp(pi_status_name(PI_STATUS_GOOD),"Good")==0); PASS();
}

static void test_val(void) {
    TEST("val_init"); pi_value_t v; pi_value_init(&v,PI_POINT_FLOAT64);
    CHECK(v.value_type==PI_POINT_FLOAT64); PASS();
    TEST("val_set64"); pi_timestamp_t ts; pi_timestamp_now(&ts);
    pi_value_set_float64(&v,42.5,ts); CLOSE(v.value.as_float64,42.5,0.001); PASS();
    TEST("val_get64"); CLOSE(pi_value_get_float64(&v),42.5,0.001); PASS();
    TEST("val_good"); CHECK(pi_value_is_good(&v)==1); PASS();
    TEST("val_dig"); pi_value_set_digital(&v,1,ts); CHECK(pi_value_get_digital(&v)==1); PASS();
    TEST("val_int"); pi_value_set_int32(&v,100,ts); CHECK(pi_value_get_int32(&v)==100); PASS();
    TEST("val_eq"); pi_value_t v2; pi_value_copy(&v2,&v); CHECK(pi_value_compare_equal(&v,&v2)==1); PASS();
}

static void test_eu(void) {
    TEST("eu_pct"); CLOSE(pi_eu_to_percent(50.0,0.0,100.0),50.0,0.01); PASS();
    TEST("pct_eu"); CLOSE(pi_percent_to_eu(50.0,0.0,100.0),50.0,0.01); PASS();
}

static void test_snap(void) {
    pi_snapshot_t snap; pi_snapshot_init(&snap);
    TEST("snap_putget"); pi_value_t v; pi_value_init(&v,PI_POINT_FLOAT64);
    pi_value_set_float64(&v,3.14,PI_TIME_NOW);
    CHECK(pi_snapshot_put(&snap,1,&v)==0);
    const pi_snapshot_entry_t *e=pi_snapshot_get(&snap,1);
    CHECK(e!=NULL); CLOSE(pi_value_get_float64(&e->current_value),3.14,0.001); PASS();
    TEST("snap_contains"); CHECK(pi_snapshot_contains(&snap,1)==1); PASS();
    TEST("snap_exc"); pi_value_t v2; pi_value_init(&v2,PI_POINT_FLOAT64);
    pi_value_set_float64(&v2,5.0,PI_TIME_NOW);
    CHECK(pi_snapshot_exception_test(&snap,1,&v2,1.0)==1); PASS();
    TEST("snap_size"); CHECK(pi_snapshot_size(&snap)==1); PASS();
    pi_snapshot_destroy(&snap);
}

static void test_arch(void) {
    pi_archive_t arch; pi_archive_init(&arch);
    TEST("arch_store"); pi_archive_event_t ev; memset(&ev,0,sizeof(ev));
    ev.timestamp.seconds=1000; pi_value_set_float64(&ev.value,25.0,ev.timestamp);
    CHECK(pi_archive_store_event(&arch,&ev)==0);
    CHECK(arch.total_events_stored==1); PASS();
    TEST("arch_range"); pi_archive_event_t out[10];
    pi_timestamp_t s={0,0},e={2000,0};
    CHECK(pi_archive_get_events_range(&arch,0,s,e,10,out)==1);
    CLOSE(pi_value_get_float64(&out[0].value),25.0,0.001); PASS();
    TEST("arch_sum"); double avg=pi_archive_get_summary(&arch,0,s,e,PI_SUMMARY_AVERAGE);
    CLOSE(avg,25.0,0.001); PASS();
    pi_archive_destroy(&arch);
}

static void test_pdb(void) {
    pi_point_db_t db; pi_point_db_init(&db);
    TEST("pdb_create"); pi_point_attributes_t a;
    pi_point_attributes_init_defaults(&a,"SINUSOID",PI_POINT_FLOAT64);
    int32_t pid; CHECK(pi_point_db_create(&db,&a,&pid)==0); CHECK(pid==1); PASS();
    TEST("pdb_get"); const pi_point_attributes_t *p=pi_point_db_get_by_id(&db,1);
    CHECK(p!=NULL); CHECK(strcmp(p->tag,"SINUSOID")==0); PASS();
    TEST("pdb_count"); CHECK(pi_point_db_count(&db)==1); PASS();
    pi_point_db_destroy(&db);
}

static void test_buf(void) {
    pi_buffer_t buf; pi_buffer_config_t cfg; memset(&cfg,0,sizeof(cfg));
    cfg.max_events=100; pi_buffer_init(&buf,&cfg);
    TEST("buf_enq"); pi_value_t v; pi_value_init(&v,PI_POINT_FLOAT64);
    pi_value_set_float64(&v,10.0,PI_TIME_NOW);
    CHECK(pi_buffer_enqueue(&buf,1,&v,1)==0);
    CHECK(pi_buffer_queue_size(&buf)==1); PASS();
    TEST("buf_conn"); pi_buffer_set_disconnected(&buf);
    CHECK(pi_buffer_is_connected(&buf)==0);
    pi_buffer_set_connected(&buf); CHECK(pi_buffer_is_connected(&buf)==1); PASS();
    pi_buffer_destroy(&buf);
}

static void test_col(void) {
    pi_collective_t col; pi_collective_init(&col,0);
    TEST("col_add"); CHECK(pi_collective_add_member(&col,"pi1",5450,10)==0);
    CHECK(pi_collective_add_member(&col,"pi2",5450,5)==1);
    CHECK(pi_collective_member_count(&col)==2); PASS();
    pi_collective_destroy(&col);
}

static void test_sec(void) {
    pi_security_db_t sec; pi_security_init(&sec);
    TEST("sec_id"); int32_t uid;
    CHECK(pi_security_create_identity(&sec,"admin",PI_IDENTITY_TYPE_USER,&uid)==0);
    CHECK(uid==1); PASS();
    TEST("sec_check");
    CHECK(pi_security_grant_access(&sec,1,100,PI_ACCESS_READ,0)==0);
    CHECK(pi_security_has_access(&sec,1,100,PI_ACCESS_READ)==1); PASS();
    pi_security_destroy(&sec);
}

static void test_mgmt(void) {
    pi_system_mgmt_t mgmt; pi_mgmt_init(&mgmt);
    TEST("mgmt_sub"); CHECK(pi_mgmt_register_subsystem(&mgmt,"updmgr",1)==0);
    CHECK(pi_mgmt_update_subsystem_state(&mgmt,"updmgr",PI_SUBSYSTEM_RUNNING)==0);
    CHECK(pi_mgmt_calculate_overall_health(&mgmt)==100); PASS();
    pi_mgmt_destroy(&mgmt);
}

int main(void) {
    printf("\n=== PI System Architecture Test Suite ===\n\n");
    test_ts(); test_pt(); test_val(); test_eu();
    test_snap(); test_arch(); test_pdb(); test_buf();
    test_col(); test_sec(); test_mgmt();
    printf("\n=== Results: %d/%d passed ===\n",tests_passed,tests_run);
    return tests_passed==tests_run?0:1;
}
