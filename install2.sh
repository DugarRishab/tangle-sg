#!/usr/bin/env bash
set -euo pipefail

# Standalone installer for tangle-sg on Raspberry Pi (or any Debian-based ARM host)
# - Installs system dependencies
# - Builds the C++ project
# - Configures environment variables
# - (Optional) Installs and starts a systemd service
# Idempotent: safe to re-run

# -----------------------------
# Configurable knobs via env
# -----------------------------
: "${INSTALL2_NO_SYSTEMD:=0}"         # set to 1 to skip systemd setup
: "${INSTALL2_SET_TIMEZONE:=0}"       # set to 1 to set timezone to Asia/Kolkata
: "${INSTALL2_TZ:=Asia/Kolkata}"
: "${TX_COUNT:=10}"
: "${TX_DELAY:=30}"
: "${MAX_PEERS:=5}"
: "${POW:=3}"
: "${WAIT_PERIOD:=300}"
: "${RUN_ID:=0}"
: "${MONITOR_PERIOD:=5}"
: "${TELEMETRY_ENDPOINT:=}"
: "${BASE_IP:=}"
: "${HMAC_SECRET:=}"

need_sudo() {
  if [ "${EUID}" -ne 0 ]; then echo "sudo"; else echo ""; fi
}

SUDO=$(need_sudo)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}"
SERVICE_NAME="tangle-sg"
ENV_FILE="/etc/default/${SERVICE_NAME}"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"

arch_info() {
  uname -a || true
  echo "Detected architecture: $(uname -m)"
}

echo "[INFO] Starting install2.sh for tangle-sg"
arch_info

# ---------------------------------
# 1) System packages and toolchain
# ---------------------------------
${SUDO} apt-get update -y
${SUDO} DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  build-essential pkg-config cmake git curl ca-certificates tzdata \
  libssl-dev libwebsocketpp-dev libboost-all-dev libcurl4-openssl-dev libjsoncpp-dev libsodium-dev

if [ "${INSTALL2_SET_TIMEZONE}" = "1" ]; then
  echo "[INFO] Setting timezone to ${INSTALL2_TZ}"
  echo "${INSTALL2_TZ}" | ${SUDO} tee /etc/timezone >/dev/null
  ${SUDO} dpkg-reconfigure -f noninteractive tzdata || true
fi

# ------------------
# 2) Build the app
# ------------------
cd "${PROJECT_DIR}"
echo "[BUILD] Cleaning and building..."
make clean || true
make -j"$(nproc)"

if [ ! -x "${PROJECT_DIR}/tangle_poc" ]; then
  echo "[ERROR] Build failed: tangle_poc not found" >&2
  exit 1
fi

echo "[INFO] Build completed: ${PROJECT_DIR}/tangle_poc"

# ---------------------------------------------
# 3) Configure environment for standalone run
# ---------------------------------------------
# Derive BASE_IP if not provided: first non-loopback IPv4
if [ -z "${BASE_IP}" ]; then
  if command -v hostname >/dev/null 2>&1; then
    BASE_IP=$(hostname -I 2>/dev/null | awk '{print $1}') || true
  fi
fi
if [ -z "${BASE_IP}" ]; then
  echo "[WARN] Could not auto-detect BASE_IP. Please export BASE_IP=xxx.xxx.xxx.xxx and re-run if peer discovery should work."
fi

# Generate HMAC secret if not provided
if [ -z "${HMAC_SECRET}" ]; then
  if command -v openssl >/dev/null 2>&1; then
    HMAC_SECRET=$(openssl rand -hex 32)
  else
    HMAC_SECRET=$(dd if=/dev/urandom bs=32 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')
  fi
fi

# Write /etc/default/tangle-sg
${SUDO} mkdir -p "$(dirname "${ENV_FILE}")"
{
  echo "# Environment for ${SERVICE_NAME}"
  echo "BASE_IP=${BASE_IP}"
  echo "HMAC_SECRET=${HMAC_SECRET}"
  echo "TX_COUNT=${TX_COUNT}"
  echo "TX_DELAY=${TX_DELAY}"
  echo "MAX_PEERS=${MAX_PEERS}"
  echo "POW=${POW}"
  echo "WAIT_PERIOD=${WAIT_PERIOD}"
  echo "RUN_ID=${RUN_ID}"
  echo "MONITOR_PERIOD=${MONITOR_PERIOD}"
  # Not currently consumed by the binary unless code is updated to read it
  echo "TELEMETRY_ENDPOINT=${TELEMETRY_ENDPOINT}"
} | ${SUDO} tee "${ENV_FILE}" >/dev/null

echo "[INFO] Wrote environment to ${ENV_FILE}"

# --------------------------------------
# 4) Optional systemd service installation
# --------------------------------------
if [ "${INSTALL2_NO_SYSTEMD}" = "1" ]; then
  echo "[INFO] Skipping systemd setup (INSTALL2_NO_SYSTEMD=1)."
else
  RUN_USER=$(id -un)
  # Prefer 'pi' when present; otherwise current user
  if id -u pi >/dev/null 2>&1; then RUN_USER=pi; fi

  ${SUDO} bash -c "cat > '${SERVICE_FILE}' <<'UNIT'
[Unit]
Description=Tangle-SG Node (standalone)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=REPLACE_USER
WorkingDirectory=REPLACE_WORKDIR
EnvironmentFile=/etc/default/tangle-sg
ExecStart=REPLACE_WORKDIR/tangle_poc
Restart=on-failure
RestartSec=5

# Hardening (optional)
NoNewPrivileges=true
ProtectSystem=full
ProtectHome=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
UNIT"

  ${SUDO} sed -i "s|REPLACE_USER|${RUN_USER}|g" "${SERVICE_FILE}"
  ${SUDO} sed -i "s|REPLACE_WORKDIR|${PROJECT_DIR}|g" "${SERVICE_FILE}"

  echo "[INFO] Wrote service file to ${SERVICE_FILE}"
  ${SUDO} systemctl daemon-reload
  ${SUDO} systemctl enable "${SERVICE_NAME}"
  ${SUDO} systemctl restart "${SERVICE_NAME}" || ${SUDO} systemctl start "${SERVICE_NAME}"

  echo "[INFO] Service status:"
  ${SUDO} systemctl --no-pager -l status "${SERVICE_NAME}" || true
fi

# ----------------
# 5) Final notes
# ----------------
echo "\n[DONE] tangle-sg installed."
echo "- Binary: ${PROJECT_DIR}/tangle_poc"
echo "- Env: ${ENV_FILE}"
if [ "${INSTALL2_NO_SYSTEMD}" != "1" ]; then
  echo "- Service: ${SERVICE_NAME} (systemd)"
  echo "  Logs: journalctl -u ${SERVICE_NAME} -f"
else
  echo "- To run manually:"
  echo "  BASE_IP='${BASE_IP}' HMAC_SECRET='${HMAC_SECRET}' TX_COUNT='${TX_COUNT}' TX_DELAY='${TX_DELAY}' MAX_PEERS='${MAX_PEERS}' POW='${POW}' WAIT_PERIOD='${WAIT_PERIOD}' RUN_ID='${RUN_ID}' MONITOR_PERIOD='${MONITOR_PERIOD}' ${PROJECT_DIR}/tangle_poc"
fi
