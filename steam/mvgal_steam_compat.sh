#!/usr/bin/env bash
# MVGAL Steam Compatibility Tool entry point
# Registered as a Steam compatibility tool so users can select it in
# Steam → Properties → Compatibility → Force the use of a specific
# Steam Play compatibility tool.
#
# SPDX-License-Identifier: MIT

set -euo pipefail

MVGAL_VERSION="0.2.1"
MVGAL_SOCKET="/run/mvgal/mvgal.sock"

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

log() {
    echo "[mvgal-steam] $*" >&2
}

# ---------------------------------------------------------------------------
# Check MVGAL daemon
# ---------------------------------------------------------------------------

check_daemon() {
    if [ -S "$MVGAL_SOCKET" ]; then
        log "Daemon socket found at $MVGAL_SOCKET"
        return 0
    else
        log "Warning: MVGAL daemon not running (socket $MVGAL_SOCKET absent)"
        log "Start with: pkexec systemctl start mvgald"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# Set up environment for the game
# ---------------------------------------------------------------------------

setup_env() {
    # Enable MVGAL
    export ENABLE_MVGAL=1

    # Default strategy: AFR for gaming
    export MVGAL_STRATEGY="${MVGAL_STRATEGY:-afr}"

    # Enable frame pacing by default
    export MVGAL_FRAME_PACING="${MVGAL_FRAME_PACING:-1}"

    # Vulkan layer path (if installed locally)
    local layer_dir
    for layer_dir in \
        "/usr/share/vulkan/implicit_layer.d" \
        "/usr/local/share/vulkan/implicit_layer.d" \
        "$HOME/.local/share/vulkan/implicit_layer.d"; do
        if [ -f "$layer_dir/VK_LAYER_MVGAL.json" ]; then
            log "Vulkan layer found at $layer_dir"
            break
        fi
    done

    # DXVK / VKD3D-Proton: no special config needed, MVGAL layer is implicit

    log "MVGAL environment:"
    log "  ENABLE_MVGAL=$ENABLE_MVGAL"
    log "  MVGAL_STRATEGY=$MVGAL_STRATEGY"
    log "  MVGAL_FRAME_PACING=$MVGAL_FRAME_PACING"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

main() {
    log "MVGAL Steam Compatibility Tool v$MVGAL_VERSION"

    if [ $# -eq 0 ]; then
        echo "Usage: $0 <game-executable> [args...]"
        exit 1
    fi

    check_daemon || true   # non-fatal; layer still works without daemon

    setup_env

    log "Launching: $*"
    exec "$@"
}

main "$@"
