# MVGAL Tools Makefile
# Multi-Vendor GPU Aggregation Layer for Linux

CC = gcc
CFLAGS = -O2 -Wall -Wextra -I.. -I../include -Wno-unused-variable -Wno-unused-result -Wno-unused-function
LDFLAGS = -L../build -lmvgal -lpthread

TOOLS = mvgal mvgal-config

.PHONY: all clean

all: $(TOOLS)

# Use the shared library built by CMake
mvgal: mvgal.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -D_FORTIFY_SOURCE=2

mvgal-config: mvgal-config.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TOOLS)
