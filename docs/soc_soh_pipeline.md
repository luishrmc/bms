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
   - Perform placeholder `process_row(const TelemetryRow&)` hooks.

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
  - Contains only scaffold/TODO extension point for future algorithm.
- **SoHTask**
  - Dedicated SoH pipeline worker and diagnostics surface.
  - Contains only scaffold/TODO extension point for future algorithm.

## Ordering guarantees
- Ordering field is explicit (`DBConsumerConfig::ordering_field`, default `cursor`).
- DB stage accepts only strictly increasing cursors (`row.cursor > checkpoint`).
- Duplicate or older rows are skipped and counted.
- Cursor gaps are detected (`expected checkpoint+1`) and logged.
- SoC and SoH each process rows sequentially in a single thread.

## Debug strategy
Use periodic and final diagnostics from `main.cpp` to answer:
- last consumed cursor (`DBConsumerDiagnostics::last_processed_cursor`)
- rows fetched/processed
- duplicates and out-of-order events
- query and processing failures
- latest observed ingestion latency (`last_latency_ms`)
- queue pressure (`pushed/popped/dropped`)

## Future algorithm integration
- Add real estimators only inside:
  - `SoCTask::process_row(const TelemetryRow&)`
  - `SoHTask::process_row(const TelemetryRow&)`
- Keep `TelemetryRow` stable as canonical input contract.
- Future work can evolve backend independently:
  - checkpoint persistence
  - replay modes
  - backend replacement or streaming transport
