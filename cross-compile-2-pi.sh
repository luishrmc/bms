#!/bin/bash

set -e

# === CONFIGURATION ===
IMAGE_NAME="cpp-dev-arm64"
DOCKERFILE_PATH=".devcontainer/Dockerfile"
BUILD_CONTEXT=".."
TAR_FILE="${IMAGE_NAME}.tar.gz"

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
  -t "$IMAGE_NAME" \
  --load "$BUILD_CONTEXT"

# === 4. Export Docker image to tarball ===
echo "📦 Saving Docker image to $TAR_FILE..."
docker save "$IMAGE_NAME" | gzip > "$TAR_FILE"

echo "✅ Done"
