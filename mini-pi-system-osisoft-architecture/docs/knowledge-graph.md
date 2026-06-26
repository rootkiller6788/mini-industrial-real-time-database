# Knowledge Graph - OSIsoft PI System Architecture

## L1: Definitions (COMPLETE)
- PI Timestamp (FILETIME + subsec)
- PI Point Types (Digital, Int16, Int32, Float16, Float32, Float64, String, Timestamp, Blob)
- PI Status Codes (Good, Bad, Uncertain, Stale, Substituted, No Data, Pt Created, Shutdown)
- PI Point Attributes (40+ classic point attributes)
- PI Archive Event
- PI Snapshot Entry
- PI Subsystem States
- PI Collective Member
- PI Security Identity & Access Mapping
- PI License Types
- PI Alarm Types (HIHI, HI, LO, LOLO, RATE, DEVIATION)

## L2: Core Concepts (COMPLETE)
- Snapshot vs Archive distinction
- Exception & Compression testing pipeline
- Store-and-Forward buffering
- PI Collective N-way redundancy
- PI Server subsystem model
- Digital state management
- Time-series data flow

## L3: Engineering Structures (COMPLETE)
- Fixed-record archive format (24-byte records)
- Snapshot hash table organization
- Point database indexing (by tag, location, source)
- Circular buffer queue
- Collective election algorithm
- Security access control list

## L4: Standards (COMPLETE)
- PI SDK Programming Conventions
- PI API Status Code Standards
- ISA-5.1 Instrument Hierarchy (Location1-5)
- ISA-18.2 Alarm Management
- EEMUA 191 Alarm System Guidelines
- ISA/IEC 62443 Industrial Security
- PI Server Sizing Guidelines

## L5: Algorithms (COMPLETE)
- Swinging Door Compression (Bristol, 1990)
- Deadband (absolute & percentage)
- Ramer-Douglas-Peucker simplification
- Boxcar averaging & min-max resampling
- Linear interpolation & trapezoidal integration
- Runge-Kutta 4th order ODE integration
- Kahan compensated summation
- OLS linear regression
- IQR outlier detection
- EMA & SMA filtering

## L6: Canonical Problems (COMPLETE)
1. Snapshot-Archive Pipeline Demo
2. Buffer & Collective HA Demo
3. Point Configuration & System Health Demo

## L7: Applications (PARTIAL)
- PI System Management (SMT equivalent)
- Interface registration & monitoring
- Performance counter management
- License tracking

## L8: Advanced Topics (PARTIAL)
- Collective synchronization algorithms
- RDP polyline simplification for trend data
- Time-weighted average for irregular time series

## L9: Frontiers (PARTIAL)
- Cloud PI (AVEVA Data Hub) documented
- Digital twin integration documented
