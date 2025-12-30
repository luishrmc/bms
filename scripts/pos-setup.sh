#!/bin/bash

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

# 4. INITIALIZE TABLES (Schema-on-Write)
echo -e "\nStep 2: Initializing table schemas..."

# Build wide fieldsets (comma-separated fields)
V_FIELDS=""
for i in $(seq 0 14); do
  V_FIELDS+="cell${i}=0.0,"
done
V_FIELDS="${V_FIELDS%,}"   # trim trailing comma
V_DATA="voltage ${V_FIELDS}"

T_FIELDS=""
for i in $(seq 0 15); do
  T_FIELDS+="sensor${i}=0.0,"
done
T_FIELDS="${T_FIELDS%,}"
T_DATA="temperature ${T_FIELDS}"

C_DATA="current current=0.0"

for DATA in "$V_DATA" "$T_DATA" "$C_DATA"; do
    TABLE=$(echo "$DATA" | awk '{print $1}')
    echo -n "Configuring $TABLE... "

    WRITE_STATUS=$(curl -s -o /dev/null -w "%{http_code}" \
      -X POST "http://${HOST}:${PORT}/api/v3/write_lp?db=${DB_NAME}&precision=auto" \
      --header "Authorization: Bearer $TOKEN" \
      --header "Content-Type: text/plain; charset=utf-8" \
      --data-binary "$DATA")

    if [ "$WRITE_STATUS" -eq 204 ]; then
        echo "OK (204)"
    else
        echo "FAILED (Status: $WRITE_STATUS)"
        exit 1
    fi
done

echo -e "\n--- Setup Verified and Complete ---"
echo "Database: $DB_NAME"
echo "Tables: voltage, temperature, current"
