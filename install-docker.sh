#!/bin/bash
# Docker worker build script for Tangle-SG
# Runs inside the DockNet worker container (root, no sudo needed)
set -euo pipefail

echo "[INFO] Starting Docker build for Tangle-SG..."

# 1. Set timezone
export DEBIAN_FRONTEND=noninteractive
echo "Asia/Kolkata" > /etc/timezone
dpkg-reconfigure -f noninteractive tzdata || true

# 2. Update system packages
echo "[INFO] Updating packages..."
apt-get update && apt-get upgrade -y

# 3. Install C++ build dependencies
echo "[INFO] Installing required packages..."
apt-get install -y \
  libssl-dev \
  build-essential \
  libwebsocketpp-dev \
  libboost-all-dev \
  libcurl4-openssl-dev \
  libjsoncpp-dev \
  libsodium-dev

# 4. Skip RPi-specific libraries (not needed in Docker)
echo "[INFO] Skipping RPi.GPIO and spidev (not required in container)."

# 5. Build the C++ project
echo "[INFO] Building the C++ system..."
make clean && make

echo "[INFO] Docker build complete. Binary: ./tangle_poc"
