# MVGAL Monitoring Dashboard

Qt-based monitoring and control dashboard for MVGAL.

## Components

| File | Purpose |
|------|---------|
| `mvgal_dashboard.cpp` | Main Qt application |
| `mvgal_dashboard.h` | Dashboard class header |
| `mvgal_gpu_widget.cpp` | Per-GPU utilisation widget |
| `mvgal_gpu_widget.h` | GPU widget header |
| `mvgal_rest_server.go` | REST API backend (Go) |
| `CMakeLists.txt` | Qt build configuration |

## Features

- Real-time per-GPU utilisation graphs
- VRAM usage per GPU and aggregate
- Temperature, power draw, and fan speed per GPU
- Active workload type per GPU
- Scheduler mode selector (static, dynamic, work-stealing)
- Idle optimisation controls
- Log viewer for mvgald daemon output
- REST API at `http://localhost:7474/api/v1/`

## Building

```bash
# Qt dashboard
mkdir -p ui/build && cd ui/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# REST API backend
cd ui
go build -o mvgal-rest-server ./mvgal_rest_server.go
```

## Running

```bash
# Start REST API backend
./mvgal-rest-server &

# Start Qt dashboard
./mvgal-dashboard
```

## REST API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/gpus` | GET | List all GPUs with current metrics |
| `/api/v1/gpus/{id}` | GET | Single GPU metrics |
| `/api/v1/scheduler` | GET | Current scheduler mode |
| `/api/v1/scheduler` | PUT | Set scheduler mode |
| `/api/v1/stats` | GET | Aggregate statistics |
| `/api/v1/config` | GET | Current configuration |
| `/api/v1/config` | PUT | Update configuration |
| `/api/v1/logs` | GET | Recent daemon log entries |
