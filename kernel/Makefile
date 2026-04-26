# MVGAL Kernel Module Makefile
# SPDX-License-Identifier: GPL-2.0-only

# Kernel module name
obj-m := mvgal.o

# Source files for the module
mvgal-y := \
	mvgal_core.o \
	mvgal_device.o \
	mvgal_scheduler.o \
	mvgal_memory.o \
	mvgal_sync.o \

# Vendor-specific source files
mvgal-y += \
	vendors/mvgal_nvidia.o \
	vendors/mvgal_amd.o \
	vendors/mvgal_intel.o \
	vendors/mvgal_mtt.o \

# Kbuild file for module dependencies
Kbuild: $(src)

# Include kernel build system
include $(src)/Kbuild

# Module version and author
MODULE_AUTHOR := AxoGM
MODULE_DESCRIPTION := Multi-Vendor GPU Aggregation Layer for Linux
MODULE_LICENSE := GPL v2
MODULE_VERSION := 0.2.0

# Compiler flags
ccflags-y := \
	-Wall \
	-Wextra \
	-Werror \
	-DDEBUG \
	-DCONFIG_MVGal_MTT=1

# Optimizations
ccflags-y += -O2

# Debug flags (can be overridden by enabling DEBUG kernel config)
ifdef CONFIG_DRM_DEBUG
ccflags-y += -DDEBUG -DVERBOSE_DEBUG
endif

# Include paths
# The kernel headers are automatically included by the build system

# List of modules to build
obj-m += mvgal.o

# Extra dependencies
# If NVIDIA driver is present in the kernel tree, we could add dependencies
# For now, we keep it independent

# Clean up generated files
clean-files := \
	Module.symvers \
	modules.order \
	*.mod.c \
	*.mod.o \
	*.ko \
