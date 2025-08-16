#!/bin/bash

# This causes the script to exit immediately if any command fails
set -e

# === CONFIGURATION ===
DOCKERFILE_PATH=".devcontainer/Dockerfile.arm64"
BUILD_CONTEXT="."

# === 1. Setup QEMU for ARM builds ===
echo "🔧 Setting up QEMU..."
sudo apt-get update
sudo apt-get install -y qemu-user-static
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes

# === 2. Enable Docker Buildx ===
echo "🛠️ Initializing Docker Buildx..."
docker buildx create --use || true
docker buildx inspect --bootstrap

# === 3. Build ARM64 image ===
echo "🧱 Building Docker image for ARM64..."
docker buildx build \
  --platform linux/arm64 \
  -f "$DOCKERFILE_PATH" \
  -t lumac1976/bms:arm64 \
  --push "$BUILD_CONTEXT"

echo "✅ Done"
