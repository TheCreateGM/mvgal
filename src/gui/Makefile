# MVGAL GUI Makefile
# Multi-Vendor GPU Aggregation Layer for Linux

CC = gcc
CFLAGS = -O2 -Wall -Wextra -I.. -I../src/userspace/core -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-but-set-variable -Wno-cast-function-type
LDFLAGS = -L../src/userspace/core -lmvgal_core -lpthread
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
GTK_LDFLAGS = $(shell pkg-config --libs gtk+-3.0 2>/dev/null)
GLIB_LDFLAGS = $(shell pkg-config --libs glib-2.0 2>/dev/null)

GUI_TOOLS = mvgal-gui mvgal-tray

.PHONY: all clean

all: $(GUI_TOOLS)

../src/userspace/core/libmvgal_core.a:
	@mkdir -p ../src/userspace/core
	$(CC) $(CFLAGS) -c ../src/userspace/core/mvgal.c -o ../src/userspace/core/mvgal.o
	ar rcs $@ ../src/userspace/core/mvgal.o

mvgal-gui: mvgal-gui.c ../src/userspace/core/libmvgal_core.a
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $^ $(LDFLAGS) $(GTK_LDFLAGS) $(GLIB_LDFLAGS)

mvgal-tray: mvgal-tray.c ../src/userspace/core/libmvgal_core.a
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $^ $(LDFLAGS) $(GTK_LDFLAGS) $(GLIB_LDFLAGS)

clean:
	rm -f $(GUI_TOOLS)
	rm -f ../src/userspace/core/libmvgal_core.a
	rm -f ../src/userspace/core/mvgal.o
