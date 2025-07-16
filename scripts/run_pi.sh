#!/bin/bash

set -e

COMPOSE_FILE=docker/docker-compose.yml

echo "🔄 Pulling latest images..."
docker compose -f "$COMPOSE_FILE" pull

echo "🧹 Stopping any running containers..."
docker compose -f "$COMPOSE_FILE" down

echo "🚀 Starting BMS and Mosquitto containers..."
docker compose -f "$COMPOSE_FILE" up -d

echo "✅ BMS stack is running!"
docker compose -f "$COMPOSE_FILE" ps
