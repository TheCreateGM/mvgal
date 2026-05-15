# SPDX-License-Identifier: GPL-2.0-only
# Legacy path: build the canonical module from kernel/

.PHONY: all clean modules_install

all modules:
	$(MAKE) -C ../../kernel

clean:
	$(MAKE) -C ../../kernel clean

modules_install:
	$(MAKE) -C ../../kernel modules_install
