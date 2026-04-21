# MVGAL Security Policy

![Version](https://img.shields.io/badge/version-0.2.0-%2376B900?style=for-the-badge)
![Status](https://img.shields.io/badge/status-Active-%234CAF50?style=for-the-badge)

**Project:** Multi-Vendor GPU Aggregation Layer for Linux (MVGAL)
**Last Updated:** April 21, 2026
**Contact:** creategm10@proton.me

---

## 🔐 Supported Versions

Security updates and patches are provided for the following versions:

| Version | Supported          | Notes |
| ------- | ------------------ | ----- |
| **0.2.x**   | ✅ **Actively Supported** | Latest stable release |
| 0.1.x   | ✅ Supported | Previous release (critical fixes only) |
| < 0.1   | ❌ Unsupported | No security updates |

---

## 🚨 Reporting a Vulnerability

**Please do NOT report security vulnerabilities through public GitHub issues.**

To report a security vulnerability in MVGAL:

1. **Email:** Report to **creategm10@proton.me** with the subject line `[SECURITY] MVGAL Vulnerability`
2. **Include:**
   - Version of MVGAL affected
   - Detailed description of the vulnerability
   - Steps to reproduce
   - Impact assessment
   - Any relevant logs or proof-of-concept code

3. **Response Timeline:**
   - Initial response within **48 hours**
   - Vulnerability assessment within **72 hours**
   - Patch or mitigation timeline provided based on severity
   - Public disclosure after fix is available (coordinated with reporter)

---

## 🛡️ Security Best Practices

### For Users
- Always run the latest version of MVGAL
- Install from official sources only
- Review the source code if building from source
- Monitor system logs when running MVGAL in production
- Use kernel module only from trusted sources

### For Developers
- All contributions must follow secure coding practices
- Vulkan/OpenCL/CUDA interception must not leak sensitive data
- Memory management must prevent use-after-free and double-free
- Thread safety must be maintained in all public APIs
- All external inputs must be validated

---

## 📋 Security Features

MVGAL implements the following security measures:

| Feature | Status | Description |
|---------|--------|-------------|
| **Privilege Separation** | ✅ | Daemon runs with minimal privileges |
| **Input Validation** | ✅ | All external inputs validated |
| **Thread Safety** | ✅ | Mutexes and atomics used appropriately |
| **Memory Safety** | ✅ | Reference counting, no manual memory management in hot paths |
| **kernel Module Isolation** | ✅ | Optional kernel module with limited interface |
| **LD_PRELOAD Safety** | ✅ | Careful handling of intercepted functions |

---

## 📚 Resources

- [MVGAL GitHub](https://github.com/TheCreateGM/mvgal)
- [Security Advisories](https://github.com/TheCreateGM/mvgal/security/advisories)
- [Code of Conduct](CODE_OF_CONDUCT.md)

---

*© 2026 MVGAL Project. Version 0.2.0 "Health Monitor". All Rights Reserved.*
