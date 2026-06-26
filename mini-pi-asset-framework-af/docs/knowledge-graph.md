# Knowledge Graph - mini-pi-asset-framework-af

## L1: Definitions (COMPLETE)
- [x] AFElement type (ISA-95 10-level hierarchy)
- [x] AFAttribute type (7 value types: Int32, Float64, String, DateTime, Boolean, Enum, ByteArray)
- [x] AFTemplate type (attribute template pattern with inheritance)
- [x] AFCategory type (classification tag with hierarchy)
- [x] AFEnumSet type (enumerated value domain)
- [x] AFEventFrame type (time-bounded event with severity)
- [x] AFDataReference types (8 DR types: None, PI Point, Formula, Table Lookup, String Builder, Constant, Attr Ref, Analysis)
- [x] AFEventSeverity enum (6 levels: Debug, Info, Warning, Minor, Major, Critical)
- [x] AFEventStatus enum (4 states: Active, Closed, Acknowledged, Cancelled)

## L2: Core Concepts (COMPLETE)
- [x] Asset hierarchy management (parent-child, cycle detection)
- [x] Template inheritance (base-derived chain, attribute override)
- [x] Category-based classification (many-to-many element-category)
- [x] Data reference pipeline (PI Point, Formula, Table Lookup, etc.)
- [x] Event frame lifecycle (active -> closed -> acknowledged state machine)
- [x] Enumeration value validation and retirement

## L3: Engineering Structures (COMPLETE)
- [x] AF path computation (absolute and relative, ISA-95 format)
- [x] Template inheritance chain resolution
- [x] Data reference resolution stages (parse -> resolve -> deliver)
- [x] Formula expression AST (infix -> RPN via shunting-yard)
- [x] Table lookup with linear interpolation
- [x] DFS hierarchy traversal for search

## L4: Engineering Laws/Standards (COMPLETE)
- [x] ISA-95 equipment hierarchy model enforcement (10-level types)
- [x] PI System naming conventions for AF paths
- [x] ISA-88 batch control state machine (Idle/Running/Held/Complete/Aborted)
- [x] Template version management
- [x] Capacity planning constraints

## L5: Algorithms/Methods (COMPLETE)
- [x] Dijkstra Shunting-yard algorithm (tokenize -> RPN)
- [x] RPN evaluation with variable resolution
- [x] Linear interpolation table lookup
- [x] Rollup analytics: avg, min, max, sum, count, stddev
- [x] Wildcard pattern matching (*, ?)
- [x] DFS search with compound criteria (name, template, category, type, attr)
- [x] Cycle detection in template inheritance graph
- [x] Relevance scoring for search result ranking
- [x] Template effective attribute merge (derived overrides base)

## L6: Canonical Problems (COMPLETE)
- [x] ISA-95 enterprise/site/area/unit/equipment hierarchy construction
- [x] Template-based repetitive equipment instantiation
- [x] Over-temperature event detection with hysteresis
- [x] Data reference resolution through pluggable pipeline

## L7: Industrial Applications (COMPLETE)
- [x] OSIsoft PI AF SDK patterns (element, attribute, template, category)
- [x] ISA-95 equipment model compliance
- [x] PI System path conventions (\Enterprise\Site\Area format)
- [ ] Honeywell Experion asset model integration
- [ ] Rockwell AssetCentre integration

## L8: Advanced Topics (PARTIAL)
- [x] Statistical process control (SPC Western Electric rules via analysis DR)
- [x] Batch state machine (ISA-88 formalization in Lean 4)
- [ ] Bayesian anomaly detection on event frames
- [ ] Predictive maintenance trigger optimization
- [ ] Multi-site federation patterns

## L9: Research Frontiers (PARTIAL)
- [ ] IT/OT fusion architecture (documented in course-tree)
- [ ] Digital twin enablement (documented in course-tree)
- [ ] Industrial 5G integration patterns
- [ ] Autonomous operations L4 (documented)
- [ ] Zero-trust asset security model
