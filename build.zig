// MVGAL - Multi-Vendor GPU Aggregation Layer
// Zig Build Configuration
// SPDX-License-Identifier: MIT

const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // =========================================================================
    // Options
    // =========================================================================

    const build_runtime = b.option(bool, "build-runtime", "Build runtime daemon") orelse true;
    const build_tools   = b.option(bool, "build-tools",   "Build CLI tools")       orelse true;
    const build_tests   = b.option(bool, "build-tests",   "Build tests")           orelse false;

    // =========================================================================
    // Runtime Daemon (C++ sources compiled via system compiler)
    // =========================================================================

    if (build_runtime) {
        const daemon = b.addExecutable(.{
            .name   = "mvgald",
            .target = target,
            .optimize = optimize,
        });

        daemon.addCSourceFiles(.{
            .files = &.{
                "runtime/daemon/main.cpp",
                "runtime/daemon/daemon.cpp",
                "runtime/daemon/scheduler.cpp",
                "runtime/daemon/device_registry.cpp",
                "runtime/daemon/memory_manager.cpp",
                "runtime/daemon/power_manager.cpp",
                "runtime/daemon/metrics_collector.cpp",
                "runtime/daemon/ipc_server.cpp",
            },
            .flags = &.{ "-std=c++20", "-Wall", "-Wextra" },
        });

        daemon.addIncludePath(b.path("runtime/daemon"));
        daemon.addIncludePath(b.path("include"));
        daemon.linkLibCpp();
        daemon.linkLibC();
        daemon.linkSystemLibrary("drm");
        daemon.linkSystemLibrary("pthread");

        b.installArtifact(daemon);
    }

    // =========================================================================
    // CLI Tools
    // =========================================================================

    if (build_tools) {
        const tool_sources = [_]struct { name: []const u8, src: []const u8 }{
            .{ .name = "mvgal-info",   .src = "tools/mvgal-info.c" },
            .{ .name = "mvgal-status", .src = "tools/mvgal-status.c" },
            .{ .name = "mvgal-bench",  .src = "tools/mvgal-bench.c" },
            .{ .name = "mvgal-compat", .src = "tools/mvgal-compat.c" },
        };

        inline for (tool_sources) |t| {
            const exe = b.addExecutable(.{
                .name     = t.name,
                .target   = target,
                .optimize = optimize,
            });
            exe.addCSourceFile(.{
                .file  = b.path(t.src),
                .flags = &.{ "-std=c11", "-Wall", "-Wextra" },
            });
            exe.addIncludePath(b.path("include"));
            exe.linkLibC();
            exe.linkSystemLibrary("pthread");
            b.installArtifact(exe);
        }
    }

    // =========================================================================
    // OpenGL preload shim
    // =========================================================================

    const gl_shim = b.addSharedLibrary(.{
        .name     = "mvgal_gl",
        .target   = target,
        .optimize = optimize,
    });
    gl_shim.addCSourceFile(.{
        .file  = b.path("opengl/mvgal_gl_preload.c"),
        .flags = &.{ "-std=c11", "-Wall" },
    });
    gl_shim.linkLibC();
    gl_shim.linkSystemLibrary("dl");
    gl_shim.linkSystemLibrary("pthread");
    b.installArtifact(gl_shim);

    // =========================================================================
    // Frame pacer (static library)
    // =========================================================================

    const frame_pacer = b.addStaticLibrary(.{
        .name     = "mvgal_frame_pacer",
        .target   = target,
        .optimize = optimize,
    });
    frame_pacer.addCSourceFile(.{
        .file  = b.path("steam/mvgal_frame_pacer.c"),
        .flags = &.{ "-std=c11", "-Wall" },
    });
    frame_pacer.linkLibC();
    frame_pacer.linkSystemLibrary("pthread");
    b.installArtifact(frame_pacer);

    // =========================================================================
    // Tests
    // =========================================================================

    if (build_tests) {
        const test_exe = b.addExecutable(.{
            .name     = "mvgal-tests",
            .target   = target,
            .optimize = optimize,
        });
        test_exe.addCSourceFile(.{
            .file  = b.path("tools/mvgal-info.c"),
            .flags = &.{ "-std=c11", "-DMVGAL_TEST" },
        });
        test_exe.linkLibC();
        b.installArtifact(test_exe);

        const run_tests = b.addRunArtifact(test_exe);
        const test_step = b.step("test", "Run tests");
        test_step.dependOn(&run_tests.step);
    }

    // =========================================================================
    // Build steps
    // =========================================================================

    const all_step = b.step("all", "Build all components");
    all_step.dependOn(b.getInstallStep());
}
