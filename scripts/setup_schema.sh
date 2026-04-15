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

# 4. INITIALIZE TABLES (Schema-on-Write)
echo -e "\nStep 2: Initializing simplified runtime schemas..."

VOLTAGE_CURRENT_BOOTSTRAP='voltage_current cell1_v=0.0,cell2_v=0.0,cell3_v=0.0,cell4_v=0.0,cell5_v=0.0,cell6_v=0.0,cell7_v=0.0,cell8_v=0.0,cell9_v=0.0,cell10_v=0.0,cell11_v=0.0,cell12_v=0.0,cell13_v=0.0,cell14_v=0.0,cell15_v=0.0,raw_current_sensor_v=0.0,current_a=0.0,sequence=0u'
TEMPERATURE_BOOTSTRAP='temperature sensor1_c=0.0,sensor2_c=0.0,sensor3_c=0.0,sensor4_c=0.0,sensor5_c=0.0,sensor6_c=0.0,sensor7_c=0.0,sensor8_c=0.0,sensor9_c=0.0,sensor10_c=0.0,sensor11_c=0.0,sensor12_c=0.0,sensor13_c=0.0,sensor14_c=0.0,sensor15_c=0.0,sensor16_c=0.0,sequence=0u'

echo -n "Configuring voltage_current... "
WRITE_STATUS=$(curl -s -o /dev/null -w "%{http_code}" \
  -X POST "http://${HOST}:${PORT}/api/v3/write_lp?db=${DB_NAME}&precision=auto" \
  --header "Authorization: Bearer $TOKEN" \
  --header "Content-Type: text/plain; charset=utf-8" \
  --data-binary "$VOLTAGE_CURRENT_BOOTSTRAP")

if [ "$WRITE_STATUS" -eq 204 ]; then
    echo "OK (204)"
else
    echo "FAILED (Status: $WRITE_STATUS)"
    exit 1
fi

echo -n "Configuring temperature... "
WRITE_STATUS=$(curl -s -o /dev/null -w "%{http_code}" \
  -X POST "http://${HOST}:${PORT}/api/v3/write_lp?db=${DB_NAME}&precision=auto" \
  --header "Authorization: Bearer $TOKEN" \
  --header "Content-Type: text/plain; charset=utf-8" \
  --data-binary "$TEMPERATURE_BOOTSTRAP")

if [ "$WRITE_STATUS" -eq 204 ]; then
    echo "OK (204)"
else
    echo "FAILED (Status: $WRITE_STATUS)"
    exit 1
fi

echo -e "\n--- Setup Verified and Complete ---"
echo "Database: $DB_NAME"
echo "Tables: voltage_current, temperature"
