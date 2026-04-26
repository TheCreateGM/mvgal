# MVGAL GUI Makefile
# Multi-Vendor GPU Aggregation Layer for Linux

CC = gcc
CFLAGS = -O2 -Wall -Wextra -I.. -I../include -I../src/userspace -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-but-set-variable -Wno-cast-function-type
LDFLAGS = -L../build -lmvgal -lpthread
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
GTK_LDFLAGS = $(shell pkg-config --libs gtk+-3.0 2>/dev/null)
GLIB_LDFLAGS = $(shell pkg-config --libs glib-2.0 2>/dev/null)

GUI_TOOLS = mvgal-gui mvgal-tray

.PHONY: all clean

all: $(GUI_TOOLS)

# Use the shared library built by CMake
mvgal-gui: mvgal-gui.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $^ $(LDFLAGS) $(GTK_LDFLAGS) $(GLIB_LDFLAGS)

mvgal-tray: mvgal-tray.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $^ $(LDFLAGS) $(GTK_LDFLAGS) $(GLIB_LDFLAGS)

clean:
	rm -f $(GUI_TOOLS)
