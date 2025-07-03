#!/bin/bash
# No use of sudo in docker since we get root access by default
echo "[INFO] Starting setup for Tangle-SG system..."

# 1. Enable SPI
echo "[INFO] Enabling SPI... Don't need SPI on docker"

# 2. Update system
echo "[INFO] Updating packages..."
apt-get update &&
apt-get upgrade -y

# 3. Install dependencies
echo "[INFO] Installing required packages..."
apt-get install -y libssl-dev build-essential g++

# 4. Install Python modules via pip (use --break-system-packages only if needed)
# echo "[INFO] Installing Python libraries..."
echo "[INFO] Skipping RPi.GPIO and spidev in Docker container."

# apt install RPi.GPIO spidev 

# 5. Build system
echo "[INFO] Building the C++ system..."
make clean && make

# # 6. Start the program
# echo "[INFO] Starting the system..."
# ./tangle_poc
