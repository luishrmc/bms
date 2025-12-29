#!/usr/bin/env python3
from __future__ import annotations

import time
from datetime import datetime, timezone
from typing import Optional, Tuple, List

from pymodbus.client import ModbusTcpClient

# -----------------------------
# Hard-coded targets (your lab)
# -----------------------------
DEVICES = [
    ("192.168.7.2", 502, 1),
    ("192.168.7.200", 502, 1),
]

# Read device clock (INPUT registers, 3x) — device time snapshot
READ_TS_ADDR = 3
READ_TS_COUNT = 3  # 3,4 = epoch2000 (hi/lo), 5 = ms

# Write host time to device (typically HOLDING registers, 4x)
# You told: 290,291 are the writable epoch registers
WRITE_EPOCH_ADDR = 290
WRITE_EPOCH_COUNT = 2  # hi, lo

# If your device ALSO supports writing ms in an adjacent register (common pattern),
# set this to True and adjust WRITE_MS_ADDR accordingly.
WRITE_MS_ENABLED = False
WRITE_MS_ADDR = 292  # common guess; confirm with your register map


EPOCH_2000_UNIX_OFFSET = 946684800  # seconds between 1970-01-01 and 2000-01-01


def words_to_u32(hi: int, lo: int) -> int:
    return ((hi & 0xFFFF) << 16) | (lo & 0xFFFF)


def u32_to_words(value: int) -> Tuple[int, int]:
    hi = (value >> 16) & 0xFFFF
    lo = value & 0xFFFF
    return hi, lo


def device_epoch2000_to_datetime(epoch2000_s: int, ms: int) -> datetime:
    unix_s = EPOCH_2000_UNIX_OFFSET + int(epoch2000_s)
    return datetime.fromtimestamp(unix_s, tz=timezone.utc).replace(microsecond=int(ms) * 1000)


def iso8601_z(dt: datetime) -> str:
    # Format: YYYY-MM-DDThh:mm:ss.mmmZ
    # (milliseconds precision; adjust if you prefer seconds only)
    return dt.strftime("%Y-%m-%dT%H:%M:%S.") + f"{dt.microsecond // 1000:03d}Z"


def read_device_timestamp(client: ModbusTcpClient, unit: int) -> Optional[Tuple[int, int, datetime]]:
    rr = client.read_input_registers(address=READ_TS_ADDR, count=READ_TS_COUNT)
    if rr.isError():
        return None

    regs = rr.registers
    if len(regs) < 3:
        return None

    epoch2000_s = words_to_u32(regs[0], regs[1])
    ms = regs[2] & 0xFFFF
    dt = device_epoch2000_to_datetime(epoch2000_s, ms)
    return epoch2000_s, ms, dt


def host_time_as_device_epoch2000() -> Tuple[int, int]:
    now = time.time()  # seconds since Unix epoch, float
    unix_s = int(now)
    ms = int((now - unix_s) * 1000.0)

    epoch2000_s = unix_s - EPOCH_2000_UNIX_OFFSET
    if epoch2000_s < 0:
        epoch2000_s = 0
    return int(epoch2000_s), int(ms)


def write_device_epoch(client: ModbusTcpClient, unit: int, epoch2000_s: int, ms: int) -> bool:
    hi, lo = u32_to_words(epoch2000_s)
    wr = client.write_registers(address=WRITE_EPOCH_ADDR, values=[hi, lo])
    if wr.isError():
        return False

    if WRITE_MS_ENABLED:
        wr2 = client.write_registers(address=WRITE_MS_ADDR, values=[ms & 0xFFFF])
        if wr2.isError():
            return False

    return True


def sync_one_device(host: str, port: int, unit: int) -> None:
    print(f"\n=== Device {host}:{port} unit={unit} ===")
    client = ModbusTcpClient(host=host, port=port, timeout=2.0)

    if not client.connect():
        print("[FAIL] connect()")
        client.close()
        return

    try:
        before = read_device_timestamp(client, unit)
        if before is None:
            print("[FAIL] read_input_registers(3..5)")
            return

        before_epoch, before_ms, before_dt = before
        print(f"[BEFORE] {iso8601_z(before_dt)}  (device: {before_epoch}s + {before_ms}ms)")

        # Compute host time (UTC) in device epoch2000 format
        host_epoch, host_ms = host_time_as_device_epoch2000()
        host_dt = device_epoch2000_to_datetime(host_epoch, host_ms)
        print(f"[HOST  ] {iso8601_z(host_dt)}  (host->device: {host_epoch}s + {host_ms}ms)")

        ok = write_device_epoch(client, unit, host_epoch, host_ms)
        if not ok:
            print("[FAIL] write_registers(290..291) — check if these are holding registers (4x) and writable.")
            return

        # Give the device a brief moment if it applies time asynchronously
        time.sleep(0.2)

        after = read_device_timestamp(client, unit)
        if after is None:
            print("[WARN] read after write failed; device may require different mapping/function.")
            return

        after_epoch, after_ms, after_dt = after
        print(f"[AFTER ] {iso8601_z(after_dt)}  (device: {after_epoch}s + {after_ms}ms)")

    finally:
        client.close()


def main() -> None:
    print("Modbus RTC sync (pymodbus 3.11.4) — single-shot, hard-coded targets.")
    print(f"Reading input regs: {READ_TS_ADDR}..{READ_TS_ADDR+READ_TS_COUNT-1}")
    print(f"Writing regs:       {WRITE_EPOCH_ADDR}..{WRITE_EPOCH_ADDR+WRITE_EPOCH_COUNT-1}"
          + (" + ms" if WRITE_MS_ENABLED else ""))

    for host, port, unit in DEVICES:
        sync_one_device(host, port, unit)


if __name__ == "__main__":
    main()
