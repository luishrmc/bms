#!/usr/bin/env python3

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

import pandas as pd
from influxdb_client_3 import InfluxDBClient3
from influxdb_client_3.exceptions import InfluxDB3ClientQueryError  # type: ignore


# =========================
# InfluxDB configuration
# =========================
INFLUXDB_HOST = "http://192.168.7.3:8181"
INFLUXDB_TOKEN = "apiv3_j41nS69StsCFpdlvfV-LjHy4WPUxZiP1KKaHhmhwPKWsnuq70PgyuOnxmJlMcHXVAD7Gv-QhSBhUaEwEqBPHSA"
INFLUXDB_DATABASE = "battery_data"

VOLTAGE_TABLE = "voltage_current"
TEMPERATURE_TABLE = "temperature"

TIME_WINDOW = "1 min"

# Set these to the nominal acquisition rates you expect in the real system.
# They are optional. If set to None, missing-sample estimation is skipped.
EXPECTED_RATE_HZ = {
    VOLTAGE_TABLE: 7.0,      
    TEMPERATURE_TABLE: 1.0,
}


@dataclass
class SampleRateStats:
    table_name: str
    row_count: int
    elapsed_s: float
    avg_rate_hz: float
    mean_dt_s: float
    median_dt_s: float
    min_dt_s: float
    max_dt_s: float
    mean_rate_hz: float
    median_rate_hz: float
    expected_rate_hz: Optional[float]
    expected_samples: Optional[int]
    missing_samples: Optional[int]
    coverage_pct: Optional[float]


def query_time_column(client: InfluxDBClient3, table_name: str) -> pd.DataFrame:
    query = f"""
        SELECT time
        FROM {table_name}
        WHERE time >= now() - INTERVAL '{TIME_WINDOW}'
        ORDER BY time
    """

    df = client.query(
        query=query,
        language="sql",
        database=INFLUXDB_DATABASE,
    )

    if df is None:
        return pd.DataFrame(columns=["time"])

    if not isinstance(df, pd.DataFrame):
        df = df.to_pandas()  # type: ignore

    if "time" not in df.columns:
        return pd.DataFrame(columns=["time"])

    df = df.copy()
    df["time"] = pd.to_datetime(df["time"], errors="coerce")
    df = df.dropna(subset=["time"]).sort_values("time").reset_index(drop=True)
    return df


def analyze_sample_rate(
    client: InfluxDBClient3,
    table_name: str,
    expected_rate_hz: Optional[float] = None,
) -> SampleRateStats:
    df = query_time_column(client, table_name)
    row_count = len(df)

    if row_count == 0:
        return SampleRateStats(
            table_name=table_name,
            row_count=0,
            elapsed_s=0.0,
            avg_rate_hz=0.0,
            mean_dt_s=0.0,
            median_dt_s=0.0,
            min_dt_s=0.0,
            max_dt_s=0.0,
            mean_rate_hz=0.0,
            median_rate_hz=0.0,
            expected_rate_hz=expected_rate_hz,
            expected_samples=None,
            missing_samples=None,
            coverage_pct=None,
        )

    if row_count == 1:
        return SampleRateStats(
            table_name=table_name,
            row_count=1,
            elapsed_s=0.0,
            avg_rate_hz=0.0,
            mean_dt_s=0.0,
            median_dt_s=0.0,
            min_dt_s=0.0,
            max_dt_s=0.0,
            mean_rate_hz=0.0,
            median_rate_hz=0.0,
            expected_rate_hz=expected_rate_hz,
            expected_samples=1 if expected_rate_hz is not None else None,
            missing_samples=0 if expected_rate_hz is not None else None,
            coverage_pct=100.0 if expected_rate_hz is not None else None,
        )

    diffs_s = df["time"].diff().dropna().dt.total_seconds()

    elapsed_s = (df["time"].iloc[-1] - df["time"].iloc[0]).total_seconds()
    avg_rate_hz = (row_count - 1) / elapsed_s if elapsed_s > 0 else 0.0

    mean_dt_s = float(diffs_s.mean())
    median_dt_s = float(diffs_s.median())
    min_dt_s = float(diffs_s.min())
    max_dt_s = float(diffs_s.max())

    mean_rate_hz = (1.0 / mean_dt_s) if mean_dt_s > 0 else 0.0
    median_rate_hz = (1.0 / median_dt_s) if median_dt_s > 0 else 0.0

    expected_samples: Optional[int] = None
    missing_samples: Optional[int] = None
    coverage_pct: Optional[float] = None

    if expected_rate_hz is not None and expected_rate_hz > 0 and elapsed_s > 0:
        # Number of samples expected over the observed span, including the first sample
        expected_samples = int(round(elapsed_s * expected_rate_hz)) + 1
        missing_samples = max(expected_samples - row_count, 0)
        coverage_pct = (row_count / expected_samples) * 100.0 if expected_samples > 0 else None

    return SampleRateStats(
        table_name=table_name,
        row_count=row_count,
        elapsed_s=elapsed_s,
        avg_rate_hz=avg_rate_hz,
        mean_dt_s=mean_dt_s,
        median_dt_s=median_dt_s,
        min_dt_s=min_dt_s,
        max_dt_s=max_dt_s,
        mean_rate_hz=mean_rate_hz,
        median_rate_hz=median_rate_hz,
        expected_rate_hz=expected_rate_hz,
        expected_samples=expected_samples,
        missing_samples=missing_samples,
        coverage_pct=coverage_pct,
    )


def print_stats(stats: SampleRateStats) -> None:
    print(f"{stats.table_name}:")
    print(f"  Rows analyzed         : {stats.row_count}")
    print(f"  Time span             : {stats.elapsed_s:.6f} s")
    print(f"  Average rate          : {stats.avg_rate_hz:.6f} Hz")
    print(f"  Mean interval         : {stats.mean_dt_s:.6f} s")
    print(f"  Median interval       : {stats.median_dt_s:.6f} s")
    print(f"  Min interval          : {stats.min_dt_s:.6f} s")
    print(f"  Max interval          : {stats.max_dt_s:.6f} s")
    print(f"  Mean-diff rate        : {stats.mean_rate_hz:.6f} Hz")
    print(f"  Median-diff rate      : {stats.median_rate_hz:.6f} Hz")

    if stats.expected_rate_hz is not None:
        print(f"  Expected nominal rate : {stats.expected_rate_hz:.6f} Hz")

    if stats.expected_samples is not None:
        print(f"  Expected samples      : {stats.expected_samples}")

    if stats.missing_samples is not None:
        print(f"  Estimated missing     : {stats.missing_samples}")

    if stats.coverage_pct is not None:
        print(f"  Sample coverage       : {stats.coverage_pct:.2f} %")

    print()


def main() -> None:
    client = InfluxDBClient3(
        host=INFLUXDB_HOST,
        token=INFLUXDB_TOKEN,
        database=INFLUXDB_DATABASE,
    )

    try:
        print(f"Time window: last {TIME_WINDOW}")
        print()

        voltage_stats = analyze_sample_rate(
            client=client,
            table_name=VOLTAGE_TABLE,
            expected_rate_hz=EXPECTED_RATE_HZ.get(VOLTAGE_TABLE),
        )

        temperature_stats = analyze_sample_rate(
            client=client,
            table_name=TEMPERATURE_TABLE,
            expected_rate_hz=EXPECTED_RATE_HZ.get(TEMPERATURE_TABLE),
        )

        print_stats(voltage_stats)
        print_stats(temperature_stats)

    except InfluxDB3ClientQueryError as e:
        print("Failed to query InfluxDB")
        print(f"Host     : {INFLUXDB_HOST}")
        print(f"Database : {INFLUXDB_DATABASE}")
        print(f"Error    : {e}")
    except Exception as e:
        print("Unexpected error while analyzing sample rate")
        print(f"Error: {e}")


if __name__ == "__main__":
    main()