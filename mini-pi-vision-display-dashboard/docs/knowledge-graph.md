# Knowledge Graph — PI Vision Display Dashboard

## L1: Definitions (Complete)

| Concept | C Type | Lean Type | Status |
|---------|--------|-----------|--------|
| Display | `pv_display_t` | - | Complete |
| DisplayElement | `pv_display_element_t` | `ElementTree` | Complete |
| DataBinding | `pv_data_binding_t` | - | Complete |
| TimeRange | `pv_time_range_t` | - | Complete |
| Coordinate | `pv_coord_t` | `Coord` | Complete |
| Rectangle | `pv_rect_t` | `Rect` | Complete |
| Color (RGBA) | `pv_color_t` | `Color` | Complete |
| Symbol Types (17) | `pv_symbol_type_t` | `SymbolType` | Complete |
| Display State (5) | `pv_display_state_t` | `DisplayState` | Complete |
| Resolution Mode (8) | `pv_resolution_mode_t` | - | Complete |
| Navigation Level (4) | `pv_navigation_level_t` | `NavigationLevel` | Complete |
| Value Symbol | `pv_value_symbol_t` | - | Complete |
| Trend Symbol | `pv_trend_symbol_t` | - | Complete |
| Bar Graph Symbol | `pv_bar_graph_symbol_t` | - | Complete |
| Gauge Symbol | `pv_gauge_symbol_t` | - | Complete |
| State Indicator | `pv_state_indicator_t` | - | Complete |
| KPI Indicator | `pv_kpi_indicator_t` | - | Complete |
| Alarm Record/List | `pv_alarm_record_t` | - | Complete |
| Dashboard | `pv_dashboard_t` | - | Complete |
| Grid Layout | `pv_grid_layout_t` | - | Complete |
| Color Palette | `pv_color_palette_t` | - | Complete |
| Render Command | `pv_render_command_t` | - | Complete |

## L2: Core Concepts (Complete)

- Display hierarchy with parent-child element trees
- Real-time data subscription and event-driven updates
- Data binding to PI Points and AF Attributes
- Multi-scale time range management (absolute/relative)
- Symbol lifecycle management (factory pattern)
- Display state machine (loading/active/paused/stale/error)
- Dashboard layout with grid-based positioning
- Responsive coordinate system (percentage-based)
- Z-order rendering with element sorting

## L3: Engineering Structures (Complete)

- Circular buffer for streaming trend data (O(1) push)
- Display element linked list with recursive traversal
- Coordinate transformation (percentage ↔ pixel)
- Grid cell to rectangle mapping
- Rectangle hit testing and overlap detection
- Render command queue with pass-based sorting
- Display version counter for delta optimization
- Gauge geometry (angle ↔ value conversion)
- Trend coordinate mapping (time/pixel, value/pixel)
- Render configuration builder

## L4: Engineering Standards (Complete)

- ISA-101.01-2015 display navigation hierarchy (Levels 1-4)
- ISA-101 High Performance HMI principles
- ISA-101 alarm color standards (red/orange/yellow/blue)
- ISA-101 element density limits per navigation level
- ISA-101 color palette (gray background, limited colors)
- ISA-101 brightness/contrast validation (WCAG relative luminance)
- ISA-101 text/label requirements
- ISA-101 recommended refresh rates per level
- ISA-101 layout zone separation
- ISA-18.2 alarm severity levels
- ISA-101 color count limitation (≤6 non-gray colors)

## L5: Algorithms/Methods (Complete)

- Douglas-Peucker polyline simplification (1973)
- Xiaolin Wu anti-aliased line rendering (1991)
- Moving average filter for trend smoothing
- Linear interpolation between trend points
- Rate-of-change calculation (central differences)
- Min/max envelope computation for shaded trends
- Merge sort for display elements by z-order
- Porter-Duff alpha compositing (color blending)
- HSV to RGB color conversion
- Nice number calculation for gauge tick marks
- Bubble sort for alarm severity ordering
- Render cache with FIFO eviction
- Adaptive resolution selection
- Damage region union tracking
- Uniform subsampling fallback for decimation

## L6: Canonical Problems (Complete)

- Process overview dashboard (ISA-101 Level 1)
- Multi-pen trend display with decimation
- Alarm summary dashboard with ISA-18.2 severity
- KPI indicator with sparkline
- Asset-relative display cloning
- Navigation breadcrumb generation

## L7: Industrial Applications (Partial)

- OSIsoft PI Vision display model
- PI Data Archive tag binding
- PI Asset Framework element/attribute paths
- PI System data quality (OPC UA mapping)
- CSV data export for external analytics

## L8: Advanced Topics (Partial)

- Adaptive resolution rendering per time range
- Display cache hit rate optimization
- Render statistics monitoring
- Anti-aliased line rendering with sub-pixel accuracy

## L9: Industry Frontiers (Documented)

- IT/OT convergence dashboard concepts
- Industrial control system display standards
- Situational awareness through high-performance HMI
