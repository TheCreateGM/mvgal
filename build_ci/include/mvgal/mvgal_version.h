/**
 * @file mvgal_version.h
 * @brief MVGAL version information
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This header contains version information generated at build time.
 */

#ifndef MVGAL_VERSION_H
#define MVGAL_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup CoreAPI
 * @{
 */

/** Major version number */
#define MVGAL_VERSION_MAJOR 0

/** Minor version number */
#define MVGAL_VERSION_MINOR 2

/** Patch version number */
#define MVGAL_VERSION_PATCH 0

/** Version string */
#define MVGAL_VERSION_STRING "0.2.0"

/** Full version string with build info */
#define MVGAL_VERSION_FULL "0.2.0 (built /home/axogm/Documents/Driver/new/mvgal/build_ci)"

/** Git commit hash (if available) */
#ifdef MVGAL_GIT_HASH
#define MVGAL_VERSION_GIT_HASH MVGAL_GIT_HASH
#else
#define MVGAL_VERSION_GIT_HASH "unknown"
#endif

/** Git branch (if available) */
#ifdef MVGAL_GIT_BRANCH
#define MVGAL_VERSION_GIT_BRANCH MVGAL_GIT_BRANCH
#else
#define MVGAL_VERSION_GIT_BRANCH "unknown"
#endif

/**
 * @brief Get version as integer
 *
 * @return Version as integer (major * 10000 + minor * 100 + patch)
 */
static inline uint32_t mvgal_version_as_int(void) {
    return MVGAL_VERSION_MAJOR * 10000 + MVGAL_VERSION_MINOR * 100 + MVGAL_VERSION_PATCH;
}

/**
 * @brief Check if version is at least a specific version
 *
 * @param major Major version to check
 * @param minor Minor version to check
 * @param patch Patch version to check
 * @return true if version is >= the specified version
 */
static inline bool mvgal_version_check(uint32_t major, uint32_t minor, uint32_t patch) {
    uint32_t current = mvgal_version_as_int();
    uint32_t check = major * 10000 + minor * 100 + patch;
    return current >= check;
}

/** @} */ // end of CoreAPI

#ifdef __cplusplus
}
#endif

#endif // MVGAL_VERSION_H
