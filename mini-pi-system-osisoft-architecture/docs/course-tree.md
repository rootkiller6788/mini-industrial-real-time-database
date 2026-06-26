# Course Dependency Tree

```
PI System Architecture
├── PI Data Archive Core
│   ├── Timestamp Operations
│   ├── Point Type System
│   └── Status Code Model
├── Data Pipeline
│   ├── Snapshot Subsystem
│   ├── Archive Subsystem
│   ├── Compression Algorithms
│   └── Event Pipeline
├── Management
│   ├── Point Database (PIPOINT)
│   ├── System Management
│   ├── Security Model
│   └── Audit Trail
└── High Availability
    ├── Buffer Subsystem
    ├── Collective Operations
    └── License Management

Prerequisites:
- mini-industrial-measurement-actuator (sensor data)
- mini-pid-control-engineering (loop data)
- mini-plc-iec61131-fundamentals (PLC tag data)
```
