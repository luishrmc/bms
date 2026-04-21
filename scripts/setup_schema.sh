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
BATTERY_SNAPSHOT_BOOTSTRAP='battery_snapshot pack_voltage_v=0.0,pack_current_raw=0i,soc_pct=0u,soh_pct=0u,remaining_capacity_ah=0u,max_charge_current_a=0u,bms_cooling_temp_c=0i,battery_internal_temp_c=0i,max_cell_temp_c=0i,status_raw=0u,alarm_raw=0u,protection_raw=0u,error_raw=0u,cycle_count=0u,full_charge_capacity_mas=0u,cell_count=0u,full_charge_capacity_ah=0.0,cell1_mv=0u,cell2_mv=0u,cell3_mv=0u,cell4_mv=0u,cell5_mv=0u,cell6_mv=0u,cell7_mv=0u,cell8_mv=0u,cell9_mv=0u,cell10_mv=0u,cell11_mv=0u,cell12_mv=0u,cell13_mv=0u,cell14_mv=0u,cell15_mv=0u,cell16_mv=0u,serial_or_model="",bms_version="",manufacturer=""'

write_bootstrap() {
    local table_name="$1"
    local payload="$2"

    echo -n "Configuring ${table_name}... "
    WRITE_STATUS=$(curl -s -o /dev/null -w "%{http_code}" \
      -X POST "http://${HOST}:${PORT}/api/v3/write_lp?db=${DB_NAME}&precision=auto" \
      --header "Authorization: Bearer $TOKEN" \
      --header "Content-Type: text/plain; charset=utf-8" \
      --data-binary "$payload")

    if [ "$WRITE_STATUS" -eq 204 ]; then
        echo "OK (204)"
    else
        echo "FAILED (Status: $WRITE_STATUS)"
        exit 1
    fi
}

write_bootstrap "voltage_current" "$VOLTAGE_CURRENT_BOOTSTRAP"
write_bootstrap "temperature" "$TEMPERATURE_BOOTSTRAP"
write_bootstrap "battery_snapshot" "$BATTERY_SNAPSHOT_BOOTSTRAP"

echo -e "\n--- Setup Verified and Complete ---"
echo "Database: $DB_NAME"
echo "Tables: voltage_current, temperature, battery_snapshot"