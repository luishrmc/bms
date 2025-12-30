#!/bin/bash

# 1. CONFIGURATION
# Using the service name 'influxdb3' ensures connectivity inside the Docker network 
HOST="influxdb3"
PORT="8181"
TOKEN_FILE="config/influxdb3/token.json"

echo "Requesting admin token from http://${HOST}:${PORT}..."

# 2. EXECUTE REQUEST
# -w "%{http_code}" captures the status code to stdout
# -s : Silent mode
# -o : Directs the JSON response body to the file
HTTP_STATUS=$(curl -s -o "$TOKEN_FILE" -w "%{http_code}" \
  -X POST "http://${HOST}:${PORT}/api/v3/configure/token/admin" \
  --header 'Accept: application/json' \
  --header 'Content-Type: application/json')

# 3. CHECK HTTP STATUS CODE
if [ "$HTTP_STATUS" -eq 200 ] || [ "$HTTP_STATUS" -eq 201 ]; then
    echo "SUCCESS: Server returned $HTTP_STATUS."
else
    echo "ERROR: Request failed with HTTP Status: $HTTP_STATUS"
    # Clean up the file if it contains an error message instead of a token
    rm -f "$TOKEN_FILE"
    exit 1
fi

# 4. VERIFY JSON INTEGRITY
# Ensure the file is not empty and contains a valid '.token' field [cite: 19]
if ! jq -e '.token' "$TOKEN_FILE" > /dev/null 2>&1; then
    echo "ERROR: Response saved to $TOKEN_FILE is not a valid token JSON."
    cat "$TOKEN_FILE"
    exit 1
fi

echo "Token successfully saved and verified in $TOKEN_FILE"
