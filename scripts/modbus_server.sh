#!/bin/bash

# Run the container in detached mode
docker run -d \
  --name modbus_server \
  -p 5020:5020 \
  -v ./utils/modbus_server/server_config.json:/app/modbus_server.json \
  oitc/modbus-server:latest

echo "Modbus server started in the background."
