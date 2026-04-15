#!/bin/bash

set -euo pipefail

# 1. CONFIGURATION
HOST="influxdb3"
PORT="8181"
DB_NAME="battery_data"
TOKEN_FILE="config/influxdb3/token.json"

# Check if jq is installed
if ! command -v jq &> /dev/null; then
    echo "Error: 'jq' is not installed. Run 'sudo apt-get install jq' first."
    exit 1
fi

# 2. LOAD TOKEN
if [ ! -f "$TOKEN_FILE" ]; then
    echo "Error: Token file not found at $TOKEN_FILE"
    exit 1
fi

TOKEN=$(jq -r '.token' "$TOKEN_FILE")

if [ -z "$TOKEN" ] || [ "$TOKEN" == "null" ]; then
  echo "Error: Could not extract token from $TOKEN_FILE"
  exit 1
fi

# 3. CREATE DATABASE
echo "Step 1: Creating database '$DB_NAME'..."
STATUS=$(curl -s -o /dev/null -w "%{http_code}" \
  -X POST "http://${HOST}:${PORT}/api/v3/configure/database" \
  --header "Authorization: Bearer $TOKEN" \
  --header 'Content-Type: application/json' \
  --data "{\"db\": \"$DB_NAME\"}")

if [ "$STATUS" -ne 200 ] && [ "$STATUS" -ne 201 ] && [ "$STATUS" -ne 409 ]; then
    echo "FAILED to create/verify database (status: $STATUS)"
    exit 1
fi

# 4. INITIALIZE TABLE (Schema-on-Write)
echo -e "\nStep 2: Initializing processed telemetry schema..."

PROCESSED_DATA='processed_telemetry cursor=0u,current_a=0.0,valid=false,voltages="[]",temperatures="[]",status="bootstrap"'

echo -n "Configuring processed_telemetry... "
WRITE_STATUS=$(curl -s -o /dev/null -w "%{http_code}" \
  -X POST "http://${HOST}:${PORT}/api/v3/write_lp?db=${DB_NAME}&precision=auto" \
  --header "Authorization: Bearer $TOKEN" \
  --header "Content-Type: text/plain; charset=utf-8" \
  --data-binary "$PROCESSED_DATA")

if [ "$WRITE_STATUS" -eq 204 ]; then
    echo "OK (204)"
else
    echo "FAILED (Status: $WRITE_STATUS)"
    exit 1
fi

echo -e "\n--- Setup Verified and Complete ---"
echo "Database: $DB_NAME"
echo "Tables: processed_telemetry"
