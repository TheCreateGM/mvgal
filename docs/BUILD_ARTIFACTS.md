# Build Artifacts Archive

This document lists automated build outputs, Copr build results and other build artifacts that were moved into `builds_archive/` to keep the source tree clean and make reviewing the repository easier.

All moved items live under the repository path:

`mvgal/builds_archive/`

## What was moved

The following top-level items were moved into `builds_archive/` (source -> archive path):

- `build` -> `builds_archive/build`
- `build_ci` -> `builds_archive/build_ci`
- `build_vulkan` -> `builds_archive/build_vulkan`
- `target` -> `builds_archive/target`
- `mvgal-0.2.0` -> `builds_archive/mvgal-0.2.0` (release snapshot)
- `mvgal-0.2.0.tar.gz` -> `builds_archive/mvgal-0.2.0.tar.gz`
- `mvgal-0.2.0-1.fc44.src.rpm` -> `builds_archive/mvgal-0.2.0-1.fc44.src.rpm`
- `rpmbuild/BUILD` -> `builds_archive/rpmbuild-BUILD`
- `rpmbuild/RPMS` -> `builds_archive/rpmbuild-RPMS`

Additionally, Copr build result directories and logs were moved into the archive. Examples include (not exhaustive):

- `fedora-42-x86_64` -> `builds_archive/fedora-42-x86_64`
- `fedora-43-x86_64` -> `builds_archive/fedora-43-x86_64`
- `fedora-44-x86_64` -> `builds_archive/fedora-44-x86_64`
- `fedora-eln-x86_64` -> `builds_archive/fedora-eln-x86_64`
- `fedora-rawhide-x86_64` -> `builds_archive/fedora-rawhide-x86_64`
- `almalinux-*`, `centos-*`, `amazonlinux-*`, `mageia-*`, `openeuler-*`, `opensuse-*`, `rhel-*` -> `builds_archive/<same-name>`

(If you need a complete inventory, list the contents of `builds_archive/` after cloning or pull — it contains all the moved build directories and archived SRPM/RPM files.)

## Why this was done

- Build outputs are large and can clutter the repository view.
- Build artifacts are generated and can be reproduced; keeping them in the main tree makes it harder to review source changes.
- Archiving keeps a historical snapshot available in the repo while improving the discoverability of actual source files.

## How to reproduce builds locally

You should create a local build directory (outside the archived area) and build from a clean source tree. Example:

```bash
# Clone repository (or use an existing checkout)
git clone https://github.com/TheCreateGM/mvgal.git
cd mvgal

# Make a fresh build directory
mkdir -p build && cd build
cmake .. -DWITH_VULKAN=ON -DWITH_TESTS=ON
make -j$(nproc)

# Run tests
ctest --test-dir . --output-on-failure
```

Notes:
- `target/` (Rust/Cargo build outputs) and `build/` (CMake build outputs) are intentionally archived in the repository. Do not edit the archived copies — create new local build directories.
- Packaging-related directories such as `rpmbuild/SPECS` and `rpmbuild/SOURCES` remain in place for producing SRPMs; only `rpmbuild/BUILD` and `rpmbuild/RPMS` (build outputs) were archived.

## If you need files from the archive

- You can inspect or extract files from `builds_archive/` in this repository.
- If you accidentally deleted or need fresh artifacts, reproduce them with a local build as shown above.

---

If you want me to produce a complete inventory file listing every entry moved, I can generate `builds_archive/INVENTORY.txt` and commit it to the repo.