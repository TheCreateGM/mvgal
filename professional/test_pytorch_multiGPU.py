#!/usr/bin/env python3
"""
MVGAL PyTorch Multi-GPU Validation Script

Tests that PyTorch can use multiple GPUs via MVGAL's CUDA compatibility layer.

Usage:
    LD_PRELOAD=/usr/lib/libmvgal_cuda.so python3 test_pytorch_multiGPU.py

SPDX-License-Identifier: MIT
"""

import sys
import time
import os

def check_mvgal():
    """Check if MVGAL is active."""
    enable = os.environ.get("ENABLE_MVGAL", "0")
    preload = os.environ.get("LD_PRELOAD", "")
    mvgal_active = enable == "1" or "libmvgal" in preload
    print(f"MVGAL active: {mvgal_active}")
    return mvgal_active


def test_gpu_detection():
    """Test that multiple GPUs are detected."""
    try:
        import torch
        gpu_count = torch.cuda.device_count()
        print(f"GPUs detected by PyTorch: {gpu_count}")
        for i in range(gpu_count):
            name = torch.cuda.get_device_name(i)
            mem = torch.cuda.get_device_properties(i).total_memory / (1024**3)
            print(f"  GPU {i}: {name} ({mem:.1f} GiB)")
        return gpu_count
    except ImportError:
        print("PyTorch not installed. Install with: pip install torch")
        return 0


def test_tensor_ops(gpu_count: int):
    """Test basic tensor operations on each GPU."""
    try:
        import torch
        results = []
        for i in range(gpu_count):
            device = torch.device(f"cuda:{i}")
            a = torch.randn(1024, 1024, device=device)
            b = torch.randn(1024, 1024, device=device)
            t0 = time.perf_counter()
            c = torch.matmul(a, b)
            torch.cuda.synchronize(device)
            t1 = time.perf_counter()
            elapsed_ms = (t1 - t0) * 1000
            results.append(elapsed_ms)
            print(f"  GPU {i}: 1024×1024 matmul in {elapsed_ms:.1f} ms")
        return results
    except Exception as e:
        print(f"Tensor ops failed: {e}")
        return []


def test_data_parallel(gpu_count: int):
    """Test DataParallel across all GPUs."""
    if gpu_count < 2:
        print("Skipping DataParallel test (need ≥2 GPUs)")
        return None

    try:
        import torch
        import torch.nn as nn

        class SimpleModel(nn.Module):
            def __init__(self):
                super().__init__()
                self.fc = nn.Linear(1024, 1024)

            def forward(self, x):
                return self.fc(x)

        model = SimpleModel().cuda()
        model = nn.DataParallel(model)

        batch = torch.randn(256, 1024).cuda()
        t0 = time.perf_counter()
        for _ in range(100):
            out = model(batch)
        torch.cuda.synchronize()
        t1 = time.perf_counter()

        elapsed_ms = (t1 - t0) * 1000
        print(f"  DataParallel (100 forward passes): {elapsed_ms:.1f} ms")
        return elapsed_ms
    except Exception as e:
        print(f"DataParallel test failed: {e}")
        return None


def test_single_gpu_baseline():
    """Baseline: same workload on single GPU."""
    try:
        import torch
        import torch.nn as nn

        class SimpleModel(nn.Module):
            def __init__(self):
                super().__init__()
                self.fc = nn.Linear(1024, 1024)

            def forward(self, x):
                return self.fc(x)

        model = SimpleModel().cuda(0)
        batch = torch.randn(256, 1024).cuda(0)
        t0 = time.perf_counter()
        for _ in range(100):
            out = model(batch)
        torch.cuda.synchronize()
        t1 = time.perf_counter()

        elapsed_ms = (t1 - t0) * 1000
        print(f"  Single GPU baseline (100 forward passes): {elapsed_ms:.1f} ms")
        return elapsed_ms
    except Exception as e:
        print(f"Single GPU baseline failed: {e}")
        return None


def main():
    print("=" * 60)
    print("MVGAL PyTorch Multi-GPU Validation")
    print("=" * 60)
    print()

    mvgal_active = check_mvgal()
    print()

    print("1. GPU Detection")
    print("-" * 40)
    gpu_count = test_gpu_detection()
    print()

    if gpu_count == 0:
        print("No GPUs available. Exiting.")
        sys.exit(1)

    print("2. Per-GPU Tensor Operations")
    print("-" * 40)
    per_gpu_times = test_tensor_ops(gpu_count)
    print()

    print("3. Single GPU Baseline")
    print("-" * 40)
    single_time = test_single_gpu_baseline()
    print()

    print("4. DataParallel Multi-GPU")
    print("-" * 40)
    multi_time = test_data_parallel(gpu_count)
    print()

    print("=" * 60)
    print("Summary")
    print("=" * 60)
    print(f"GPUs: {gpu_count}")
    print(f"MVGAL active: {mvgal_active}")

    if single_time and multi_time and gpu_count >= 2:
        speedup = single_time / multi_time
        print(f"Speedup (DataParallel vs single GPU): {speedup:.2f}×")
        if speedup >= 1.5:
            print("✓ Meets 1.5× target")
        else:
            print(f"✗ Below 1.5× target ({speedup:.2f}×)")
    print()


if __name__ == "__main__":
    main()
