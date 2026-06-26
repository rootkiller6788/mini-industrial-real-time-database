/** example_point_configuration.c - Point database configuration demo */
#include <stdio.h>
#include <string.h>
#include "../include/pi_da_types.h"
#include "../include/pi_point_db.h"
#include "../include/pi_system_mgmt.h"

int main(void) {
    printf("=== PI Point Configuration Demo ===

");
    pi_point_db_t db; pi_point_db_init(&db);

    /* Create points */
    pi_point_attributes_t attrs;
    const char *tags[] = {"TEMPERATURE", "PRESSURE", "FLOW_RATE", "LEVEL"};
    int32_t ids[4];
    int i;
    for (i = 0; i < 4; i++) {
        pi_point_attributes_init_defaults(&attrs, tags[i], PI_POINT_FLOAT64);
        attrs.zero = 0.0; attrs.span = 100.0;
        attrs.exc_dev = 0.1;
        pi_point_db_create(&db, &attrs, &ids[i]);
        printf("Created point %d: %s (id=%d)
", i+1, tags[i], ids[i]);
    }
    printf("Total points: %d
", pi_point_db_count(&db));

    /* System management */
    pi_system_mgmt_t mgmt; pi_mgmt_init(&mgmt);
    pi_mgmt_register_subsystem(&mgmt, "updmgr", 1);
    pi_mgmt_register_subsystem(&mgmt, "archmgr", 2);
    pi_mgmt_update_subsystem_state(&mgmt, "updmgr", PI_SUBSYSTEM_RUNNING);
    pi_mgmt_update_subsystem_state(&mgmt, "archmgr", PI_SUBSYSTEM_RUNNING);
    pi_mgmt_calculate_overall_health(&mgmt);
    pi_mgmt_print_status(&mgmt);

    pi_point_db_destroy(&db);
    pi_mgmt_destroy(&mgmt);
    printf("
Done.
");
    return 0;
}
