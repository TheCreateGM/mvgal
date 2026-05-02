# Contributing to MVGAL

Thank you for your interest in contributing to MVGAL (Multi-Vendor GPU Aggregation Layer for Linux).

---

## Getting Started

1. Fork the repository on GitHub
2. Clone your fork: `git clone https://github.com/YOUR_USERNAME/mvgal.git`
3. Install dependencies: `bash scripts/install_dependencies.sh`
4. Build: `cmake -B build_output && cmake --build build_output --parallel $(nproc)`
5. Run tests: `cd build_output && ctest --output-on-failure`

---

## Code Style

### C / C++

- C17 for userspace library, C++20 for the runtime daemon
- 4-space indentation, no tabs
- `snake_case` for functions and variables, `UPPER_CASE` for macros and constants
- All public API functions prefixed with `mvgal_`
- All public types suffixed with `_t`
- Compile with `-Wall -Wextra -Werror` — zero warnings policy
- Format with `clang-format` (config in `.clang-format` if present)

### Rust

- Edition 2021, MSRV 1.75
- `cargo fmt --all` before committing
- `cargo clippy --all-targets --all-features -- -D warnings` must pass
- All `unsafe` blocks must have a `// SAFETY:` comment explaining why it is safe
- FFI functions must be `#[no_mangle] extern "C"`

### Shell Scripts

- `#!/usr/bin/env bash` shebang
- `set -euo pipefail` at the top
- All privileged operations use `pkexec` — **never `sudo`**
- `shellcheck` must pass with `--severity=warning`

---

## Privileged Operations

**All scripts and installers must use `pkexec` instead of `sudo`.**

```bash
# Correct
pkexec modprobe mvgal
pkexec cp file /etc/udev/rules.d/

# Wrong — never use sudo
sudo modprobe mvgal
```

This applies to: `build/install.sh`, `scripts/install_dependencies.sh`, `config/load-module.sh`, `config/unload-module.sh`, and any new scripts.

---

## Commit Messages

Use conventional commits format:

```
type(scope): short description

Longer description if needed.

- Bullet points for multiple changes
```

Types: `feat`, `fix`, `docs`, `refactor`, `test`, `ci`, `chore`

Examples:
```
feat(scheduler): add work-stealing mode for idle GPUs
fix(vulkan): handle NULL pName in vkGetDeviceProcAddr
docs(api): document mvgal_memory_replicate flags
```

---

## Pull Requests

- Push to a new branch, never directly to `main`
- PR title under 70 characters
- Include what was changed, what was tested, and any known limitations
- All CI checks must pass (run manually via Actions → Run workflow)
- One logical change per PR

---

## Adding a New GPU Vendor

1. Add vendor ops in `kernel/vendors/mvgal_<vendor>.c`:
   - Implement all 10 functions in `struct mvgal_vendor_ops`
   - Add PCI device IDs to `kernel/mvgal_core.c`

2. Add vendor detection in `src/userspace/daemon/gpu_manager.c`:
   - Add PCI vendor ID to the detection logic
   - Add sysfs paths for metrics (utilization, VRAM, temperature)

3. Add vendor to `include/mvgal/mvgal_types.h`:
   - Add to `mvgal_vendor_t` enum

4. Document in `docs/DRIVER_INTEGRATION.md`

---

## Adding a New Scheduling Strategy

1. Create `src/userspace/scheduler/strategy/my_strategy.c`
2. Add to `mvgal_distribution_strategy_t` in `include/mvgal/mvgal_types.h`
3. Register in `src/userspace/scheduler/scheduler.c`
4. Add to `tools/mvgal-config.c` string conversion
5. Document in `docs/ARCHITECTURE.md`

---

## Testing

- Add unit tests for new C code in `src/tests/tests/unit/`
- Add Rust unit tests in the crate's `src/lib.rs` under `#[cfg(test)]`
- Run: `ctest --output-on-failure` and `cargo test`
- Benchmark new features with `mvgal-bench`

---

## Documentation

- Update `docs/API.md` for any new public API functions
- Update `docs/STATUS.md` for completion status changes
- Update `docs/ARCHITECTURE.md` for architectural changes
- Keep `docs/MISSING.md` current

---

## Security

See [SECURITY.md](SECURITY.md) for the vulnerability disclosure policy. Do not open public issues for security vulnerabilities.

---

## License

By contributing, you agree that your contributions will be licensed under:
- **GPL-2.0-only** for kernel module code (`kernel/`)
- **MIT** for all other code
