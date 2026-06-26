# Knowledge Graph — mini-pi-event-frames-notifications

## L1: Definitions (Complete)

| # | Definition | Location | Type |
|---|-----------|----------|------|
| 1 | Event Frame (event_frame_t) | include/event_frame.h:107 | struct |
| 2 | Event Frame Status (ef_status_t) | include/event_frame.h:40 | enum |
| 3 | Event Frame Severity (ef_severity_t) | include/event_frame.h:49 | enum |
| 4 | Trigger Type (ef_trigger_type_t) | include/event_frame.h:60 | enum |
| 5 | Event Attribute (ef_attribute_t) | include/event_frame.h:78 | struct |
| 6 | Active Set (ef_active_set_t) | include/event_frame.h:120 | struct |
| 7 | Template (ef_template_t) | include/event_template.h:68 | struct |
| 8 | Template Attribute Def | include/event_template.h:36 | struct |
| 9 | Template Registry | include/event_template.h:86 | struct |
| 10 | Notification Rule (notif_rule_t) | include/notification.h:113 | struct |
| 11 | Notification Channel | include/notification.h:91 | struct |
| 12 | Notification Recipient | include/notification.h:77 | struct |
| 13 | Notification Instance | include/notification.h:130 | struct |
| 14 | Notification Engine | include/notification.h:146 | struct |
| 15 | Trigger State (trig_state_t) | include/trigger_engine.h:22 | enum |
| 16 | Threshold Direction | include/trigger_engine.h:32 | enum |
| 17 | Value Change Trigger | include/trigger_engine.h:43 | struct |
| 18 | Digital State Trigger | include/trigger_engine.h:65 | struct |
| 19 | Schedule Trigger | include/trigger_engine.h:81 | struct |
| 20 | Expression Token | include/trigger_engine.h:103 | struct |
| 21 | Correlation Link (corr_link_t) | include/event_correlation.h:42 | struct |
| 22 | Correlation Matrix | include/event_correlation.h:59 | struct |
| 23 | Causal Chain | include/event_correlation.h:72 | struct |
| 24 | Temporal Cluster | include/event_correlation.h:86 | struct |
| 25 | Archive Record (arch_record_t) | include/event_archive.h:30 | struct |
| 26 | Retention Policy | include/event_archive.h:50 | struct |
| 27 | Time Partition | include/event_archive.h:62 | struct |
| 28 | Event Archive | include/event_archive.h:82 | struct |
| 29 | Archive Query | include/event_archive.h:97 | struct |
| 30 | Archive Statistics | include/event_archive.h:108 | struct |

## L2: Core Concepts (Complete)

| # | Concept | Implementation |
|---|---------|---------------|
| 1 | Event capture with time-bounded context | ef_init/ef_start/ef_close |
| 2 | Event lifecycle state machine | ef_status_t transitions |
| 3 | Parent-child event hierarchy | ef_add_child, parent/children pointers |
| 4 | Template-based event typing | ef_tmpl_instantiate |
| 5 | Event-driven notification delivery | notif_process_event |
| 6 | Message template substitution | notif_format_message |
| 7 | Multi-channel delivery routing | notif_channel_type_t, notif_deliver |
| 8 | Time-partitioned archival | arch_partition_t, arch_store |
| 9 | Trigger-based event generation | trig_engine_scan |
| 10 | Event correlation for root cause | corr_build_matrix |
| 11 | Temporal clustering of events | corr_dbscan_cluster |
| 12 | Causal chain discovery | corr_discover_chains |

## L3: Engineering Structures (Complete)

| # | Structure | Implementation |
|---|-----------|---------------|
| 1 | FNV-1a hash for attribute names | src/event_frame.c: fnv1a_32/fnv1a_64 |
| 2 | Linear-probe hash map (attributes) | ef_set_attribute O(1) expected |
| 3 | Ring-buffer active set | ef_active_set_t, O(1) add |
| 4 | GUID generation (timestamp+ctr) | src/event_frame.c: guid_generate |
| 5 | Binary search time partition lookup | src/event_archive.c: find_or_create_partition |
| 6 | Shunting-yard expression compiler | src/trigger_engine.c: trig_compile_expression |
| 7 | RPN expression stack evaluator | src/trigger_engine.c: trig_eval_expression |
| 8 | CRC32 checksum computation | src/event_archive.c: crc32_compute |
| 9 | Template registry hash map | src/event_template.c: FNV-1a + linear probe |
| 10 | Compaction with record shifting | src/event_archive.c: arch_compact_partition |

## L4: Engineering Laws & Standards (Complete)

| # | Standard | Coverage |
|---|----------|----------|
| 1 | ISA-106 §5 Event Frame definition | event_frame.h typedefs and lifecycle |
| 2 | ISA-106 §5.1 Template definition | event_template.h, ef_tmpl_* API |
| 3 | ISA-106 §5.3 Event start/end | ef_start, ef_close with invariant |
| 4 | ISA-106 §6 Data retention | arch_set_retention_policy, arch_enforce_retention |
| 5 | ISA-18.2 §8 Alarm state detection | Trigger engine with hysteresis |
| 6 | ISA-18.2 §9.3 Acknowledgment | ef_acknowledge with user/comment/timestamp |
| 7 | ISA-18.2 §10 Annunciation | Notification engine with rules and channels |
| 8 | IEC 62682 Alarm management | Severity classification, escalation ready |
| 9 | IEC 61508 Data integrity | CRC32 checksum for archival records |

## L5: Algorithms & Methods (Complete)

| # | Algorithm | Complexity | Source |
|---|-----------|------------|--------|
| 1 | FNV-1a hash (32/64-bit) | O(n) | src/event_frame.c |
| 2 | GUID v7-style generation | O(1) | src/event_frame.c |
| 3 | Exponential Moving Average filter | O(1) | src/trigger_engine.c |
| 4 | Schmitt trigger hysteresis | O(1) | src/trigger_engine.c |
| 5 | Shunting-yard infix-to-RPN | O(n) | src/trigger_engine.c |
| 6 | RPN stack evaluation | O(n) | src/trigger_engine.c |
| 7 | Template instantiation | O(a+d) | src/event_template.c |
| 8 | Template validation | O(r) | src/event_template.c |
| 9 | Message template interpolation | O(n+a) | src/notification.c |
| 10 | CRC32 checksum | O(n) | src/event_archive.c |
| 11 | Jaccard index (attributes) | O(min(m,n)) | src/event_correlation.c |
| 12 | DBSCAN temporal clustering | O(n2) | src/event_correlation.c |
| 13 | Granger causality proxy | O(n) | src/event_correlation.c |
| 14 | Causal chain DFS traversal | O(V+E) | src/event_correlation.c |
| 15 | Binary search partition lookup | O(log P) | src/event_archive.c |

## L6: Canonical Problems (Complete)

| # | Problem | Example |
|---|---------|---------|
| 1 | Equipment downtime tracking | examples/example_downtime.c |
| 2 | Batch production event hierarchy (ISA-88) | examples/example_batch.c |
| 3 | Critical alarm notification delivery | examples/example_notification.c |

## L7: Industrial Applications (Complete)

| # | Application | Reference |
|---|-------------|-----------|
| 1 | OSIsoft PI Event Frames | Full API mirror of PI AF SDK EventFrame |
| 2 | PI Notifications 2020 | Rule-based notification with multi-channel |
| 3 | PI Data Archive integration | Time-partitioned archival with CRC integrity |
| 4 | Siemens WinCC Alarm Management | ISA-18.2 severity classification |

## L8: Advanced Topics (Partial)

| # | Topic | Status |
|---|-------|--------|
| 1 | DBSCAN temporal clustering | Implemented |
| 2 | Granger causality proxy for event streams | Implemented |
| 3 | Causal chain discovery (graph DFS) | Implemented |
| 4 | Bayesian network event modeling | Documented only |
| 5 | Transfer entropy for event streams | Documented in event_correlation.h |

## L9: Research Frontiers (Partial)

| # | Topic | Status |
|---|-------|--------|
| 1 | IT/OT event fusion | Documented in course-tree.md |
| 2 | Autonomous event response | Documented (L4 autonomous framework) |
| 3 | ML-based event prediction | Architecture notes in event_correlation.h |
