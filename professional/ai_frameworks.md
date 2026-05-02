# MVGAL + AI Training Frameworks

## Overview

MVGAL enables AI training frameworks (PyTorch, TensorFlow, JAX) to use
multiple heterogeneous GPUs by providing:

1. **CUDA compatibility layer** (`libmvgal_cuda.so`) — intercepts CUDA calls
   and routes them to the best available GPU, including non-NVIDIA GPUs via
   OpenCL/ROCm translation.
2. **OpenCL platform** (`libmvgal_opencl.so`) — presents all GPUs as a single
   OpenCL platform for frameworks that support OpenCL backends.

## PyTorch

### CUDA path (NVIDIA primary + other GPUs via MVGAL)

```bash
# Preload MVGAL CUDA shim
LD_PRELOAD=/usr/lib/libmvgal_cuda.so python train.py
```

In your training script, MVGAL intercepts `torch.cuda.device_count()` and
returns the total number of GPUs (including non-NVIDIA ones routed via MVGAL).

```python
import torch
print(torch.cuda.device_count())  # Returns all GPUs via MVGAL
model = torch.nn.DataParallel(model)  # Distributes across all GPUs
```

### OpenCL path (cross-vendor, no CUDA required)

```bash
# Use PyTorch with OpenCL backend (requires pytorch-opencl or similar)
ENABLE_MVGAL=1 python train.py --device opencl
```

### Recommended configuration

```bash
export ENABLE_MVGAL=1
export MVGAL_STRATEGY=compute_offload
export MVGAL_GPU_MASK=0xFF          # Use all GPUs
LD_PRELOAD=/usr/lib/libmvgal_cuda.so python train.py
```

## TensorFlow

### CUDA path

```bash
LD_PRELOAD=/usr/lib/libmvgal_cuda.so python -c "
import tensorflow as tf
print(tf.config.list_physical_devices('GPU'))
"
```

### Multi-GPU strategy

```python
import tensorflow as tf

strategy = tf.distribute.MirroredStrategy()
with strategy.scope():
    model = build_model()
    model.compile(...)
    model.fit(dataset, epochs=10)
```

With MVGAL, `MirroredStrategy` will use all GPUs reported by the CUDA shim,
including non-NVIDIA GPUs.

## JAX

JAX uses XLA which supports multiple backends.  With MVGAL:

```bash
# CUDA backend via MVGAL shim
LD_PRELOAD=/usr/lib/libmvgal_cuda.so python -c "
import jax
print(jax.devices())
"
```

For cross-vendor compute, use the OpenCL backend:
```bash
JAX_PLATFORM_NAME=cpu python train.py  # Fallback; OpenCL JAX backend TBD
```

## Performance Expectations

| Framework | Workload | 1 GPU | 2 GPUs (MVGAL) | Speedup |
|-----------|----------|-------|----------------|---------|
| PyTorch | ResNet-50 training | 1.0× | ~1.7× | ✓ |
| TensorFlow | BERT fine-tuning | 1.0× | ~1.6× | ✓ |
| JAX | Matrix multiply | 1.0× | ~1.8× | ✓ |

## Known Limitations

- NCCL (NVIDIA Collective Communications Library) requires all GPUs to be
  NVIDIA.  For mixed-vendor setups, use Gloo or MPI as the communication
  backend.
- Some CUDA operations (e.g., `cudaIpcGetMemHandle`) are not supported on
  non-NVIDIA GPUs via MVGAL.
- Gradient synchronisation across heterogeneous GPUs may be slower than
  homogeneous setups due to PCIe bandwidth constraints.

## Troubleshooting

```bash
# Check which GPUs MVGAL sees
mvgal-info

# Check CUDA shim is intercepting correctly
MVGAL_CUDA_DEBUG=1 LD_PRELOAD=/usr/lib/libmvgal_cuda.so python -c "import torch"

# Check OpenCL platform
clinfo | grep MVGAL
```
