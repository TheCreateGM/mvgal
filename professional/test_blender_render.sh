#!/usr/bin/env bash
# Test Blender multi-GPU rendering with MVGAL
# Usage: ./test_blender_render.sh [scene.blend]
# SPDX-License-Identifier: MIT

set -euo pipefail

SCENE="${1:-/usr/share/blender/scripts/addons/cycles/shader/default.blend}"
BLENDER="${BLENDER:-blender}"
FRAMES=5

if ! command -v "$BLENDER" &>/dev/null; then
    echo "Blender not found. Install with: sudo apt install blender"
    exit 1
fi

echo "=== MVGAL Blender Multi-GPU Render Test ==="
echo "Scene: $SCENE"
echo "Frames: $FRAMES"
echo ""

# Single GPU baseline
echo "--- Single GPU (baseline) ---"
t0=$(date +%s%N)
ENABLE_MVGAL=0 "$BLENDER" -b "$SCENE" -E CYCLES -o /tmp/mvgal_test_single_ \
    -s 1 -e "$FRAMES" -a 2>/dev/null || true
t1=$(date +%s%N)
single_ms=$(( (t1 - t0) / 1000000 ))
echo "Single GPU time: ${single_ms} ms"

# Multi GPU with MVGAL
echo ""
echo "--- Multi GPU (MVGAL) ---"
t0=$(date +%s%N)
ENABLE_MVGAL=1 MVGAL_STRATEGY=compute_offload \
    "$BLENDER" -b "$SCENE" -E CYCLES -o /tmp/mvgal_test_multi_ \
    -s 1 -e "$FRAMES" -a 2>/dev/null || true
t1=$(date +%s%N)
multi_ms=$(( (t1 - t0) / 1000000 ))
echo "Multi GPU time: ${multi_ms} ms"

# Report
echo ""
echo "=== Results ==="
echo "Single GPU: ${single_ms} ms"
echo "Multi GPU:  ${multi_ms} ms"

if [ "$multi_ms" -gt 0 ] && [ "$single_ms" -gt 0 ]; then
    speedup=$(echo "scale=2; $single_ms / $multi_ms" | bc)
    echo "Speedup:    ${speedup}×"
    if (( $(echo "$speedup >= 1.5" | bc -l) )); then
        echo "✓ Meets 1.5× target"
    else
        echo "✗ Below 1.5× target"
    fi
fi

# Cleanup
rm -f /tmp/mvgal_test_single_*.png /tmp/mvgal_test_multi_*.png
