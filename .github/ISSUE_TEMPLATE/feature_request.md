---
name: Feature request
about: Suggest an idea for this project
title: ''
labels: 'enhancement'
assignees: ''
---

![Version](https://img.shields.io/badge/version-0.2.0-%2376B900?style=flat-square)
**MVGAL Feature Request** | **Version:** 0.2.0 "Health Monitor" | **Last Updated:** April 21, 2026

---

## 💡 Is your feature request related to a problem?

Please describe the **problem** this feature would solve.

**Example:**
> Currently, MVGAL doesn't support automatic GPU selection based on workload type. Users must manually configure which GPU to use for different applications.

---

## 🎯 Describe the solution you'd like

Please provide a **clear and concise** description of what you want to happen.

**Example:**
> Add a smart scheduling option that automatically selects GPUs based on workload characteristics:
> - Use NVIDIA GPU for CUDA workloads
> - Use AMD GPU for Vulkan gaming workloads
> - Use Intel GPU for general compute workloads
> - Balance across all GPUs for multi-threaded applications

---

## 🔄 Describe alternatives you've considered

Please describe any **alternative solutions or features** you've considered.

**Example:**
> - Manual GPU assignment via configuration files (current approach)
> - Environment variable overrides per application
> - Third-party GPU management tools

---

## 🎨 Design Proposal (Optional)

If you have a specific design in mind, describe it here:

### API Changes
```c
// New function to suggest GPU for workload
mvgal_gpu_t mvgal_suggest_gpu(mvgal_workload_type_t type, uint32_t flags);

typedef enum {
    MVGAL_WORKLOAD_GRAPHICS,
    MVGAL_WORKLOAD_COMPUTE,
    MVGAL_WORKLOAD_VIDEO,
    MVGAL_WORKLOAD_AI,
    MVGAL_WORKLOAD_GENERAL
} mvgal_workload_type_t;
```

### Configuration Changes
```ini
[smart_scheduling]
enabled = true
prefer_nvidia_for_cuda = true
prefer_amd_for_vulkan = true
balance_compute = true
```

### Environment Variables
```bash
export MVGAL_SMART_SCHEDULING=1
export MVGAL_WORKLOAD_TYPE=compute
```

---

## 📊 Priority & Complexity

**Priority:**
- [ ] 🔴 High - Critical feature blocking major use cases
- [ ] 🟡 Medium - Important enhancement
- [ ] 🟢 Low - Nice to have, cosmetic

**Complexity:**
- [ ] Easy - Simple change, < 1 hour
- [ ] Medium - Moderate change, 1-4 hours
- [ ] Hard - Complex change, 1-5 days
- [ ] Epic - Major feature, weeks of work

---

## 👥 Use Cases

Describe **who would use this feature** and **how it would benefit them**:

**Example:**
> **Users:** Gamers with mixed AMD/NVIDIA GPUs
> **Benefit:** Automatic optimal GPU selection without manual configuration
> **Impact:** Better performance, easier setup for multi-GPU gaming

---

## 📎 Additional Context

Add any other context, mockups, diagrams, or links about the feature request here:

- [Exceptional Utility Workflow Diagram](https://mermaid.live/)
- Screenshots or UI mockups
- Quotes from users requesting this feature
- Market research or competitor analysis
- Performance benchmarks (if applicable)

---

## ✅ Acceptance Criteria

Define what it means for this feature to be **complete**:

- [ ] Feature implemented in source code
- [ ] Unit tests added and passing
- [ ] Documentation updated
- [ ] Examples provided
- [ ] Backward compatible
- [ ] Performance impact documented

---

## 📚 Related Information

- [Module Documentation](https://github.com/TheCreateGM/mvgal/tree/main/docs)
- [Architecture Research](https://github.com/TheCreateGM/mvgal/blob/main/docs/ARCHITECTURE_RESEARCH.md)
- [API Reference](https://github.com/TheCreateGM/mvgal/blob/main/include/mvgal/mvgal.h)

---

**Thank you for suggesting this feature! 🙌**

The MVGAL team will review your request and provide feedback within 3-5 business days.

---

*© 2026 MVGAL Project. Version 0.2.0 "Health Monitor".*
