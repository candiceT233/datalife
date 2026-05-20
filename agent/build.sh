#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATALIFE_ROOT="$(dirname "$SCRIPT_DIR")"

echo "=== Building DataLife flow-monitor ==="

if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake not found. Install cmake >= 2.8."
    exit 1
fi

BUILD_DIR="$DATALIFE_ROOT/build"
# TIMER_JSON=ON is REQUIRED: it makes the Timer destructor emit
# monitor_timer.<pid>-<host>.datalife.json per process. Without it libmonitor
# only prints text stats and writes NO timer JSON, so dfl_mcp.verification fails
# ("no monitor_timer.*.datalife.json files found") even when block traces exist.
cmake -S "$DATALIFE_ROOT" -B "$BUILD_DIR" \
    -DENABLE_FlowMonitor=ON -DENABLE_FlowAnalysis=OFF -DTIMER_JSON=ON
cmake --build "$BUILD_DIR" -j"$(nproc)"

LIB=$(find "$BUILD_DIR" -name 'libmonitor.so' -type f | head -1)
if [ -z "$LIB" ]; then
    echo "ERROR: libmonitor.so not produced."
    exit 1
fi

echo "Built successfully: $LIB"
