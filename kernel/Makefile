# SPDX-License-Identifier: GPL-2.0-only
# Out-of-tree build: make -C kernel

KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all clean modules_install

all modules:
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)" modules

clean:
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)" clean

modules_install:
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)" modules_install
