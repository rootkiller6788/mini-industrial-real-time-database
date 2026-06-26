# Course Dependency Tree - mini-pi-asset-framework-af

## Prerequisites
```
Basic C Programming
  -> Data Structures (lists, trees, hash maps)
      -> mini-pi-asset-framework-af
          -> mini-pi-system-osisoft-architecture
          -> mini-pi-event-frames-notifications
          -> mini-pi-asset-analytics-af-analytics
```

## Internal Dependency Graph
```
af_element.h/c (Base: hierarchy node)
  -> af_attribute.h/c (Data: values on elements)
  -> af_template.h/c (Pattern: reusable shapes)
  -> af_category.h/c (Classification: tagging)
  -> af_enumset.h/c (Domain: value constraints)
  -> af_data_reference.h/c (Pipeline: value sources)
  -> af_search.h/c (Query: finding elements)
  -> af_event_frame.h/c (Time: event capture)
```

## Knowledge Layer Dependencies
- L1 -> L2: Definitions required before core concepts
- L2 -> L3: Concepts provide semantics for structures
- L3 -> L4: Structures must conform to standards
- L4 -> L5: Standards inform algorithm requirements
- L5 -> L6: Algorithms solve canonical problems
- L6 -> L7: Problems map to industrial applications
- L7 -> L8: Applications create data for analytics
- L8 -> L9: Analytics enable frontier technologies

## L9 Frontier Topics (Documented)
- IT/OT Fusion: AF bridges enterprise IT (ERP, MES) with OT (PLC, DCS)
- Digital Twin: AF asset models serve as digital twin backbone
- Industrial 5G: Low-latency AF updates via 5G TSN
- Autonomous Operations L4: Self-configuring AF templates with AI
- Zero-Trust Security: AF-level access control per asset/attribute
