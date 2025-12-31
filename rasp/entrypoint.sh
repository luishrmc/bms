#!/usr/bin/env bash
set -euo pipefail

cd /opt/bms

TOKEN_FILE="config/influxdb3/token.json"
GET_TOKEN_SCRIPT="scripts/get_token.sh"
SETUP_SCHEMA_SCRIPT="scripts/setup_schema.sh"

# Basic sanity checks
if [[ ! -x "bin/bms" ]]; then
  echo "ERROR: /opt/bms/bin/bms not found or not executable."
  exit 1
fi

if [[ ! -x "$GET_TOKEN_SCRIPT" ]]; then
  echo "ERROR: $GET_TOKEN_SCRIPT not found or not executable."
  exit 1
fi

if [[ ! -x "$SETUP_SCHEMA_SCRIPT" ]]; then
  echo "ERROR: $SETUP_SCHEMA_SCRIPT not found or not executable."
  exit 1
fi

# Tools required by the scripts
command -v curl >/dev/null 2>&1 || { echo "ERROR: curl is required."; exit 1; }
command -v jq   >/dev/null 2>&1 || { echo "ERROR: jq is required."; exit 1; }

mkdir -p "$(dirname "$TOKEN_FILE")"

if [[ ! -f "$TOKEN_FILE" ]]; then
  echo "token.json not found at $TOKEN_FILE. Bootstrapping InfluxDB token and schema..."

  # Optional: simple retry loop in case influxdb3 isn't ready yet.
  # Your get_token.sh already fails fast on non-200/201. :contentReference[oaicite:1]{index=1}
  for attempt in {1..30}; do
    if "$GET_TOKEN_SCRIPT"; then
      break
    fi
    echo "Waiting for influxdb3... (attempt $attempt/30)"
    sleep 2
  done

  # If token still doesn't exist, fail clearly
  if [[ ! -f "$TOKEN_FILE" ]]; then
    echo "ERROR: Failed to create $TOKEN_FILE after retries."
    exit 1
  fi

  "$SETUP_SCHEMA_SCRIPT"
else
  echo "token.json already exists. Skipping token/schema initialization."
fi

echo "Starting BMS..."
exec /opt/bms/bin/bms
