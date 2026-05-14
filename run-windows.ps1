#Requires -Version 5.1
<#
.SYNOPSIS
    Build and run Tangle-SG on Windows.
.DESCRIPTION
    Tries WSL first (recommended). Falls back to MinGW-w64 g++ if WSL is unavailable.
    Run from the tangle-sg repository root (same folder as this script).
#>

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $scriptDir

function Test-MinGwAvailable {
    $gcc = Get-Command "g++" -ErrorAction SilentlyContinue
    return ($null -ne $gcc)
}

function Invoke-WslBuild([string]$distro) {
    Write-Host "[INFO] WSL detected. Using distro '$distro'..." -ForegroundColor Green

    # Convert current Windows path to WSL /mnt/... path
    $winPath = Resolve-Path $scriptDir
    $wslPath = $winPath.Path -replace '^([A-Za-z]):', '/mnt/$1' -replace '\\', '/'
    $wslPath = $wslPath.ToLower()

    Write-Host "[INFO] Mapping Windows path to WSL: $wslPath"

    # Run install-server.sh then run.sh inside the specified WSL distro
    # Strip Windows CRLF from shell scripts before execution
    wsl -d $distro -e /bin/bash -c "cd '$wslPath' && sed -i 's/\\r$//' install-server.sh run.sh && chmod +x install-server.sh run.sh && ./install-server.sh && ./run.sh"
}

function Invoke-MinGwBuild {
    Write-Host "[INFO] MinGW-w64 g++ detected. Building natively on Windows..." -ForegroundColor Yellow
    Write-Host "[WARN] Ensure these libraries are installed via MSYS2/MinGW: openssl, boost, jsoncpp, libsodium, curl, pthreads" -ForegroundColor Yellow

    $srcFiles = @(
        "src/main.cpp",
        "src/modules/tsa.cpp",
        "src/modules/network.cpp",
        "src/modules/tangle.cpp",
        "src/modules/peers2.cpp",
        "src/modules/transaction.cpp",
        "src/modules/utils.cpp",
        "src/modules/peerDiscovery.cpp",
        "src/modules/telemetry.cpp"
    )

    $includes = "-I src/headers"
    $libs = "-lssl -lcrypto -lpthread -ljsoncpp -lboost_thread -lsodium -lcurl"
    $flags = "-std=c++17 -Wall -Wextra"

    $cmd = "g++ $flags $includes -o tangle_poc.exe $($srcFiles -join ' ') $libs"
    Write-Host "[BUILD] $cmd"
    Invoke-Expression $cmd

    if (-not (Test-Path "tangle_poc.exe")) {
        throw "Build failed: tangle_poc.exe was not created."
    }

    Write-Host "[INFO] Build successful. Starting tangle_poc.exe..." -ForegroundColor Green

    $env:TX_COUNT = $env:TX_COUNT -or "10"
    $env:TX_DELAY = $env:TX_DELAY -or "30"
    $env:MAX_PEERS = $env:MAX_PEERS -or "5"
    $env:WAIT_PERIOD = $env:WAIT_PERIOD -or "300"
    $env:RUN_ID = $env:RUN_ID -or "0"
    $env:MONITOR_PERIOD = $env:MONITOR_PERIOD -or "5"
    $env:ORPHAN_TTL_SEC = $env:ORPHAN_TTL_SEC -or "600"
    $env:ORPHAN_POOL_MAX = $env:ORPHAN_POOL_MAX -or "1000"
    $env:RATE_LIMIT_BASE = $env:RATE_LIMIT_BASE -or "10.0"
    $env:RATE_LIMIT_BURST = $env:RATE_LIMIT_BURST -or "20.0"
    $env:RATE_LIMIT_WINDOW_SEC = $env:RATE_LIMIT_WINDOW_SEC -or "60"

    if (-not $env:BASE_IP) {
        $ip = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notmatch '^127\.' } | Select-Object -First 1).IPAddress
        if ($ip) {
            $env:BASE_IP = $ip
            Write-Host "[INFO] Auto-detected BASE_IP=$ip"
        } else {
            Write-Host "[WARN] Could not auto-detect BASE_IP. Set it manually: \$env:BASE_IP='xxx.xxx.xxx.xxx'"
        }
    }

    if (-not $env:HMAC_SECRET) {
        $env:HMAC_SECRET = "tangle-sg-shared-hmac-secret-12345"
    }

    & "./tangle_poc.exe"
}

# ---- main ----
Write-Host "[INFO] Tangle-SG Windows launcher"
Write-Host "[INFO] Repository path: $(Resolve-Path .)"

# Check if the Ubuntu WSL distro is usable
$usableDistro = $null
try {
    $probe = wsl -d Ubuntu -e /bin/bash -c 'echo wsl_ok' 2>$null
    if ($LASTEXITCODE -eq 0 -and $probe -eq "wsl_ok") {
        $usableDistro = "Ubuntu"
    }
} catch {
    $usableDistro = $null
}

if ($usableDistro) {
    Invoke-WslBuild -distro $usableDistro
} elseif (Test-MinGwAvailable) {
    Invoke-MinGwBuild
} else {
    # Diagnostics: show what we found
    Write-Host ""
    Write-Host "[DIAGNOSTICS]" -ForegroundColor Cyan
    $wslPath = (Get-Command "wsl" -ErrorAction SilentlyContinue).Source
    if ($wslPath) {
        Write-Host "  wsl.exe found at: $wslPath"
        Write-Host "  Installed distros:"
        wsl -l -q 2>$null | ForEach-Object { Write-Host "    - $_" }
        Write-Host ""
        Write-Host "[HINT] If you just installed WSL, open it once to finish distro setup, then re-run this script." -ForegroundColor Yellow
        Write-Host "[HINT] If bash is missing inside WSL, run: wsl -e /bin/bash" -ForegroundColor Yellow
    } else {
        Write-Host "  wsl.exe not found on PATH."
    }

    $gppPath = (Get-Command "g++" -ErrorAction SilentlyContinue).Source
    if ($gppPath) {
        Write-Host "  g++ found at: $gppPath"
    } else {
        Write-Host "  g++ not found on PATH."
    }

    Write-Host @"

[ERROR] No supported build environment found on this Windows machine.

To run Tangle-SG on Windows, install one of the following:

1. WSL2 (recommended for production parity):
   wsl --install
   Then open the WSL terminal once to complete distro setup, then re-run this script.

2. MSYS2 / MinGW-w64 (for native Windows builds):
   Install from https://www.msys2.org/
   Then install dependencies:
     pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-boost \
               mingw-w64-x86_64-jsoncpp mingw-w64-x86_64-libsodium \
               mingw-w64-x86_64-curl mingw-w64-x86_64-openssl
   Ensure g++ is on your PATH, then re-run this script.
"@ -ForegroundColor Red
    exit 1
}
