// MVGAL - Multi-Vendor GPU Aggregation Layer
// Zig Build Configuration
// SPDX-License-Identifier: MIT

const std = @import("std");

// =========================================================================
// Build Configuration
// =========================================================================

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{]);
    const optimize = b.standardOptimizeOption(.{});

    // Project configuration
    const project = struct {
        name: []const u8 = "mvgal",
        version: []const u8 = "0.2.0",
        description: []const u8 = "Multi-Vendor GPU Aggregation Layer for Linux",
    };

    // Options
    const options = b.addOptions();
    const build_kernel: bool = options.getBoolOption(
        .{
            .name = "build-kernel",
            .default_value = true,
            .description = "Build kernel module",
        },
    );
    const build_runtime: bool = options.getBoolOption(
        .{
            .name = "build-runtime",
            .default_value = true,
            .description = "Build runtime daemon",
        },
    );
    const build_tools: bool = options.getBoolOptions(
        .{
            .name = "build-tools",
            .default_value = true,
            .description = "Build tools and utilities",
        },
    );
    const build_tests: bool = options.getBoolOption(
        .{
            .name = "build-tests",
            .default_value = false,
            .description = "Build tests",
        },
    );
    const enable_sanitizers: bool = options.getBoolOption(
        .{
            .name = "enable-sanitizers",
            .default_value = false,
            .description = "Enable sanitizers (Address, Undefined)",
        },
    );

    // =========================================================================
    // Module Dependencies
    // =========================================================================

    // Cross-compile for C (used for kernel and runtime)
    const c_target = if (target.os.tag == .linux and target.arch.tag == .x86_64)
        b.standardTargetOptions(.{
            .os_tag = .linux,
            .arch_tag = .x86_64,
        })
    else
        target;

    // C module for kernel and userspace
    const c_module = b.addCModule(
        .{
            .name = "mvgal-c",
            .target = c_target,
            .optimize = optimize,
        },
    );

    // C++ module for runtime
    const cxx_module = b.addCxxModule(
        .{
            .name = "mvgal-cxx",
            .target = target,
            .optimize = optimize,
        },
    );

    // =========================================================================
    // Kernel Module
    // =========================================================================

    if (build_kernel and target.os.tag == .linux) {
        const kernel_module = b.addExecutable(
            .{
                .name = "mvgal.ko",
                .target = .{
                    .os_tag = .linux,
                    .arch_tag = target.arch.tag,
                },
                .optimize = optimize,
            },
        );

        // Add kernel source files
        kernel_module.addCSourceFile(.{
            .file = .{ .path = "kernel/mvgal_core.c" },
            .flags = &[_][]const u8{
                "-D__KERNEL__",
                "-DMODULE",
                "-DKBUILD_MODNAME=\"mvgal\"",
                "-DKBUILD_BASENAME=\"mvgal\"",
            },
        });
        kernel_module.addCSourceFile(.{ .file = .{ .path = "kernel/mvgal_device.c" } });
        kernel_module.addCSourceFile(.{ .file = .{ .path = "kernel/mvgal_scheduler.c" } });
        kernel_module.addCSourceFile(.{ .file = .{ .path = "kernel/mvgal_memory.c" } });
        kernel_module.addCSourceFile(.{ .file = .{ .path = "kernel/mvgal_sync.c" } });

        // Add vendor-specific files
        kernel_module.addCSourceFile(.{ .file = .{ .path = "kernel/vendors/mvgal_nvidia.c" } });
        kernel_module.addCSourceFile(.{ .file = .{ .path = "kernel/vendors/mvgal_amd.c" } });
        kernel_module.addCSourceFile(.{ .file = .{ .path = "kernel/vendors/mvgal_intel.c" } });
        kernel_module.addCSourceFile(.{ .file = .{ .path = "kernel/vendors/mvgal_mtt.c" } });

        // TODO: Kernel modules in Zig need special linker handling
        // This is a placeholder - actual kernel module building requires
        // integration with Kbuild or custom linker scripts
        kernel_module.linkLibCpp();
        kernel_module.setTargetName("mvgal");

        // Install kernel module
        b.installArtifact(kernel_module, .{
            .dest = b.path.join(&[_][]const u8{ &std.fs.cwd, "lib", "modules" });
        });
    }

    // =========================================================================
    // Runtime Daemon
    // =========================================================================

    if (build_runtime) {
        const daemon = b.addExecutable(
            .{
                .name = "mvgald",
                .target = target,
                .optimize = optimize,
            },
        );

        // Add C++ source files
        daemon.addCxxSourceFile(.{ .file = .{ .path = "runtime/daemon/main.cpp" } });
        daemon.addCxxSourceFile(.{ .file = .{ .path = "runtime/daemon/scheduler.cpp" } });
        daemon.addCxxSourceFile(.{ .file = .{ .path = "runtime/daemon/device_registry.cpp" } });
        daemon.addCxxSourceFile(.{ .file = .{ .path = "runtime/daemon/memory_manager.cpp" } });
        daemon.addCxxSourceFile(.{ .file = .{ .path = "runtime/daemon/power_manager.cpp" } });
        daemon.addCxxSourceFile(.{ .file = .{ .path = "runtime/daemon/metrics_collector.cpp" } });
        daemon.addCxxSourceFile(.{ .file = .{ .path = "runtime/daemon/ipc_server.cpp" } });

        // Link libraries
        daemon.linkLibCpp();
        daemon.linkLibC();
        daemon.linkSystemLibrary("drm");
        daemon.linkSystemLibrary("pci");
        daemon.linkSystemLibrary("pthread");

        // Include paths
        daemon.addIncludePath(.{ .path = "runtime/daemon" });

        // Link flags
        daemon.linkFlags = &[_][]const u8{
            "-lstdc++",
            "-ldl",
        };

        // C++ standard
        daemon.setTargetName("mvgald");
        daemon.wantLto = true;

        // Install daemon
        b.installArtifact(daemon, .{
            .dest = b.path.join(&[_][]const u8{ &std.fs.cwd, "bin" });
        });

        // =========================================================================
        // Rust Safety Components
        // =========================================================================

        // Build Rust library via cargo
        const rust_lib = b.addExternalLibrary(
            .{
                .name = "mvgal-safe",
                .path = "runtime/safe",
            },
        );
        rust_lib.linkFlavor = .{
            .name = "cargo",
            .root = "runtime/safe",
        };

        // Link Rust library to daemon (if available)
        // Note: This requires cargo to be installed and available on PATH
        // daemon.linkLibrary(rust_lib);
    }

    // =========================================================================
    // Tools
    // =========================================================================

    if (build_tools) {
        // mvgal-cli tool
        const cli = b.addExecutable(
            .{
                .name = "mvgal",
                .target = target,
                .optimize = optimize,
            },
        );

        // TODO: Add CLI tool source files
        // cli.addCSourceFile(.{ .file = .{ .path = "tools/cli/main.c" } });
        cli.linkLibC();
        cli.setTargetName("mvgal");

        b.installArtifact(cli, .{
            .dest = b.path.join(&[_][]const u8{ &std.fs.cwd, "bin" });
        });
    }

    // =========================================================================
    // Zig Components
    // =========================================================================

    // Add Zig source files if any exist
    // These are placeholders for future Zig utilities
    // const zig_utils = b.addExecutable(.{
    //     .name = "mvgal-utils",
    //     .target = target,
    //     .optimize = optimize,
    // });
    // zig_utils.addZigSourceFile(.{ .file = .{ .path = "tools/utils/main.zig" } });

    // =========================================================================
    // Tests
    // =========================================================================

    if (build_tests) {
        const test_suite = b.addTest(
            .{
                .name = "mvgal-tests",
                .target = target,
                .optimize = optimize,
            },
        );

        // TODO: Add test source files
        // test_suite.addCSourceFile(.{ .file = .{ .path = "tests/unit/test_core.c" } });
        test_suite.linkLibC();

        // Add test runner if exists
        const test_runner = b.addExecutable(
            .{
                .name = "run-tests",
                .target = target,
                .optimize = optimize,
            },
        );
        // test_runner.addCSourceFile(.{ .file = .{ .path = "tests/main.c" } });
        test_runner.linkLibC();

        b.installArtifact(test_runner, .{
            .dest = b.path.join(&[_][]const u8{ &std.fs.cwd, "bin" });
        });
    }

    // =========================================================================
    // Installation
    // =========================================================================

    // Install configuration files
    b.installArtifact(
        b.addInstallFile(
            .{
                .source_file = .{ .path = "config/mvgald.conf" },
                .install_path = .{ .path = "etc/mvgal/mvgald.conf" },
            },
        ),
        .{},
    );

    // Install udev rules
    b.installArtifact(
        b.addInstallFile(
            .{
                .source_file = .{ .path = "kernel/99-mvgal.rules" },
                .install_path = .{ .path = "etc/udev/rules.d/99-mvgal.rules" },
            },
        ),
        .{},
    );

    // =========================================================================
    // Summary
    // =========================================================================

    const summary_step = b.addBuildStep(
        .{
            .name = "summary",
            .fn = func(ctx: *std.Build.Step) error{} {
                std.debug.print(
                    \   MVGAL Zig Build Summary
                    \   ======================
                    \   Project: {s}
                    \   Version: {s}
                    \   Target: {s}
                    \   Optimize: {s}
                    \   Kernel Module: {s}
                    \   Runtime Daemon: {s}
                    \   Tools: {s}
                    \   Tests: {s}
,
                    .{
                        project.name,
                        project.version,
                        target.queryOsArch(),
                        std.mem.asCalled(.{}, @typeInfo(std.Builtin.Type, optimize)),
                        @as([]const u8, if (build_kernel) "Yes" else "No"),
                        @as([]const u8, if (build_runtime) "Yes" else "No"),
                        @as([]const u8, if (build_tools) "Yes" else "No"),
                        @as([]const u8, if (build_tests) "Yes" else "No"),
                    },
                );
            },
        },
    );

    // Add summary as a final step
    if (build_kernel or build_runtime or build_tools) {
        summary_step.dependOn(b.getInstallStep());
    }

    // =========================================================================
    // Default Build Steps
    // =========================================================================

    // Build everything by default
    const build_all_step = b.step("build-all", "Build all components");
    if (build_kernel) build_all_step.dependOn(&kernel_module.**); // workaround
    if (build_runtime) build_all_step.dependOn(&daemon.**); // workaround
    if (build_tools) build_all_step.dependOn(&cli.**); // workaround

    // Test step
    if (build_tests) {
        const test_step = b.step("test", "Run all tests");
        test_step.dependOn(&test_suite.**);
    }

    // Install step
    const install_step = b.step("install", "Install all components");
    install_step.dependOn(b.getInstallStep());

    // Run step (runs the daemon)
    if (build_runtime) {
        const run_step = b.step("run", "Run the daemon");
        run_step.dependOn(&daemon.**);
    }
}

// =========================================================================
// Helper: Query OS and Architecture
// =========================================================================

func queryOsArch(os_tag: type, arch_tag: type) []const u8 {
    const os = @tagName(os_tag);
    const arch = @tagName(arch_tag);
    return std.fmt.allocPrint(
        b.allocator,
        "{s}-{s}",
        .{ os, arch },
    );
}
