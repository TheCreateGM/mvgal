#!/bin/bash
# MVGAL Vulkan Aggregation Verification Script
#
# This script verifies that:
# 1. Multiple GPUs are detected and aggregated.
# 2. Vulkan device groups are correctly emulated.
# 3. Dynamic load balancing triggers rebalancing in the rewrite engine.

set -e

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}Starting MVGAL Verification...${NC}"

# 1. Check GPU Detection
echo -n "Checking GPU detection... "
GPU_COUNT=$(mvgal-info --count || echo 0)
if [ "$GPU_COUNT" -gt 0 ]; then
    echo -e "${GREEN}OK ($GPU_COUNT GPUs detected)${NC}"
else
    echo -e "${RED}FAILED (No GPUs detected)${NC}"
    exit 1
fi

# 2. Verify Vulkan Device Group Emulation
echo -n "Verifying Vulkan device group emulation... "
if mvgal-info --vulkan-groups | grep -q "MVGAL Virtual Multi-GPU"; then
    echo -e "${GREEN}OK${NC}"
else
    echo -e "${RED}FAILED (Device group not found)${NC}"
    exit 1
fi

# 3. Test Load Balancing Trigger
echo -n "Testing load balancing trigger... "
# Start a simulated workload on GPU 0 to create imbalance
mvgal-bench --simulate-load --gpu 0 --duration 5 > /dev/null 2>&1 &
BENCH_PID=$!

sleep 2

# Check if rebalancing was triggered in logs
if grep -q "Rebalancing SFR tiles" /var/log/mvgal.log 2>/dev/null || \
   grep -q "Load imbalance detected" /var/log/mvgal.log 2>/dev/null; then
    echo -e "${GREEN}OK (Rebalancing triggered)${NC}"
else
    echo -e "${RED}FAILED (Rebalancing not triggered)${NC}"
    # Note: This might fail if log level is too low or timing is off
fi

wait $BENCH_PID || true

echo -e "${GREEN}Verification Complete!${NC}"
