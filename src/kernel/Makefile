# Makefile for MVGAL Kernel Module
# Build with: make -C /lib/modules/$(uname -r)/build M=$(PWD) modules

obj-m := mvgal.o
mvgal-objs := mvgal_kernel.o

# Kernel module flags
KVERSION := $(shell uname -r)
KSRC := /lib/modules/$(KVERSION)/build

# Additional compiler flags
EXTRA_CFLAGS += -DDEBUG
EXTRA_CFLAGS += -Wno-unused-variable
EXTRA_CFLAGS += -Wno-unused-function
EXTRA_CFLAGS += -Wno-unused-but-set-variable

# Include kernel build system
all:
	$(MAKE) -C $(KSRC) M=$(PWD) modules

# Clean
clean:
	$(MAKE) -C $(KSRC) M=$(PWD) clean
	rm -f *.o *.ko *.mod.*

# Install
install:
	$(MAKE) -C $(KSRC) M=$(PWD) modules_install
	depmod -a $(KVERSION)

# Uninstall
uninstall:
	rm -f /lib/modules/$(KVERSION)/extra/mvgal.ko
	depmod -a $(KVERSION)

# Load module
load:
	sudo insmod mvgal.ko
	lsmod | grep mvgal

# Unload module
unload:
	sudo rmmod mvgal.ko

.PHONY: all clean install uninstall load unload
