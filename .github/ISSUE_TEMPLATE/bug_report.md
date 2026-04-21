---
name: Bug report
about: Create a report to help us improve MVGAL
title: '[Bug] '
labels: 'bug, needs-triage'
assignees: ''
---

![Version](https://img.shields.io/badge/version-0.2.0-%2376B900?style=flat-square)
**MVGAL Bug Report** | **Version:** 0.2.0 "Health Monitor" | **Last Updated:** April 21, 2026

---

## 🐛 Describe the bug

A clear and concise description of what the bug is.

**Example:**
> When running a Vulkan application with MVGAL enabled, the application crashes with a segmentation fault in vkCreateDevice.

---

## 🔍 To Reproduce

Steps to reproduce the behavior:

1. **Step 1** - [e.g., Enable MVGAL with `export MVGAL_ENABLED=1`]
2. **Step 2** - [e.g., Set strategy to AFR with `export MVGAL_STRATEGY=afr`]
3. **Step 3** - [e.g., Run `vkcube`]
4. **Step 4** - [e.g., See segmentation fault]

**Reproducibility:**
- [ ] Always happens
- [ ] Happens sometimes (please describe when)
- [ ] Could not reproduce after initial report
- [ ] Only happens with specific configuration

---

## ✅ Expected behavior

A clear and concise description of what you expected to happen.

**Example:**
> The application should run without crashing and display the cube properly.

---

## 📎 Additional Files

If applicable, attach or reference:

- [ ] Log files (`/var/log/mvgal/mvgal.log`)
- [ ] Core dumps
- [ ] Configuration files (`/etc/mvgal/mvgal.conf`)
- [ ] Screenshots or videos
- [ ] Valgrind/memcheck output
- [ ] Stack traces (use `gdb` or `bt`)

---

## 🖥️ Environment Information

Please complete the following information:

| Field | Value |
|-------|-------|
| **OS** | [e.g., Ubuntu 24.04, Fedora 41, Arch Linux] |
| **MVGAL Version** | [e.g., 0.2.0, or `git rev-parse --short HEAD`] |
| **Installation Method** | [Source, Debian .deb, RPM .rpm, Flatpak, Snap] |
| **Kernel Version** | `uname -r` output: [e.g., 6.19.0-arch1-1] |
| **Architecture** | [x86_64, arm64, other] |
| **Compiler** | [GCC 13.2.0, Clang 17.0.0, other] |

### GPU Hardware

List all GPUs in your system:

```
GPU 0: [Vendor: AMD/NVIDIA/Intel/Moore Threads] [Model: e.g., RX 7900 XT]
GPU 1: [Vendor] [Model: e.g., RTX 4090]
...
```

### GPU Drivers

| GPU | Driver | Version |
|-----|--------|---------|
| GPU 0 | [e.g., amdgpu, nvidia] | [e.g., 24.3.0, 550.127.05] |
| GPU 1 | | |

---

## 📊 MVGAL Configuration

Please provide your MVGAL configuration:

**Environment Variables:**
```bash
# Paste output of: env | grep MVGAL
MVGAL_ENABLED=1
MVGAL_STRATEGY=afr
MVGAL_LOG_LEVEL=3
...
```

**Configuration File:**
```ini
# Paste contents of /etc/mvgal/mvgal.conf or ~/.config/mvgal/mvgal.conf
[general]
enabled = true
...
```

**Daemon Status:**
```bash
# Paste output of: systemctl status mvgal-daemon 2>&1 | head -20
```

---

## 🔬 Debug Information

### Enable Debug Logging

To capture debug information:

```bash
# Enable verbose logging
export MVGAL_LOG_LEVEL=5
export MVGAL_DEBUG=1

# Reproduce the issue
your_application 2>&1 | tee mvgal_debug.log

# Or save to file
export MVGAL_LOG_FILE=/tmp/mvgal_error.log
./your_application
```

### Log Output

Attach the relevant portion of your logs:

```
[2026-04-21 12:00:00] [ERROR] [gpu_manager.c:123] Failed to detect GPU: Resource temporarily unavailable
[2026-04-21 12:00:00] [DEBUG] [vk_layer.c:456] Intercepted vkCreateInstance call
...
```

---

## 💡 Possible Workarounds

Have you found any workarounds? If so, please describe:

- [ ] Disabling MVGAL (`export MVGAL_ENABLED=0`)
- [ ] Using a different distribution strategy
- [ ] Disabling specific GPUs
- [ ] Using a different version of MVGAL
- [ ] Other (describe): _______________

---

## ✅ Additional Context

Add any other context about the problem here:

- When did the issue first appear?
- Has it always occurred, or did it start after a specific change?
- Does it occur with all applications, or only specific ones?
- Any other software running that might interact with GPUs?

---

## 📚 Related Information

- [Troubleshooting Guide](https://github.com/TheCreateGM/mvgal#-troubleshooting) (in README.md)
- [Steam Integration Guide](https://github.com/TheCreateGM/mvgal/blob/main/docs/STEAM.md)
- [Build & Test Guide](https://github.com/TheCreateGM/mvgal/blob/main/docs/BUILDworkspace.md)

---

**Thank you for taking the time to report this issue! 🙏**

Your detailed bug report helps us improve MVGAL for everyone.

---

*© 2026 MVGAL Project. Version 0.2.0 "Health Monitor".*
