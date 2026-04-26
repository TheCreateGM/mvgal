# MVGAL Top-Level Makefile
# Multi-Vendor GPU Aggregation Layer for Linux

.PHONY: all clean benchmarks tools gui dbus kernel distclean cmake-build

all: cmake-build

# Build using CMake (primary build system)
cmake-build:
	mkdir -p build
	cd build && cmake .. -DCMAKE_BUILD_TYPE=Release
	cd build && make -j$(nproc)

# Benchmarks
benchmarks:
	cd misc/benchmarks && $(MAKE) all

# CLI Tools (for development testing - uses CMake output)
tools: cmake-build
	cd tools && $(MAKE) all

# GUI Tools (requires GTK3) - uses CMake output
gui: cmake-build
	cd src/gui && $(MAKE) all

# DBus Service (requires libdbus-1-dev) - uses CMake output
dbus: cmake-build
	cd src/pkg/dbus && $(CC) -o mvgal-dbus-service mvgal-dbus-service.c \
		-I../.. -I../../include \
		-L../../build -lmvgal -lpthread \
		$(shell pkg-config --cflags --libs dbus-1) -O2 -Wall

# Kernel Module
kernel:
	cd src/kernel && make -C /lib/modules/$$(uname -r)/build M=$$(pwd) modules

# Clean
clean:
	cd misc/benchmarks && $(MAKE) clean || true
	cd tools && $(MAKE) clean || true
	cd src/gui && $(MAKE) clean || true
	cd src/pkg/dbus && rm -f mvgal-dbus-service || true
	cd build && make clean || true

# Distclean - full clean
distclean: clean
	rm -rf build
	rm -f src/kernel/*.ko src/kernel/*.o src/kernel/.*.cmd src/kernel/*.mod.c
	rm -rf misc/benchmarks/results/*.txt

# Tarball
tarball: clean
	mkdir -p dist
	tar -czf dist/mvgal-$(shell cat include/mvgal/mvgal_version.h | grep MVGAL_VERSION_STRING | awk '{print $$3}' | tr -d '"').tar.gz -X .gitignore .
	@echo "Tarball created: dist/mvgal-*.tar.gz"

# Install
install: cmake-build
	mkdir -p $(DESTDIR)/usr/local/bin
	cp build/libmvgal.so $(DESTDIR)/usr/local/bin/ || cp build/libmvgal.so $(DESTDIR)/usr/local/bin/ 2>/dev/null || true
	cp tools/mvgal $(DESTDIR)/usr/local/bin/ || true
	cp tools/mvgal-config $(DESTDIR)/usr/local/bin/ || true
	mkdir -p $(DESTDIR)/usr/local/lib
	cp build/libmvgal.so $(DESTDIR)/usr/local/lib/ 2>/dev/null || true
	mkdir -p $(DESTDIR)/usr/local/include/mvgal
	cp -r include/mvgal/*.h $(DESTDIR)/usr/local/include/mvgal/
	mkdir -p $(DESTDIR)/etc/mvgal
	cp config/mvgal.conf $(DESTDIR)/etc/mvgal/
	cp config/99-mvgal.rules /lib/udev/rules.d/ 2>/dev/null || cp config/99-mvgal.rules /usr/lib/udev/rules.d/ 2>/dev/null || true
	# Install icon
	mkdir -p $(DESTDIR)/usr/local/share/icons/hicolor/256x256/apps
	cp assets/mvgal.svg $(DESTDIR)/usr/local/share/icons/hicolor/256x256/apps/ || cp config/icons/hicolor/256x256/apps/mvgal.svg $(DESTDIR)/usr/local/share/icons/hicolor/256x256/apps/ 2>/dev/null || true
	# Install desktop file
	mkdir -p $(DESTDIR)/usr/local/share/applications
	cp config/org.mvgal.MVGAL-GUI.desktop $(DESTDIR)/usr/local/share/applications/ 2>/dev/null || true
	@echo "MVGAL installed to /usr/local"

# Uninstall
uninstall:
	rm -f /usr/local/bin/mvgal /usr/local/bin/mvgal-config
	rm -f /usr/local/lib/libmvgal.so
	rm -rf /usr/local/include/mvgal
	rm -f /etc/mvgal/mvgal.conf
	rm -f /lib/udev/rules.d/99-mvgal.rules /usr/lib/udev/rules.d/99-mvgal.rules 2>/dev/null || true
	rm -f /usr/local/share/icons/hicolor/256x256/apps/mvgal.svg
	rm -f /usr/local/share/applications/org.mvgal.MVGAL-GUI.desktop
	@echo "MVGAL uninstalled from /usr/local"
