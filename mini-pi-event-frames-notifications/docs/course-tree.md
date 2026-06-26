# Course Tree — mini-pi-event-frames-notifications

## Prerequisite Knowledge

```
mini-industrial-real-time-database (parent module)
├── mini-pi-event-frames-notifications (this module)
│   ├── event_frame.h          ← Requires: time.h, stdint.h
│   ├── event_template.h       ← Requires: event_frame.h
│   ├── notification.h         ← Requires: event_frame.h
│   ├── trigger_engine.h       ← Requires: event_frame.h
│   ├── event_correlation.h    ← Requires: event_frame.h
│   └── event_archive.h        ← Requires: event_frame.h
├── Requires:
│   ├── Basic C data structures (struct, union, enum)
│   ├── Hash functions (FNV-1a)
│   ├── State machines
│   └── Graph traversal algorithms
└── Dependencies on external modules:
    ├── mini-industrial-communication-protocol (Modbus, OPC UA channels)
    └── mini-safety-instrumented-system (IEC 61508 data integrity)
```

## Knowledge Dependency Tree

```
L1 Definitions
 ├─ Event Frame, Status, Severity, Trigger
 ├─ Template, Attribute Definition
 ├─ Notification Rule, Channel, Recipient
 ├─ Trigger State, Direction, Token
 ├─ Correlation Link, Causal Chain
 └─ Archive Record, Retention Policy

L2 Core Concepts
 ├─ Event lifecycle state machine
 ├─ Template instantiation (class→object)
 ├─ Event-driven notification routing
 ├─ Trigger evaluation cycle
 ├─ Temporal event correlation
 └─ Time-partitioned archival

L3 Engineering Structures
 ├─ FNV-1a hash map (attributes, registry)
 ├─ Ring buffer active set
 ├─ Shunting-yard expression compiler
 ├─ RPN stack evaluator
 ├─ CRC32 checksum
 └─ Binary search partition index

L4 Standards ← Requires L1-L3
 ├─ ISA-106: Event Frames, Templates, Lifecycle
 ├─ ISA-18.2: Alarm State, Acknowledgment, Annunciation
 ├─ IEC 62682: Alarm Management
 └─ IEC 61508: Data Integrity

L5 Algorithms ← Requires L1-L3
 ├─ Schmitt trigger (hysteresis threshold)
 ├─ EMA filter (noise suppression)
 ├─ DBSCAN (temporal clustering)
 ├─ Granger causality proxy
 ├─ DFS chain discovery
 └─ Jaccard index (attribute similarity)

L6 Canonical Problems ← Requires L1-L5
 ├─ Equipment downtime (example_downtime.c)
 ├─ Batch hierarchy (example_batch.c)
 └─ Alert notification (example_notification.c)

L7 Industrial Applications ← Requires L1-L6
 ├─ OSIsoft PI AF Event Frames
 ├─ PI Notifications 2020
 ├─ PI Data Archive
 └─ Siemens WinCC / Rockwell FactoryTalk

L8 Advanced Topics ← Requires L1-L7
 ├─ DBSCAN clustering [implemented]
 ├─ Granger causality proxy [implemented]
 ├─ Causal chain discovery [implemented]
 └─ Bayesian networks [documented]

L9 Research Frontiers ← Requires L1-L8
 ├─ IT/OT event fusion [documented]
 ├─ Autonomous event response [documented]
 └─ ML-based prediction [documented]
```
