# MVGAL Tools Makefile
# Multi-Vendor GPU Aggregation Layer for Linux
# SPDX-License-Identifier: MIT

CC      = gcc
CFLAGS  = -std=c11 -O2 -Wall -Wextra -I.. -I../include \
          -Wno-unused-variable -Wno-unused-result -Wno-unused-function \
          -D_GNU_SOURCE -D_FORTIFY_SOURCE=2
LDFLAGS = -L../build -lmvgal -lpthread

# Tools that link against libmvgal
MVGAL_TOOLS = mvgal mvgal-config

# Standalone tools (no libmvgal dependency — use sysfs directly)
STANDALONE_TOOLS = mvgal-info mvgal-status mvgal-bench mvgal-compat

ALL_TOOLS = $(MVGAL_TOOLS) $(STANDALONE_TOOLS)

.PHONY: all standalone clean install

all: $(ALL_TOOLS)

standalone: $(STANDALONE_TOOLS)

# Tools that require libmvgal
mvgal: mvgal.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

mvgal-config: mvgal-config.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Standalone tools
mvgal-info: mvgal-info.c
	$(CC) $(CFLAGS) -o $@ $^

mvgal-status: mvgal-status.c
	$(CC) $(CFLAGS) -o $@ $^

mvgal-bench: mvgal-bench.c
	$(CC) $(CFLAGS) -o $@ $^ -lpthread -lm

mvgal-compat: mvgal-compat.c
	$(CC) $(CFLAGS) -o $@ $^

install: $(STANDALONE_TOOLS)
	install -Dm755 mvgal-info    $(DESTDIR)/usr/bin/mvgal-info
	install -Dm755 mvgal-status  $(DESTDIR)/usr/bin/mvgal-status
	install -Dm755 mvgal-bench   $(DESTDIR)/usr/bin/mvgal-bench
	install -Dm755 mvgal-compat  $(DESTDIR)/usr/bin/mvgal-compat

clean:
	rm -f $(ALL_TOOLS)
