#!/bin/bash

echo "[INFO] Starting setup for Tangle-SG system..."

# 1. Enable SPI
echo "[INFO] Enabling SPI..."
if ! grep -q "^dtparam=spi=on" /boot/config.txt; then
    sudo sed -i '/^#*dtparam=spi=/c\dtparam=spi=on' /boot/config.txt
    sudo systemctl restart systemd-modules-load.service
else
    echo "[INFO] SPI already enabled."
fi

# 2. Update system
echo "[INFO] Updating packages..."
sudo apt update && sudo apt upgrade -y

# 3. Install dependencies
echo "[INFO] Installing required packages..."
sudo apt install -y libssl-dev build-essential g++

# 4. Install Python modules via pip (use --break-system-packages only if needed)
# echo "[INFO] Installing Python libraries..."
# sudo apt install RPi.GPIO spidev 

# 5. Build system
echo "[INFO] Building the C++ system..."
make clean && make

# # 6. Start the program
# echo "[INFO] Starting the system..."
# ./tangle_poc
