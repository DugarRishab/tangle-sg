#!/bin/bash
set -euo pipefail

# Source systemd env file if it exists
if [ -f /etc/default/tangle-sg ]; then
    # shellcheck source=/dev/null
    source /etc/default/tangle-sg
fi

# Ensure BASE_IP is set (required by PeerDiscovery)
if [ -z "${BASE_IP:-}" ]; then
    # Try common auto-detection methods
    if command -v ip >/dev/null 2>&1; then
        BASE_IP=$(ip -4 route get 8.8.8.8 2>/dev/null | awk '{print $7; exit}') || true
    fi
    if [ -z "${BASE_IP:-}" ] && command -v hostname >/dev/null 2>&1; then
        BASE_IP=$(hostname -I 2>/dev/null | awk '{print $1}') || true
    fi
    if [ -z "${BASE_IP:-}" ]; then
        echo "[ERROR] BASE_IP is not set and could not be auto-detected."
        echo "        Please export BASE_IP=<your_ip> and re-run."
        exit 1
    fi
    echo "[INFO] Auto-detected BASE_IP=${BASE_IP}"
    export BASE_IP
fi

# Optional env defaults (match install2.sh defaults)
export TX_COUNT="${TX_COUNT:-10}"
export TX_DELAY="${TX_DELAY:-30}"
export MAX_PEERS="${MAX_PEERS:-5}"
export WAIT_PERIOD="${WAIT_PERIOD:-300}"
export RUN_ID="${RUN_ID:-0}"
export MONITOR_PERIOD="${MONITOR_PERIOD:-5}"
export ORPHAN_TTL_SEC="${ORPHAN_TTL_SEC:-600}"
export ORPHAN_POOL_MAX="${ORPHAN_POOL_MAX:-1000}"
export RATE_LIMIT_BASE="${RATE_LIMIT_BASE:-10.0}"
export RATE_LIMIT_BURST="${RATE_LIMIT_BURST:-20.0}"
export RATE_LIMIT_WINDOW_SEC="${RATE_LIMIT_WINDOW_SEC:-60}"
if [ -n "${TELEMETRY_ENDPOINT:-}" ]; then
    export TELEMETRY_ENDPOINT
fi

# Check build tools
for cmd in make g++; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "[ERROR] Required command '$cmd' not found. Please install build-essential."
        exit 1
    fi
done

# Ensure required directories exist
mkdir -p keys secret

echo "[INFO] Cleaning old build files..."
make clean

echo "[INFO] Compiling the project..."
if make; then
    echo "[INFO] Compilation successful! Running the program..."
    ./tangle_poc
else
    echo "[ERROR] Compilation failed! Fix the errors and try again."
    exit 1
fi
