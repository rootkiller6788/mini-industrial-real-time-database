/**
 * @file example_notification.c
 * @brief Notification system example - alert delivery for process events
 *
 * Demonstrates complete notification workflow: channel configuration,
 * rule creation with severity filters, event processing through the
 * notification engine, message formatting with template substitution,
 * and delivery simulation.
 *
 * L6 Canonical Problem: Alarm notification for critical process events
 * L7 Application: OSIsoft PI Notifications with email/SMS delivery
 *
 * Reference: ISA-18.2 "Management of Alarm Systems for the Process Industries"
 *            IEC 62682 "Management of alarm systems for the process industries"
 */

#include "event_frame.h"
#include "notification.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>

int main(void) {
    printf("===============================================================\n");
    printf("  PI Notifications - Event Alert Delivery System\n");
    printf("  Scenario: Critical temperature alarm on Reactor R-101\n");
    printf("===============================================================\n\n");

    /* ── Step 1: Configure notification engine ── */
    printf("Step 1: Initialize notification engine\n");

    notif_engine_t engine;
    notif_engine_init(&engine);
    printf("  Engine initialized with %d rule slots\n", NOTIF_MAX_RULES);

    /* ── Step 2: Add delivery channels ── */
    printf("\nStep 2: Configure delivery channels\n");

    notif_channel_t email_ch;
    memset(&email_ch, 0, sizeof(email_ch));
    strcpy(email_ch.name, "SMTP-Corp");
    email_ch.type = NOTIF_CHANNEL_EMAIL;
    email_ch.enabled = 1;
    strcpy(email_ch.smtp_server, "smtp.pharma-corp.com");
    email_ch.smtp_port = 587;
    email_ch.smtp_use_tls = 1;
    email_ch.rate_limit_per_min = 30;
    notif_add_channel(&engine, &email_ch);

    notif_channel_t sms_ch;
    memset(&sms_ch, 0, sizeof(sms_ch));
    strcpy(sms_ch.name, "SMS-OnCall");
    sms_ch.type = NOTIF_CHANNEL_SMS;
    sms_ch.enabled = 1;
    sms_ch.rate_limit_per_min = 10;
    notif_add_channel(&engine, &sms_ch);

    notif_channel_t pv_ch;
    memset(&pv_ch, 0, sizeof(pv_ch));
    strcpy(pv_ch.name, "PI-Vision");
    pv_ch.type = NOTIF_CHANNEL_PI_VISION;
    pv_ch.enabled = 1;
    strcpy(pv_ch.pi_vision_server, "https://pi-vision.pharma-corp.com");
    notif_add_channel(&engine, &pv_ch);

    printf("  Added %d delivery channels: Email, SMS, PI Vision\n", engine.channel_count);

    /* ── Step 3: Create notification rules ── */
    printf("\nStep 3: Create notification rules\n");

    /* Rule 1: Critical alarms to on-call engineer via SMS */
    notif_rule_t rule_critical;
    memset(&rule_critical, 0, sizeof(rule_critical));
    strcpy(rule_critical.name, "Critical-Alarm-SMS");
    strcpy(rule_critical.description, "Immediate SMS for critical process alarms");
    rule_critical.enabled = 1;
    rule_critical.min_severity = EF_SEVERITY_CRITICAL;
    rule_critical.channel_type = NOTIF_CHANNEL_SMS;
    strcpy(rule_critical.channel_name, "SMS-OnCall");
    rule_critical.format = NOTIF_FORMAT_PLAIN_TEXT;
    strcpy(rule_critical.message_template,
           "ALARM: {severity} - {name} on {attr.UnitName}\n"
           "Status: {status} | Duration: {duration}s\n"
           "Acked by: {acked_by}");
    rule_critical.max_delay_ms = 5000;

    /* Add recipient */
    notif_recipient_t *rcpt = &rule_critical.recipients[0];
    strcpy(rcpt->name, "On-Call Engineer");
    strcpy(rcpt->address, "+1-555-0100");
    rcpt->preferred_channel = NOTIF_CHANNEL_SMS;
    rcpt->is_on_call = 1;
    rcpt->priority = 1;
    rule_critical.recipient_count = 1;

    notif_add_rule(&engine, &rule_critical);

    /* Rule 2: Warning-level events to shift supervisor via email */
    notif_rule_t rule_warning;
    memset(&rule_warning, 0, sizeof(rule_warning));
    strcpy(rule_warning.name, "Warning-Email");
    strcpy(rule_warning.description, "Email notification for warning events");
    rule_warning.enabled = 1;
    rule_warning.min_severity = EF_SEVERITY_WARNING;
    rule_warning.channel_type = NOTIF_CHANNEL_EMAIL;
    strcpy(rule_warning.channel_name, "SMTP-Corp");
    rule_warning.format = NOTIF_FORMAT_HTML;
    strcpy(rule_warning.message_template,
           "<h3>{severity}: {name}</h3>"
           "<p>Time: {start_time} - {end_time}</p>"
           "<p>Duration: {duration}s</p>"
           "<p>Description: {description}</p>");
    rule_warning.max_delay_ms = 30000;

    rcpt = &rule_warning.recipients[0];
    strcpy(rcpt->name, "Shift Supervisor");
    strcpy(rcpt->address, "supervisor@pharma-corp.com");
    rcpt->preferred_channel = NOTIF_CHANNEL_EMAIL;
    rcpt->priority = 1;
    rule_warning.recipient_count = 1;

    notif_add_rule(&engine, &rule_warning);

    printf("  Added %d notification rules\n", engine.rule_count);

    /* ── Step 4: Create event frames ── */
    printf("\nStep 4: Create event frames that trigger notifications\n");

    /* Critical event: Reactor over-temperature */
    event_frame_t critical_event;
    ef_init(&critical_event, "R-101 High Temperature", "ProcessAlarm");
    strcpy(critical_event.description, "Reactor R-101 temperature exceeded critical limit of 200C");
    critical_event.severity = EF_SEVERITY_CRITICAL;
    critical_event.trigger_type = EF_TRIGGER_THRESHOLD;
    strcpy(critical_event.source_element, "\\PharmaSite\\ReactorArea\\R-101");

    ef_start(&critical_event);
    ef_set_attribute(&critical_event, "UnitName", EF_ATTR_STRING, "R-101");

    /* Warning event: Pressure deviation */
    event_frame_t warning_event;
    ef_init(&warning_event, "R-101 Pressure Deviation", "ProcessAlarm");
    strcpy(warning_event.description, "Reactor R-101 pressure +15% from setpoint");
    warning_event.severity = EF_SEVERITY_WARNING;
    strcpy(warning_event.source_element, "\\PharmaSite\\ReactorArea\\R-101");

    ef_start(&warning_event);
    ef_set_attribute(&warning_event, "UnitName", EF_ATTR_STRING, "R-101");

    /* Info event: Batch step complete (should NOT trigger notifications) */
    event_frame_t info_event;
    ef_init(&info_event, "R-101 Charge Complete", "UnitOperation");
    strcpy(info_event.description, "R-101 charging step completed successfully");
    info_event.severity = EF_SEVERITY_INFO;
    ef_start(&info_event);

    printf("  Created 3 events: 1 CRITICAL, 1 WARNING, 1 INFO\n");

    /* ── Step 5: Process events through notification engine ── */
    printf("\nStep 5: Process events through notification rules\n");

    int n1 = notif_process_event(&engine, &critical_event);
    printf("  Critical event '%s' -> %d notification(s) generated\n",
           critical_event.name, n1);

    int n2 = notif_process_event(&engine, &warning_event);
    printf("  Warning event '%s' -> %d notification(s) generated\n",
           warning_event.name, n2);

    int n3 = notif_process_event(&engine, &info_event);
    printf("  Info event '%s' -> %d notification(s) generated (expected 0)\n",
           info_event.name, n3);

    assert(n1 > 0);  /* Critical should match */
    assert(n2 > 0);  /* Warning should match */
    assert(n3 == 0); /* Info should NOT match (below min_severity) */

    /* ── Step 6: Rule matching verification ── */
    printf("\nStep 6: Verify rule matching logic\n");

    printf("  Rule 'Critical-Alarm-SMS' matches critical event: %s\n",
           notif_rule_matches(&rule_critical, &critical_event) ? "YES" : "NO");
    printf("  Rule 'Critical-Alarm-SMS' matches info event: %s\n",
           notif_rule_matches(&rule_critical, &info_event) ? "YES" : "NO");
    printf("  Rule 'Warning-Email' matches warning event: %s\n",
           notif_rule_matches(&rule_warning, &warning_event) ? "YES" : "NO");

    /* ── Step 7: Format a notification message ── */
    printf("\nStep 7: Message formatting example\n");

    char formatted[1024];
    notif_format_message(&rule_critical, &critical_event, formatted, sizeof(formatted));
    printf("  Formatted message:\n");
    printf("  ---\n%s\n  ---\n", formatted);

    /* ── Step 8: Simulate delivery ── */
    printf("\nStep 8: Simulate notification delivery\n");

    /* Deliver the first generated notification */
    if (engine.instance_count > 0) {
        notif_instance_t *inst = &engine.instances[0];
        printf("  Attempting delivery to: %s via channel %d\n",
               inst->recipient.name, inst->recipient.preferred_channel);

        int rc = notif_deliver(inst, 0.95);  /* 95% success probability */
        printf("  Delivery result: %s\n",
               rc == 0 ? "SUCCESS" : "FAILED (will retry)");
        if (rc != 0) {
            printf("  Error: %s\n", inst->error_message);
        }
    }

    /* ── Step 9: Engine statistics ── */
    printf("\nStep 9: Notification engine statistics\n");

    uint64_t delivered, failed;
    int pending;
    notif_engine_stats(&engine, &delivered, &failed, &pending);
    printf("  Total instances: %d\n", engine.instance_count);
    printf("  Delivered: %llu, Failed: %llu, Pending: %d\n",
           (unsigned long long)delivered, (unsigned long long)failed, pending);

    printf("\n===============================================================\n");
    printf("  Example Complete - Notification System Demonstrated\n");
    printf("===============================================================\n");

    return 0;
}
