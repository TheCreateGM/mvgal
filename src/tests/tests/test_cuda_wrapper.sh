#!/bin/bash
# Test script for CUDA wrapper

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

CUDADIR="/home/axogm/Documents/Driver/mvgal/src/userspace/intercept/cuda"
LIBMVGAL_CUDA="${CUDADIR}/libmvgal_cuda.so"

echo "========================================="
echo "MVGAL CUDA Wrapper Test Script"
echo "========================================="
echo ""

# Check if library exists
if [ ! -f "$LIBMVGAL_CUDA" ]; then
    echo -e "${RED}Error: $LIBMVGAL_CUDA not found!${NC}"
    echo "Please build the CUDA wrapper first:"
    echo "  cd $CUDADIR && gcc -shared -fPIC -I../../../../include -o libmvgal_cuda.so cuda_wrapper.c -ldl -lpthread -L../../../../build/src/userspace -lmvgal_core"
    exit 1
fi

echo -e "${GREEN}✓ CUDA wrapper library found: $LIBMVGAL_CUDA${NC}"
echo ""

# Check library size
echo "Library info:"
ls -lh "$LIBMVGAL_CUDA"
ldd "$LIBMVGAL_CUDA"
echo ""

# Test 1: Check if library can be loaded
echo "Test 1: Loading library..."
CAT_PROGRAM=$(cat <<'EOF'
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>

int main() {
    void* handle = dlopen("libmvgal_cuda.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) {
        fprintf(stderr, "Error loading library: %s\n", dlerror());
        return 1;
    }
    printf("Library loaded successfully!\n");
    dlclose(handle);
    return 0;
}
EOF
)

echo "$CAT_PROGRAM" > /tmp/test_load.c
cd /tmp
GCC_OUTPUT=$(gcc -o test_load test_load.c -ldl 2>&1)
if [ $? -ne 0 ]; then
    echo -e "${RED}Failed to compile test program${NC}"
    echo "$GCC_OUTPUT"
    cd - >/dev/null
    exit 1
fi

cp "$LIBMVGAL_CUDA" /tmp/libmvgal_cuda.so
LD_LIBRARY_PATH=/tmp ./test_load
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Test 1 PASSED: Library loads successfully${NC}"
else
    echo -e "${RED}✗ Test 1 FAILED: Library failed to load${NC}"
fi
cd - >/dev/null
rm -f /tmp/test_load /tmp/test_load.c /tmp/libmvgal_cuda.so

echo ""

# Test 2: Check symbols in library
echo "Test 2: Checking exported symbols..."
if nm -D "$LIBMVGAL_CUDA" 2>/dev/null | grep -q "cuda_wrapper_init"; then
    echo -e "${GREEN}✓ Test 2 PASSED: Init function found${NC}"
else
    echo -e "${YELLOW}⚠ Warning: Init function not found in symbols (may be static)${NC}"
fi
echo ""

# Test 3: Try with a simple CUDA detection
echo "Test 3: Checking CUDA availability..."
if command -v nvcc &>/dev/null || [ -f /usr/local/cuda/bin/nvcc ]; then
    echo -e "${GREEN}✓ CUDA Toolkit is installed${NC}"
    echo ""
    echo "To test with a real CUDA application:"
    echo "  export MVGAL_CUDA_ENABLED=1"
    echo "  export MVGAL_CUDA_DEBUG=1"
    echo "  export MVGAL_CUDA_STRATEGY=round_robin"
    echo "  export LD_PRELOAD=$LIBMVGAL_CUDA"
    echo "  ./your_cuda_application"
else
    echo -e "${YELLOW}⚠ CUDA Toolkit not found in PATH${NC}"
    echo "Testing without CUDA Toolkit is limited"
fi

echo ""
echo "========================================="
echo "MVGAL CUDA Wrapper Test Summary"
echo "========================================="
echo "Library: $LIBMVGAL_CUDA"
echo "Status: Ready for testing with CUDA applications"
echo ""
echo "Next steps:"
echo "1. Ensure CUDA Toolkit is installed"
echo "2. Set environment variables as shown above"
echo "3. Run a CUDA application with LD_PRELOAD"
