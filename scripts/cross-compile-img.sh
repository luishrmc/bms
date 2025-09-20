#!/bin/bash
set -euo pipefail

DOCKERFILE_PATH=".devcontainer/Dockerfile.arm64"   # runtime-only Dockerfile
BUILD_CONTEXT="."

# 0) Ensure the arm64 binary exists before building the image
if [[ ! -x "build_arm64/project_output/bms" ]]; then
  echo "❌ Missing binary: build_arm64/project_output/bms"
  echo "   Run your CMake arm64 task first, then re-run this script."
  exit 1
fi

# 1) (Optional) Setup QEMU for running arm64 binaries during build
# Not strictly needed here since the Dockerfile only copies the binary.
# Uncomment if your future Dockerfile RUNs arm64 code during build.
# echo "🔧 Setting up QEMU..."
# sudo apt-get update -y
# sudo apt-get install -y qemu-user-static
# docker run --rm --privileged multiarch/qemu-user-static --reset -p yes

# 2) Ensure Docker Buildx is ready
echo "🛠️ Initializing Docker Buildx..."
docker buildx create --name bmsbuilder --use 2>/dev/null || docker buildx use bmsbuilder
docker buildx inspect --bootstrap

# 3) Check Docker Hub login
if ! docker info | grep -q 'Username:'; then
  echo "⚠️  Not logged in to Docker Hub. Run: docker login"
  exit 1
fi

# 4) Build & push arm64 runtime image
echo "🧱 Building Docker image for ARM64..."
docker buildx build \
  --pull \
  --platform linux/arm64 \
  -f "$DOCKERFILE_PATH" \
  -t lumac1976/bms:arm64 \
  --push "$BUILD_CONTEXT"

echo "✅ Done. Image pushed as lumac1976/bms:arm64"
