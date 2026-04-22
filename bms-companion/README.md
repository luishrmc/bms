# bms-companion

## Project Overview

`bms-companion` is a Linux-first C++20 companion service for a BMS laboratory setup.
It links three operational paths:

- battery pack telemetry acquisition over RS485 / Modbus RTU
- Regatron/eStorage control and feedback over CAN
- MQTT-based remote command and status exchange

The executable target is `bms_companion` (source in `bms-companion/`).

## Why This Application Exists

The service is intentionally simple and operational:

- small concrete task classes
- one responsibility per task
- shared latest-state objects between tasks
- robust reconnect/retry loops
- explicit, readable finite-state behavior for cycling

It is not a framework. It is a runtime companion process for lab operation.

## High-Level Architecture

`main` composes four long-running tasks on separate threads:

1. `RS485Task`: polls battery registers and updates `LatestBatteryState`
2. `MQTTTask`: publishes latest battery snapshot to MQTT
3. `MQTTControlTask`: receives Regatron control commands and publishes Regatron status
4. `RegatronTask`: runs Regatron CAN I/O and FSM logic

Shared state objects:

- `LatestBatteryState`: latest decoded battery snapshot (thread-safe copy access)
- `RegatronCommandState`: command mailbox updated by MQTT control
- `LatestRegatronState`: latest Regatron runtime status (thread-safe copy access)

## Main Runtime Components

### `RS485Task` (`bms-companion/src/rs485_task.cpp`)

- connects to Modbus RTU (`/dev/ttyUSB0`, slave `1` by default)
- reads 125 holding registers starting at address `0`
- decodes to `BatterySnapshot` through `ModbusCodec`
- updates `LatestBatteryState` every poll cycle (`1000 ms` default)
- reconnects after read/connect errors

### `MQTTTask` (`bms-companion/src/mqtt_task.cpp`)

- connects to broker (`tcp://mosquitto:1883` default)
- every `5000 ms`, fetches latest battery snapshot and publishes JSON to:
  - `bms/battery/snapshot` (QoS 1, retained by default)
- reconnects on broker/publish failures

### `MQTTControlTask` (`bms-companion/src/mqtt_control_task.cpp`)

- subscribes to `bms/regatron/cmd/#`
- updates `RegatronCommandState` from incoming command payloads
- publishes Regatron status topics under `bms/regatron/status/*` every 1 second
- publishes retained scalar topics and a retained JSON `summary`

### `RegatronTask` (`bms-companion/src/regatron_task.cpp`)

- opens CAN RAW socket on `can0` (default)
- decodes feedback frames into `RegatronStatusSnapshot`
- consumes command pulses/parameters from `RegatronCommandState`
- uses latest battery state for safety and cycle transition decisions
- emits CAN command frames at:
  - base loop: every `10 ms` (`base_period_ms`)
  - supervisory group: every `100 ms`
  - cycle-time group: every `1 s`

## Startup To Operation Flow

Runtime flow in current code:

1. process starts; SIGINT/SIGTERM handlers set global `g_running=false` on shutdown signal
2. `main` creates shared states (`LatestBatteryState`, `RegatronCommandState`, `LatestRegatronState`)
3. task configs are instantiated with in-code defaults
4. four worker threads are started (`RS485Task`, `MQTTTask`, `MQTTControlTask`, `RegatronTask`)
5. `RS485Task` connects and polls battery Modbus registers continuously
6. decoded battery snapshot updates `LatestBatteryState`
7. `MQTTTask` publishes battery snapshot JSON periodically
8. `MQTTControlTask` receives Regatron command messages and updates `RegatronCommandState`
9. `RegatronTask` runs FSM transitions, sends cyclic CAN control frames, decodes CAN feedback
10. `RegatronTask` writes latest status to `LatestRegatronState`; `MQTTControlTask` publishes status topics
11. on shutdown signal, loops exit, sockets/clients are disconnected, threads are joined

## Data And Control Flow

### Battery Communication Flow (RS485 / Modbus RTU)

Path:

1. `ModbusCodec::connect()` opens RTU context with configured serial parameters
2. `read_snapshot()` reads configured register block (`read_register_count`, default `125`)
3. selected registers are decoded into engineering fields (`voltage`, `SOC`, alarms, temperatures, etc.)
4. full raw register image is preserved in `BatterySnapshot::raw_registers`
5. `LatestBatteryState::update()` stores the new snapshot for other tasks

Notes from current implementation:

- `pack_voltage_v = reg[0] * 0.01`
- `pack_current_a` is only populated when `current_scale_a_per_lsb > 0` (default is `0.0`, so raw current is used)
- serial/model/manufacturer strings are decoded from register text regions

### Regatron Communication Flow (CAN)

Receive/decode path:

- `ID_ACT_U_I (161)`: actual voltage/current
- `ID_ACT_SET_U_I (264)`: actual setpoints
- `ID_ACT_PWR_SWITCH_CTRL (225)`: actual power, control mode, switch state
- `ID_ACT_FAULT (1032)`: fault code/message id

Transmit/control path:

- fast path (every loop): `ID_SET_U_I (177)`, `ID_SET_SWITCH_CTRL (193)`
- 10 Hz group: `ID_SET_LIM_MX (352)`, `ID_SET_LIM_MN (368)`, `ID_SET_SUPV_MX (276)`, `ID_SET_SUPV_MN (308)`, `ID_SET_SLOPE (324)`, `ID_CLEARANCE (1824)`, and pulse `ID_SET_VIRTUAL_IN (543)` when clear-fault is requested
- 1 Hz group: `ID_SET_CYCLE_TIME (1855)`

`RegatronTask` writes the resulting status snapshot to `LatestRegatronState`.

### MQTT Command/Status Flow

Command intake:

- subscriber: `bms/regatron/cmd/#`
- preferred parameter topic: `bms/regatron/cmd/set` (JSON object, partial updates allowed)
- action topic: `bms/regatron/cmd/action` (`start`, `stop`, `clear_fault`)
- backward-compatible legacy per-topic command parsing is still supported

Status publication (retained):

- base: `bms/regatron/status/`
- scalar topics include:
  - `state`
  - `actual_voltage_v`
  - `actual_current_a`
  - `actual_power_kw`
  - `actual_switch`
  - `fault_active`
  - `fault_code`
  - `fault_msg_id`
- JSON summary topic:
  - `summary`

Battery publication:

- topic: `bms/battery/snapshot`
- payload: JSON snapshot from latest battery data
- QoS: `1`
- retained: `true`

### Database Interaction

`bms_companion` currently has no direct database writes.

- no InfluxDB/PostgreSQL client is used in companion runtime
- telemetry persistence is expected to be handled by external subscribers (for example MQTT consumers)

Repository note: there is another executable target (`bms`, under `app/`) with database-specific logic, but that is separate from `bms_companion`.

## Finite State Machine Behavior

FSM enum: `INIT`, `OFF`, `WAIT`, `STANDBY1`, `STANDBY2`, `CHARGE`, `DISCHARGE`, `ERROR`.

State behavior summary:

- `INIT`: immediate transition to `OFF`
- `OFF`: idle with zero commanded current; waits for `enable=true` and `start` pulse
- `WAIT`: delay before standby; stop/disable returns to `OFF`
- `STANDBY1`: requests source ON path; waits for switch feedback `ON` or times out to `ERROR`
- `STANDBY2`: stabilization delay before entering `CHARGE`
- `CHARGE`: positive commanded current; transitions to `DISCHARGE` by upper voltage, upper SOC, or cycle timer
- `DISCHARGE`: negative commanded current; transitions to `CHARGE` by lower voltage, lower SOC, or cycle timer
- `ERROR`: output current forced to zero; leaves error on disable or clear-fault pulse when no active fault remains

Safety-relevant transitions in `CHARGE` and `DISCHARGE`:

- any battery alarm/protection/protected-status forces transition to `OFF`
- active Regatron fault forces transition to `ERROR`
- stop/disable commands force transition to `OFF`

## MQTT Contract

### Regatron Set Topic (preferred)

Topic:

`bms/regatron/cmd/set`

Payload:

```json
{
  "enable": true,
  "charge_current_a": 8.0,
  "discharge_current_a": 8.0,
  "cycle_time_s": 1800,
  "voltage_limit_min_v": 44.0,
  "voltage_limit_max_v": 54.2,
  "current_limit_min_a": -10.0,
  "current_limit_max_a": 10.0,
  "soc_min_pct": 20.0,
  "soc_max_pct": 80.0
}
```

Partial update example:

```json
{ "charge_current_a": 5.0 }
```

### Regatron Action Topic

Topic:

`bms/regatron/cmd/action`

Accepted payloads:

- JSON object: `{"action":"start"}`, `{"action":"stop"}`, `{"action":"clear_fault"}`
- plain string: `start`, `stop`, `clear_fault`

## Build And Run

## Requirements

- Linux with C++20 toolchain and CMake >= 3.22
- Boost (`system`, `thread`, `chrono`)
- `libmodbus`
- Eclipse Paho MQTT C/C++ (`paho-mqtt3as`, `paho-mqttpp3`)
- `nlohmann_json`
- working serial device (default `/dev/ttyUSB0`)
- working SocketCAN interface (default `can0`)
- MQTT broker reachable at configured URI

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target bms_companion
```

By default the executable is generated at:

`bin/bms_companion`

## Runtime Setup

1. Ensure serial permissions and correct RS485 adapter mapping (`/dev/ttyUSB0` by default).
2. Bring up CAN interface externally (the app does not set bitrate):
   ```bash
   sudo ip link set can0 up type can bitrate 500000
   ```
3. Start an MQTT broker (example Mosquitto config exists at `config/mosquitto/config/mosquitto.conf`).
4. Run:
   ```bash
   ./bin/bms_companion
   ```

Current configuration is code-defined in `bms-companion/src/main.cpp` and config structs in `bms-companion/inc/*.hpp` (no CLI/env config layer yet).

## Cycling Behavior (High Level)

Once enabled and started, the FSM performs:

1. controlled entry sequence (`WAIT -> STANDBY1 -> STANDBY2`)
2. alternating charge/discharge phases
3. phase reversal when one of these triggers occurs:
   - cycle timer expires
   - SOC boundary reached
   - pack voltage boundary reached
4. immediate stop to `OFF` on battery protection/alarm

## Operational Notes And Safety Cautions

- `clear_fault` is a pulse command; it is consumed once by the FSM loop.
- `start`/`stop` are also pulse semantics (single-shot requests).
- default `current_scale_a_per_lsb` is `0.0`, so battery current is published as raw register value.
- CAN feedback timeout marks `can_online=false` and updates status info to `"CAN feedback timeout"`.
- no persistent command validation layer exists beyond payload parsing; use safe setpoints from upstream controller.

## Suggested Next Improvements

1. Add external configuration (file/env/CLI) for serial, CAN, and MQTT settings.
2. Add integration tests with recorded Modbus and CAN frames for FSM transition regression.
3. Add MQTT contract validation and command range checks before applying setpoints.
4. Add a dedicated status field for CAN timeout severity handling (for example forced transition policy).
