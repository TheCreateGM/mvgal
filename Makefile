# MVGAL Top-Level Makefile
# Multi-Vendor GPU Aggregation Layer for Linux

.PHONY: all clean benchmarks tools gui dbus kernel distclean

all: benchmarks tools

# Benchmarks
benchmarks:
	cd benchmarks && $(MAKE) all

# CLI Tools
tools:
	cd tools && $(MAKE) all

# GUI Tools (requires GTK3)
gui:
	cd gui && $(MAKE) all

# DBus Service (requires libdbus-1-dev)
dbus:
	cd pkg/dbus && $(CC) -o mvgal-dbus-service mvgal-dbus-service.c \
		-I../.. -I../../src/userspace/core \
		-L../../src/userspace/core -lmvgal_core -lpthread \
		$(shell pkg-config --cflags --libs dbus-1) -O2 -Wall

# Kernel Module
kernel:
	cd src/kernel && make -C /lib/modules/$$(uname -r)/build M=$$(pwd) modules

# Clean
clean:
	cd benchmarks && $(MAKE) clean || true
	cd tools && $(MAKE) clean || true
	cd gui && $(MAKE) clean || true
	cd pkg/dbus && rm -f mvgal-dbus-service || true

# Distclean - full clean
distclean: clean
	rm -f src/kernel/*.ko src/kernel/*.o src/kernel/.*.cmd src/kernel/*.mod.c
	rm -f src/userspace/*.so src/userspace/*.o
	rm -rf benchmarks/results/*.txt
	rm -rf pkg/dbus/mvgal-dbus-service
	rm -rf src/userspace/intercept/*/*.so src/userspace/intercept/*/*.o

# Tarball
tarball: clean
	mkdir -p dist
	tar -czf dist/mvgal-0.1.0.tar.gz -X .gitignore .
	@echo "Tarball created: dist/mvgal-0.1.0.tar.gz"

# Install
install: all
	mkdir -p /usr/local/bin
	cp tools/mvgal /usr/local/bin/
	cp tools/mvgal-config /usr/local/bin/
	mkdir -p /usr/local/lib
	cp src/userspace/core/libmvgal_core.a /usr/local/lib/
	cp src/userspace/libmvgal_core.so /usr/local/lib/ 2>/dev/null || true
	mkdir -p /usr/local/include/mvgal
	cp src/userspace/core/mvgal.h /usr/local/include/mvgal/
	mkdir -p /etc/mvgal
	cp config/mvgal.conf /etc/mvgal/
	cp config/99-mvgal.rules /lib/udev/rules.d/ 2>/dev/null || cp config/99-mvgal.rules /usr/lib/udev/rules.d/ 2>/dev/null || true
	# Install icon
	mkdir -p /usr/local/share/icons/hicolor/256x256/apps
	cp config/icons/hicolor/256x256/apps/mvgal.svg /usr/local/share/icons/hicolor/256x256/apps/
	# Install desktop file
	mkdir -p /usr/local/share/applications
	cp config/org.mvgal.MVGAL-GUI.desktop /usr/local/share/applications/
	@echo "MVGAL installed to /usr/local"

# Uninstall
uninstall:
	rm -f /usr/local/bin/mvgal /usr/local/bin/mvgal-config
	rm -f /usr/local/lib/libmvgal_core.so /usr/local/lib/libmvgal_core.a
	rm -f /usr/local/include/mvgal/mvgal.h
	rm -f /etc/mvgal/mvgal.conf
	rm -f /lib/udev/rules.d/99-mvgal.rules /usr/lib/udev/rules.d/99-mvgal.rules 2>/dev/null || true
	rm -f /usr/local/share/icons/hicolor/256x256/apps/mvgal.svg
	rm -f /usr/local/share/applications/org.mvgal.MVGAL-GUI.desktop
	@echo "MVGAL uninstalled from /usr/local"
