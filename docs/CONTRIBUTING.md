# Contributing to MVGAL

> Multi-Vendor GPU Aggregation Layer for Linux

SPDX-License-Identifier: MIT

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Development Setup](#development-setup)
3. [Code Style Guidelines](#code-style-guidelines)
4. [Coding Standards](#coding-standards)
5. [Testing](#testing)
6. [Submitting Changes](#submitting-changes)
7. [Review Process](#review-process)
8. [Maintainer Guidelines](#maintainer-guidelines)
9. [Security Vulnerabilities](#security-vulnerabilities)

---

## Getting Started

Thank you for your interest in contributing to MVGAL! We welcome contributions from everyone, regardless of experience level.

### Ways to Contribute

- **Code**: Implement new features, fix bugs, improve performance
- **Documentation**: Write docs, improve existing documentation, add examples
- **Testing**: Report bugs, write test cases, improve test coverage
- **Design**: Review architecture decisions, propose improvements
- **Community**: Help others, answer questions, improve onboarding

### First Steps

1. Read this document (CONTRIBUTING.md)
2. Read the [Architecture Overview](ARCHITECTURE.md)
3. Read the [Build Guide](BUILD.md)
4. Join the community discussions (Discord, mailing list)
5. Browse open issues and pull requests

---

## Development Setup

### Prerequisites

See [BUILD.md](BUILD.md) for detailed dependency requirements.

**Minimum Requirements:**
- Linux kernel 5.4+
- GCC 10+ or Clang 12+
- CMake 3.16+
- libdrm development headers
- libpci development headers

**Recommended:**
- Rust 1.60+ (for safety components)
- Python 3.8+ (for scripts)
- lit (for test suite)

### Clone the Repository

```bash
# Clone the repository
git clone https://github.com/mvgal/mvgal.git
cd mvgal

# Initialize submodules (if any)
git submodule update --init --recursive
```

### Build from Source

```bash
# Create and enter build directory
mkdir build && cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Debug -DMVGAL_ENABLE_RUST=ON

# Build
cmake --build . -j$(nproc)
```

### Development Workflow

Most developers use the following workflow:

1. **Configure for development:**
   ```bash
   cmake .. -DCMAKE_BUILD_TYPE=Debug \
       -DMVGAL_ENABLE_SANITIZERS=ON \
       -DMVGAL_ENABLE_COVERAGE=ON \
       -DMVGAL_BUILD_TESTS=ON
   ```

2. **Build incrementally:**
   ```bash
   # Build specific target
   cmake --build . --target mvgald
   
   # Or build everything
   cmake --build .
   ```

3. **Run tests:**
   ```bash
   ctest --output-on-failure
   ```

4. **Clean build:**
   ```bash
   cmake --build . --clean-first
   ```

---

## Code Style Guidelines

### General Principles

- **Readability First**: Code should be easy to read and understand
- **Consistency**: Follow existing patterns in the codebase
- **Maintainability**: Write code that can be maintained by others
- **Documentation**: Comment non-obvious decisions and complex logic

### Language Conventions

#### C Code (Kernel Module)

- Follow Linux kernel coding style (see `Documentation/process/coding-style.rst`)
- Use `snake_case` for functions and variables
- Use `MACRO_CASE` for macros and constants
- Limit lines to 80 characters (kernel convention)
- Use tabs for indentation (kernel convention)

#### C++ Code (Runtime)

- Follow Google C++ Style Guide with exceptions:
  - Use `snake_case` for variables and functions
  - Use `PascalCase` for types and classes
  - Use `m_` prefix for member variables
  - Use `CONSTANT_CASE` for macros and enum values
- Use 4 spaces for indentation
- Limit lines to 100 characters
- Prefer `constexpr` over `#define`
- Prefer `enum class` over plain `enum`
- Use `nullptr` instead of `NULL` or `0`
- Use smart pointers (`unique_ptr`, `shared_ptr`) where appropriate

#### Rust Code (Safety Components)

- Follow Rust API Guidelines (Rust RFC 430)
- Use `snake_case` for variables and functions
- Use `PascalCase` for types, traits, and enums
- Use `SCREAMING_SNAKE_CASE` for constants
- Document all public items with `///` doc comments
- Use `clippy` for linting

#### Zig Code (Utilities)

- Follow Zig style conventions
- Use `snake_case` for variables and functions
- Use `PascalCase` for types
- Use `SCREAMING_SNAKE_CASE` for constants

### Naming Conventions

| Entity | Prefix/Suffix | Example |
|--------|---------------|---------|
| Kernel Module | `mvgal_` | `mvgal_enumerate_gpus()` |
| C++ Class | `mvgal::` namespace | `mvgal::DeviceRegistry` |
| Rust Module | `mvgal::` | `mvgal::fence_manager` |
| Public API | `mvgal_` | `mvgal_init()` |
| Internal API | (none, in anonymous namespace) | N/A |
| Test Function | `test_` | `test_memory_allocation()` |
| Test File | `test_*.c/cpp` | `test_memory.cpp` |

### File Organization

- Headers use `.h` (C) or `.hpp` (C++)
- Source files use `.c` (C) or `.cpp` (C++)
- Each major component has its own directory
- Header guards use `MVGAL_<PATH>_H` or `MVGAL_<PATH>_HPP`
- SPDX license identifiers at top of every file

---

## Coding Standards

### Error Handling

**C (Kernel):**
- Return negative errno values for errors
- Use ` PTR_ERR` or ` IS_ERR` for pointer errors
- Check return values from all non-void functions

**C++:**
- Use exceptions for error handling
- Use `std::error_code` or custom error types for API boundaries
- Catch exceptions at the top level (daemon entry point)

**Rust:**
- Use `Result<T, E>` for fallible operations
- Use custom error types with `thiserror` or `anyhow`
- Provide meaningful error messages

### Memory Management

**C:**
- Use kernel allocators (`kmalloc`, `kzalloc`, `vmalloc`)
- Always check for NULL returns
- Use `devm_*` variants for device-managed memory when possible

**C++:**
- Prefer stack allocation for small objects
- Use `std::unique_ptr` for exclusive ownership
- Use `std::shared_ptr` for shared ownership
- Use `std::weak_ptr` to break circular dependencies
- Avoid raw `new`/`delete` in library code

**Rust:**
- Let the borrow checker do its job
- Use `Rc`/`Arc` only when necessary
- Prefer stack allocation

### Thread Safety

- **C**: Use kernel mutexes, spinlocks, and RCU as appropriate
- **C++**: Use `std::mutex` and `std::lock_guard`/`std::unique_lock`
- **Rust**: Use `Mutex`, `RwLock`, or atomic operations
- **General**: Document locking requirements in function comments
- Always initialize locks before use
- Keep critical sections as short as possible
- Avoid holding locks across function calls when possible

### Resource Management

- Use RAII pattern (C++/Rust) for resource management
- Ensure all resources are properly released on error paths
- Use `defer` pattern (Go-style) for cleanup in C when possible
- Close file descriptors, free memory, release locks

### Security Considerations

- Always validate user input (especially in kernel mode)
- Check buffer sizes and pointer arithmetic
- Use safe string functions (`strncpy`, `snprintf`, etc.)
- Initialize all variables before use
- Zero sensitive memory before freeing
- Validate file paths and canonicalize them
- Use proper file permissions (0644, 0755, etc.)

---

## Testing

### Test Structure

Tests are organized in the `tests/` directory:

```
tests/
├── unit/           # Unit tests
├── integration/    # Integration tests
└── stress/         # Stress/endurance tests
```

### Writing Tests

**C Tests:**
- Use ` Damon` test framework or custom harness
- Test one thing per test function
- Use clear naming: `test_<component>_<scenario>`

**C++ Tests:**
- Use Google Test or Catch2
- Use fixture classes for complex setup
- Test edge cases and error conditions

**Rust Tests:**
- Use built-in `#[test]` attribute
- Separate unit tests (in module) and integration tests (in `tests/`)

### Running Tests

```bash
# All tests
ctest --output-on-failure

# Specific test
ctest -R test_name

# With valgrind (memory check)
ctest --output-on-failure --tests-regex test_name --extra-verbose

# Coverage report (with gcov)
cmake --build . --target coverage
```

### Test Coverage

We aim for minimum 80% code coverage for critical components.

```bash
# Enable coverage
cmake .. -DMVGAL_ENABLE_COVERAGE=ON
cmake --build .

# Generate report
cmake --build . --target coverage
```

---

## Submitting Changes

### Pre-Submission Checklist

- [ ] Code compiles without warnings
- [ ] All existing tests pass
- [ ] New tests added for new functionality
- [ ] Documentation updated (if applicable)
- [ ] Code follows style guidelines
- [ ] Commit messages are clear and descriptive
- [ ] Signed-off-by added (see below)

### Commit Guidelines

#### Commit Messages

Follow the [Conventional Commits](https://www.conventionalcommits.org/) style:

```
<type>(<scope>): <description>

<body>

<footer>
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation only changes
- `style`: Code style changes (formatting, etc.)
- `refactor`: Code refactoring (no functional changes)
- `perf`: Performance improvements
- `test`: Adding or fixing tests
- `build`: Build system or dependency changes
- `ci`: CI configuration changes
- `chore`: Miscellaneous changes that don't fit above

**Example:**
```
feat(kernel): add AMD GPU support via amdgpu driver

- Implement mvgal_amd_probe() for device detection
- Add DMA-BUF export/import for AMD GPUs
- Tested on RDNA2 and CDNA2 architectures

Signed-off-by: John Doe <john@example.com>
```

#### Sign Your Work

All commits must be signed-off using `-s` or `--signoff`:

```bash
git commit -s -m "Your commit message"
```

This certifies that you agree to the [Developer Certificate of Origin (DCO)](https://developercertificate.org/):

```
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```

### Creating a Pull Request

1. **Fork the repository** on GitHub
2. **Create a feature branch**:
   ```bash
   git checkout -b feat/my-awesome-feature
   ```
3. **Make your changes** and commit them:
   ```bash
   git add .
   git commit -s -m "feat: my awesome feature"
   ```
4. **Push to your fork**:
   ```bash
   git push origin feat/my-awesome-feature
   ```
5. **Open a Pull Request** on GitHub

### Pull Request Template

```markdown
## Description

<Brief description of what this PR does>

## Related Issues

<Links to related GitHub issues>

## Changes Made

- <List of changes>

## Testing

- [ ] Code compiles
- [ ] Unit tests pass
- [ ] Integration tests pass
- [ ] Manual testing performed on <hardware/configuration>

## Checklist

- [ ] Code follows style guidelines
- [ ] Documentation updated
- [ ] All commits are signed-off
- [ ] No new warnings introduced
```

---

## Review Process

### Code Review

All non-trivial changes require at least one approval from a maintainer.

**Review Criteria:**
- Code quality and style
- Correctness and completeness
- Security implications
- Performance impact
- Documentation quality
- Test coverage

**Review Timeline:**
- Small fixes (<100 lines): 1-2 days
- Medium features (<500 lines): 3-5 days
- Large features (>500 lines): 1-2 weeks
- Breaking changes: Depends on discussion

### Addressing Feedback

When reviewers request changes:

1. **Acknowledge** each comment
2. **Discuss** if you disagree with the feedback
3. **Implement** agreed-upon changes
4. **Update** the PR description if the scope changes significantly
5. **Re-request review** after making changes

### Merging

Pull requests are merged by maintainers using:
- **Squash and merge** for simple fixes (1-2 commits)
- **Rebase and merge** for feature branches
- **Merge commit** for complex multi-commit changes

---

## Maintainer Guidelines

### Responsibilities

Maintainers are expected to:

- Review pull requests in a timely manner
- Provide constructive feedback
- Guide new contributors
- Ensure code quality and consistency
- Maintain project roadmap
- Handle releases and versioning

### Merge Requirements

Before merging a PR, ensure:

- [ ] At least one maintainer approval
- [ ] All CI checks pass
- [ ] Code follows style guidelines
- [ ] All tests pass
- [ ] Documentation is complete
- [ ] No unresolved discussions
- [ ] Commit history is clean (if not squashed)

### Security Releases

For security vulnerabilities:

1. Create a private security advisory
2. Notify all maintainers
3. Prepare a fix in a private branch
4. Coordinate with distributions
5. Release with CVE assigned

---

## Security Vulnerabilities

If you discover a security vulnerability in MVGAL, **please do not** open a public issue or pull request.

Instead, please:

1. **Email** the maintainers privately at: security@mvgal.org
2. **Wait** for a response (typically within 48 hours)
3. **Do not** disclose the vulnerability publicly until a fix is released

For security-related inquiries, see our [Security Policy](SECURITY.md).

---

## License

By contributing to MVGAL, you agree that your contributions will be licensed under the project's [MIT License](LICENSE).

---

## Resources

- [GitHub Repository](https://github.com/mvgal/mvgal)
- [Documentation](https://mvgal.org/docs)
- [Issue Tracker](https://github.com/mvgal/mvgal/issues)
- [Discord Server](https://discord.gg/...)
- [Mailing List](mailto:mvgal-dev@lists.mvgal.org)

---

*Last updated: $(date)*
