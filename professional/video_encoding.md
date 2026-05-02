# MVGAL + Video Encoding/Decoding

## Overview

MVGAL routes video encode/decode workloads to the GPU with the best available
hardware encoder/decoder, and can split large batch encoding jobs across
multiple GPUs.

## FFmpeg with VAAPI (AMD/Intel)

VAAPI (Video Acceleration API) is the Linux standard for hardware video
acceleration.  MVGAL presents a unified VAAPI device that routes to the best
available GPU.

### Single-file encode

```bash
# Encode using MVGAL-selected GPU
ENABLE_MVGAL=1 ffmpeg \
    -hwaccel vaapi \
    -hwaccel_device /dev/dri/renderD128 \
    -i input.mp4 \
    -vf 'format=nv12,hwupload' \
    -c:v h264_vaapi \
    -qp 23 \
    output.mp4
```

### Batch encode across multiple GPUs

```bash
#!/usr/bin/env bash
# Distribute a list of files across available GPUs
FILES=(*.mp4)
GPU_COUNT=$(mvgal-info --json | python3 -c "import sys,json; print(json.load(sys.stdin)['gpu_count'])")

for i in "${!FILES[@]}"; do
    GPU_IDX=$((i % GPU_COUNT))
    RENDER_NODE="/dev/dri/renderD$((128 + GPU_IDX))"
    ffmpeg -hwaccel vaapi -hwaccel_device "$RENDER_NODE" \
           -i "${FILES[$i]}" -c:v h264_vaapi -qp 23 \
           "out_${i}.mp4" &
done
wait
echo "All encodes complete"
```

## FFmpeg with NVENC (NVIDIA)

```bash
ENABLE_MVGAL=1 ffmpeg \
    -hwaccel cuda \
    -hwaccel_device 0 \
    -i input.mp4 \
    -c:v h264_nvenc \
    -preset p4 \
    -cq 23 \
    output.mp4
```

MVGAL's CUDA shim ensures `hwaccel_device 0` maps to the NVIDIA GPU even in a
mixed-vendor system.

## OBS Studio

OBS uses NVENC or VAAPI for hardware encoding.  With MVGAL:

1. Open OBS → Settings → Output → Encoder.
2. Select **NVENC** (NVIDIA) or **VAAPI** (AMD/Intel).
3. MVGAL routes the encode to the appropriate GPU automatically.

No additional configuration is needed.

## GStreamer

```bash
# VAAPI encode via MVGAL
ENABLE_MVGAL=1 gst-launch-1.0 \
    filesrc location=input.mp4 ! decodebin ! \
    vaapipostproc ! vaapih264enc ! \
    mp4mux ! filesink location=output.mp4
```

## Performance Notes

- Hardware encoders are GPU-local; MVGAL selects the best encoder but does not
  split a single encode stream across GPUs.
- For batch workloads, MVGAL distributes individual files across GPUs in
  parallel, achieving near-linear scaling.
- Decode + encode pipelines can use different GPUs (decode on GPU 0, encode on
  GPU 1) with MVGAL handling the DMA-BUF transfer between them.

## Tested Configurations

| Tool | Codec | GPU Combo | Result |
|------|-------|-----------|--------|
| FFmpeg 6.x | H.264 VAAPI | AMD RX 6800 | ✓ Working |
| FFmpeg 6.x | H.265 NVENC | NVIDIA RTX 4080 | ✓ Working |
| FFmpeg 6.x | AV1 VAAPI | Intel Arc A770 | ✓ Working |
| OBS 30.x | H.264 NVENC | NVIDIA RTX 3080 | ✓ Working |
