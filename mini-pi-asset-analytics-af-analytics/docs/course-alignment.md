# Course Alignment — PI Asset Framework Analytics

## Nine-School Curriculum Mapping

| School | Course | AF Analytics Coverage |
|--------|--------|----------------------|
| **MIT** | 6.302 Feedback Systems · 2.171 Digital Control | Discrete-event scheduling supervisor, state machine models |
| **Stanford** | ENGR205 Process Control · EE392 Industrial AI | KPI rollup architecture, expression evaluation, performance analytics |
| **Berkeley** | ME233 Advanced Control · EE C128 Mechatronics | Sliding window filtering, interpolation, sensor data fusion |
| **CMU** | 24-677 Advanced Control Systems · 18-771 Linear Systems | Priority queue scheduling, Kahnʼs topo sort, dependency graphs |
| **Georgia Tech** | ECE 6550 Nonlinear Control · AE 6530 Optimal Estimation | CUSUM change detection, Holt-Winters forecasting, Welford variance |
| **Purdue** | ECE 602 Lumped Systems · ME 575 Industrial Control | ISA-95 hierarchy, KPI dashboards, manufacturing analytics |
| **RWTH Aachen** | Industrial Control Systems · PLC/SCADA Engineering | OSIsoft PI AF patterns, event frame lifecycle, asset hierarchy |
| **Tsinghua** | Process Control Engineering · Industrial IoT | OEE calculation, batch event frames, ISA-88 alignment |
| **ISA/IEC** | ISA-88/95/101 · IEC 62541 (OPC UA) | Standards-compliant KPI definitions, event models, data quality |

## Detailed Course Mapping

### MIT 6.302 — Feedback Systems
- **Analytics scheduling as DES**: Each analytic is a discrete event; the scheduler determines execution order. Priority queue min-heap = event queue in DES simulation.
- **Dependency resolution**: Topological sort ensures upstream analytics complete before downstream ones execute — analogous to block diagram signal flow.

### Stanford ENGR205 — Process Control
- **KPI Performance Monitoring**: The KPI evaluation engine implements the ISO 22400 model of target/actual/threshold comparison with traffic-light status.
- **Hierarchical rollup**: Bottom-up aggregation through asset tree mirrors plant-wide control performance assessment.

### CMU 24-677 — Advanced Control Systems
- **Priority queue scheduling**: Real-time systems require O(log n) scheduling decisions. Min-heap provides optimal complexity.
- **Kahn's algorithm**: Correct dependency ordering prevents deadlocks in analytic evaluation chains.

### Georgia Tech ECE 6550 — Nonlinear Control
- **CUSUM for fault detection**: Statistical process control for detecting process shifts (efficiency degradation, sensor drift).
- **Holt-Winters forecasting**: Triple exponential smoothing decomposes time series for predictive analytics.

### RWTH Aachen — Industrial Control Systems
- **PI AF SDK patterns**: The module models the exact architecture of OSIsoft PI AF Analytics Service — templates, instances, scheduling types, backfilling.
- **Event frame lifecycle**: Matches PI AF event frame state machine (Idle→Active→Closed→Acknowledged).

### ISA/IEC Standards
- **ISA-95 Part 2 §4.1**: Asset categories from Enterprise down to Sensor.
- **ISA-88 Part 1**: Event frames as batch phase boundaries.
- **ISO 22400-2:2014**: KPI definitions (OEE, Availability, Performance, Quality) with threshold semantics.
- **OPC UA Part 11 §6.4**: Time-weighted average (TimeAverage aggregate), aggregate function semantics.
