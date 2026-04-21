# MVGAL Pull Request

**Version:** 0.2.0 "Health Monitor" | **Last Updated:** April 21, 2026

---

## 📝 Summary

 Please provide a **clear and concise** description of what this PR changes or fixes.

**Example:**
> - Fixes crash in GPU detection when NVIDIA GPUs are present
> - Adds support for Intel Xe GPUs in health monitoring
> - Updates Vulkan layer to properly handle vkCreateInstance

---

## 🏷️ Type of Change

Please select the type(s) that apply:

- [ ] ✅ **Bug fix** - Non-breaking change which fixes an issue
- [ ] 🚀 **New feature** - Non-breaking change which adds functionality
- [ ] ⚠️ **Breaking change** - Fix or feature that would cause existing functionality to not work as expected
- [ ] 📚 **Documentation update** - Improvements to documentation
- [ ] 🔄 **Refactoring** - No functional changes, code cleanup
- [ ] 🧪 **Tests** - Adding or improving tests
- [ ] 📦 **Dependencies** - Updating or adding dependencies
- [ ] ⚡ **Performance** - Performance optimizations
- [ ] 🔒 **Security** - Security-related changes

---

## ✅ Testing

Please confirm the following:

- [ ] ✅ **Code compiles** with `-Wall -Wextra -Werror`
- [ ] 🧪 **All tests pass** locally (`ctest -V`)
- [ ] 🎯 **New tests added** (if applicable)
- [ ] 🏃 **Manual testing** completed
- [ ] ⚠️ **No new warnings** introduced

**Test results:**
```
Paste test output here if applicable:
ctest -V
```

---

## 🔗 Related Issues

 Link to any related issues, discussions, or feature requests.

Use the `Fixes #`, `Closes #`, or `Resolves #` syntax to **automatically close** issues when this PR is merged.

**Examples:**
- Fixes #123
- Closes #456
- Resolves TheCreateGM/mvgal#789

If this PR doesn't fully resolve an issue, just mention it without the special syntax.

---

## 📊 Impact Assessment

### Affected Components

- [ ] Core API (`src/userspace/api/`)
- [ ] GPU Management (`src/userspace/daemon/gpu_manager.c`)
- [ ] Memory Layer (`src/userspace/memory/`)
- [ ] Scheduler (`src/userspace/scheduler/`)
- [ ] Vulkan Layer (`src/userspace/intercept/vulkan/`)
- [ ] OpenCL Intercept (`src/userspace/intercept/opencl/`)
- [ ] CUDA Wrapper (`src/userspace/intercept/cuda/`)
- [ ] Execution Module (`src/userspace/execution/`)
- [ ] Kernel Module (`src/kernel/`)
- [ ] Tests (`test/`)
- [ ] Documentation (`docs/`)
- [ ] Build System (`CMakeLists.txt`)
- [ ] Other (specify): _______________

### Breaking Changes

If this PR introduces breaking changes, please document them:

**API Changes:**
- Function signature changes: _______________
- Deprecated functions: _______________
- Removed functions: _______________

**Behavior Changes:**
- Changed default behavior: _______________
- Configuration file changes: _______________
- Environment variable changes: _______________

---

## 📝 Additional Context

Please add any other context about the changes here:

- Motivation behind the change
- Design decisions made
- Trade-offs considered
- Alternative approaches rejected
- Screenshots (for UI changes)
- Performance impact
- Related discussions or RFCs

---

## 🔍 Review Checklist

For maintainers to use during review:

- [ ] PR title is clear and descriptive
- [ ] Commit messages follow conventions
- [ ] Code follows project coding standards
- [ ] Tests are comprehensive
- [ ] Documentation is updated (if applicable)
- [ ] Version numbers are updated (if applicable)
- [ ] No sensitive information exposed
- [ ] Changes are backward compatible (or breaking changes documented)

---

**Thank you for your contribution! 🎉**

By submitting this pull request, you agree to license your contributions under the [GPLv3 License](LICENSE).

---

*© 2026 MVGAL Project. Version 0.2.0 "Health Monitor".*