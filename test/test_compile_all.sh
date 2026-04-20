#!/bin/bash

# MVGAL Compilation Test Script
# Tests all core modules compile with strict flags

# Don't exit on error - we want to test all files
set +e

echo "=========================================="
echo "MVGAL Compilation Test"
echo "=========================================="
echo ""

CFLAGS="-Iinclude -Iinclude/mvgal -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror -O2"
BASE_DIR="/home/axogm/Documents/Driver/mvgal"
TMP_DIR="/tmp/mvgal_test"

mkdir -p "$TMP_DIR"

total=0
passed=0
failed=0

# Test function
test_file() {
    local file="$1"
    local name=$(basename "$file")
    local output="$TMP_DIR/$(basename "$file" .c).o"

    total=$((total + 1))
    echo -n "[$total] $name... "

    if cc $CFLAGS -c "$file" -o "$output" 2>&1; then
        echo "✅ PASS"
        passed=$((passed + 1))
        return 0
    else
        echo "❌ FAIL"
        failed=$((failed + 1))
        return 1
    fi
}

echo "--- Memory Module ---"
for f in "$BASE_DIR"/src/userspace/memory/*.c; do
    test_file "$f" || true
done

echo ""
echo "--- Scheduler Module ---"
for f in "$BASE_DIR"/src/userspace/scheduler/*.c "$BASE_DIR"/src/userspace/scheduler/strategy/*.c; do
    test_file "$f" || true
done

echo ""
echo "--- API Module ---"
for f in "$BASE_DIR"/src/userspace/api/*.c; do
    test_file "$f" || true
done

echo ""
echo "--- Daemon Module ---"
for f in "$BASE_DIR"/src/userspace/daemon/*.c; do
    test_file "$f" || true
done

echo ""
echo "--- OpenCL Intercept Module ---"
for f in "$BASE_DIR"/src/userspace/intercept/opencl/*.c; do
    test_file "$f" || true
done

echo ""
echo "------------------------------------------"
echo "Results: $passed/$total passed, $failed failed"
echo "------------------------------------------"

if [ $failed -eq 0 ]; then
    echo "✅ ALL TESTS PASSED"
    exit 0
else
    echo "❌ SOME TESTS FAILED"
    exit 1
fi
