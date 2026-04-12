# SoC / SoH Pipeline Architecture

This document describes the design and integration of the State of Charge (SoC) and State of Health (SoH) pipeline in the Battery Management System (BMS) data logger.

## Objective

The objective of this pipeline is to retrieve historical, processed telemetry data (typically from a database like InfluxDB), guarantee strict sequencing, and dispatch the data to downstream modules dedicated to estimating SoC and SoH. This serves as the architectural foundation to allow algorithms to be plugged in seamlessly in the future without disrupting existing tasks or rewriting core scheduling infrastructure.

## Pipeline Stages

The architecture defines three major stages:

1. **Database Consumer Task** (`DatabaseConsumerTask` in `db_consumer.cpp`):
   Responsible for querying the database at regular intervals, applying backoff strategies during empty polls, and retrieving ordered telemetry data based on a defined sequence or timestamp.
2. **SoC Task** (`SoCTask` in `soc.cpp`):
   A dedicated worker task designed to consume rows of `TelemetryRow` strictly ordered and process them. Currently containing stub placeholders (`TODO`s), this isolates the SoC computation from the rest of the system.
3. **SoH Task** (`SoHTask` in `soh.cpp`):
   Similar to the SoC task, it operates independently on a duplicate data stream to compute the long-term State of Health algorithmically.

## Task Responsibilities and Data Flow

- The `DatabaseConsumerTask` allocates new `TelemetryRow` instances from a `TelemetryRowPool` (`BatchPool`) and populates them.
- To facilitate isolated processing without lock contention, each fetched row is dispatched to two `SafeQueue`s: one for the `SoCTask` and one for the `SoHTask`.
- The SoC and SoH tasks operate as `PeriodicTask` workers, independently pulling data from their respective lock-free queues. They are responsible for freeing the `TelemetryRow` memory back to the pool once processing is complete.

## Ordering Guarantees

Strict ordering is required for algorithm accuracy:
- The database query logic requests data ordered by a designated monotonic field (e.g., `sequence` or `timestamp`).
- The Consumer task tracks the `last_sequence_` and discards any out-of-order data before dispatching.
- The SoC and SoH tasks enforce sequence integrity on consumption. If a row arrives with a sequence number less than or equal to the highest sequence number processed so far, the row is discarded and an `out_of_order` diagnostic counter is incremented.

## Debug Strategy

The pipeline optimizes for observability by exposing atomic diagnostic counters directly to the `main.cpp` diagnostic loop:
- **DB Consumer**: Tracks total rows fetched, total query failures, and duplicates skipped.
- **SoC / SoH Tasks**: Track total rows processed, out-of-order sequence detections, and processing failures.

Developers can inspect standard output logs every 10 seconds to monitor the ingestion rate, queue depth, queue dropping, memory pool usage, and any out-of-sequence errors.

## Future Integration

When real SoC and SoH algorithms are implemented:
1. Locate the `TODO` markers in `SoCTask::process_row_` and `SoHTask::process_row_`.
2. Integrate your mathematical models. The `TelemetryRow` contains fields for voltages, currents, and temperatures.
3. If additional outputs need to be routed back to a database or sent over MQTT, the tasks can be modified to push result batches to an upstream producer queue, mirroring the existing system architecture.