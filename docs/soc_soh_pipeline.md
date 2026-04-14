# SoC/SoH Database Consumption Pipeline

## Pipeline stages
1. **DBConsumerTask (polling + ordering gate)**
   - Executes periodically using `PeriodicTask`.
   - Queries InfluxDB (`InfluxTelemetryQueryBackend`) for rows with `cursor > checkpoint`.
   - Requires ascending cursor order from backend query (`ORDER BY cursor ASC`).
   - Applies duplicate/old-row rejection and cursor-gap diagnostics.
2. **Fan-out to two FIFO queues**
   - Each accepted row is copied and enqueued to:
     - `soc_queue` (SoC stream)
     - `soh_queue` (SoH stream)
3. **SoCTask / SoHTask (independent consumers)**
   - Run as independent periodic workers.
   - Consume one ordered stream each (single-consumer FIFO semantics from `SafeQueue`).
   - Delegate row processing to injectable estimators that consume canonical `TelemetryRow` values.

## Task responsibilities
- **InfluxTelemetryQueryBackend**
  - Isolates query-transport specifics (`/api/v3/query_sql`) from task logic.
  - Converts query payload to canonical `TelemetryRow`.
- **DBConsumerTask**
  - Owns cursor checkpoint and ordering enforcement.
  - Detects and reports duplicates/old rows, missing cursor gaps, and query/fan-out failures.
  - Publishes only rows accepted by ordering contract.
- **SoCTask**
  - Dedicated SoC pipeline worker and diagnostics surface.
  - Owns queue consumption, ordering checks, and per-row latency/health diagnostics.
  - Delegates estimation behavior to `ISoCEstimator` (default: `NoOpSoCEstimator`).
- **SoHTask**
  - Dedicated SoH pipeline worker and diagnostics surface.
  - Owns queue consumption, ordering checks, and per-row latency/health diagnostics.
  - Delegates estimation behavior to `ISoHEstimator` (default: `NoOpSoHEstimator`).

## Ordering guarantees
- Ordering field is explicit (`DBConsumerConfig::ordering_field`, default `cursor`).
- DB stage accepts only strictly increasing cursors (`row.cursor > checkpoint`).
- Duplicate or older rows are skipped and counted.
- Cursor gaps are detected (`expected checkpoint+1`) and logged.
- SoC and SoH each process rows sequentially in a single thread.
- Estimators do not own cursor sequencing; they receive already-ordered canonical rows.

## Debug strategy
Use periodic and final diagnostics from `main.cpp` to answer:
- last consumed cursor (`DBConsumerDiagnostics::last_processed_cursor`)
- rows fetched/processed
- duplicates and out-of-order events
- query and processing failures
- latest observed ingestion latency (`last_latency_ms`)
- queue pressure (`pushed/popped/dropped`)
- estimator outcome context (`last_estimator_message`, rejection counters)

## Future algorithm integration
- Implement real algorithms behind interfaces:
  - `ISoCEstimator::estimate(const TelemetryRow&)`
  - `ISoHEstimator::estimate(const TelemetryRow&)`
- Keep `TelemetryRow` stable as canonical input contract.
- Keep SoC/SoH task ownership boundaries stable:
  - task = queueing, ordering, diagnostics
  - estimator = row interpretation/model math
- Future work can evolve backend independently:
  - checkpoint persistence
  - replay modes
  - backend replacement or streaming transport
