# mini-pi-integrator-opcua-mqtt

PI Integrator for OPC UA and MQTT -- Industrial Real-Time Data Connectivity

## Module Status: COMPLETE ✅

- **L1 Definitions**: Complete
- **L2 Core Concepts**: Complete
- **L3 Engineering Structures**: Complete
- **L4 Engineering Laws/Standards**: Complete
- **L5 Algorithms/Methods**: Complete
- **L6 Canonical Problems**: Complete
- **L7 Industrial Applications**: Complete
- **L8 Advanced Topics**: Complete
- **L9 Industry Frontiers**: Partial (documented)

### Line Count
`include/` + `src/` total C code: 3846 lines (threshold: 3000)

### Core Definitions (L1)
1. `pi_opcua_node_id_t` - OPC UA NodeId (IEC 62541-3)
2. `pi_opcua_variant_t` - OPC UA Variant (25+ built-in types)
3. `pi_mqtt_message_t` - MQTT message with MQTT 5.0 extensions
4. `pi_point_config_t` - PI Point full configuration
5. `pi_af_element_t` / `pi_af_attribute_t` - PI AF Asset Framework
6. `pi_transfer_object_t` - Data Transfer Object (JSON/CSV/Binary)
7. `pi_type_mapping_entry_t` - Cross-system type mapping
8. `sparkplug_edge_node_t` - Sparkplug B Edge Node

### Core Theorems (L4, Lean 4)
1. Data Quality partial order: reflexive, transitive, antisymmetric
2. Deadband filter: symmetric, zero-triggers-always
3. Session lifecycle: acyclic transition invariant
4. Sparkplug B state: no dead-to-unborn transition
5. Variant type compatibility: reflexive

### Core Algorithms (L5)
1. Deadband Filter (Swinging Door extension)
2. Store-and-Forward (Exponential Backoff)
3. Token Bucket Rate Limiter
4. Circuit Breaker (3-state pattern)
5. Adaptive Batch Control (EMA-based feedback)
6. Priority Queue (Binary Heap)
7. Ring Buffer
8. Timer Wheel (O(1) scheduling)

### Canonical Problems (L6)
1. PI Tag -> OPC UA Variable Mapping (`example_opcua_bridge.c`)
2. PI Archive -> MQTT Streaming (`example_mqtt_streaming.c`)
3. Pipeline Data Processing (`example_integrator_pipeline.c`)

### University Alignment
| School | Course | Topic |
|--------|--------|-------|
| MIT | 6.302 | Networked control over OPC UA |
| Stanford | EE392 | IoT streaming via MQTT |
| CMU | 24-677 | Distributed data transport |
| RWTH Aachen | Industrie 4.0 | OPC UA + MQTT integration |

### Build
```
make          # Build library
make test     # Build and run all tests
make examples # Build example programs
make clean    # Clean
```

*Part of mini-control-engineering-practice / mini-industrial-real-time-database*
