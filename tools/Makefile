# MVGAL Tools Makefile
# Multi-Vendor GPU Aggregation Layer for Linux

CC = gcc
CFLAGS = -O2 -Wall -Wextra -I.. -I../src/userspace/core -Wno-unused-variable -Wno-unused-result -Wno-unused-function
LDFLAGS = -L../src/userspace/core -lmvgal_core -lpthread

TOOLS = mvgal mvgal-config

.PHONY: all clean

all: $(TOOLS)

../src/userspace/core/libmvgal_core.a:
	@mkdir -p ../src/userspace/core
	$(CC) -c ../src/userspace/core/mvgal.c -o ../src/userspace/core/mvgal.o
	ar rcs $@ ../src/userspace/core/mvgal.o

mvgal: mvgal.c ../src/userspace/core/libmvgal_core.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -D_FORTIFY_SOURCE=2

mvgal-config: mvgal-config.c ../src/userspace/core/libmvgal_core.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TOOLS)
	rm -f ../src/userspace/core/libmvgal_core.a
	rm -f ../src/userspace/core/mvgal.o
