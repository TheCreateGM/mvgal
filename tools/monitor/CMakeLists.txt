cmake_minimum_required(VERSION 3.16)
project(mvgal_monitor_tools NONE)

# Simple development helper for running the MVGAL daemon from the build tree.
# This CMakeLists creates:
#  - a small wrapper script (run-mvgald.sh) in the build directory that sets
#    the recommended development environment variables and execs the daemon.
#  - a custom target `run-mvgald` that builds the `mvgal-daemon` target and
#    runs it in the foreground with MVGAL_NO_DAEMON=1 (development mode).
#  - a custom target `run-mvgald-script` that runs the generated wrapper script.
#
# Usage:
#   cmake --build <builddir> --target run-mvgald
#   OR
#   cmake --build <builddir> --target run-mvgald-script
#
# Notes:
#  - This expects the top-level project to create a target named `mvgal-daemon`.
#  - The script and run targets are intended for local development only.
#  - The script will place the runtime socket in ${CMAKE_BINARY_DIR}/mvgal.sock
#    by default; you can override by setting MVGAL_SOCKET_PATH in the environment.

# Path to the generated run script in the build tree
set(MVGAL_RUN_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/run-mvgald.sh")

# Generate a small helper shell script that can be used independently.
# The script expects the path to the built daemon binary as its first argument.
file(WRITE "${MVGAL_RUN_SCRIPT}" "#!/usr/bin/env bash\n\
set -eu\n\
# Simple MVGAL development runner script\n\
# Usage: run-mvgald.sh /path/to/mvgal-daemon [args...]\n\
DAEMON_BIN=\"$1\"\n\
shift || true\n\
if [ -z \"${DAEMON_BIN}\" ]; then\n  echo \"Usage: $0 /path/to/mvgal-daemon [args...]\" >&2\n  exit 2\nfi\n\n# Development environment defaults\n: \"${MVGAL_NO_DAEMON:=1}\"\n: \"${MVGAL_SOCKET_PATH:=${CMAKE_BINARY_DIR}/mvgal.sock}\"\nexport MVGAL_NO_DAEMON\nexport MVGAL_SOCKET_PATH\n\nexec \"${DAEMON_BIN}\" \"$@\"\n")

# Make the script executable for convenience
# Use portable CMake file(CHMOD) PERMISSIONS form
file(CHMOD "${MVGAL_RUN_SCRIPT}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Provide a custom target to build the daemon and run it in the foreground.
# This uses generator expression $<TARGET_FILE:mvgal-daemon> to reference the built binary.
add_custom_target(run-mvgald
    DEPENDS mvgal-daemon "${MVGAL_RUN_SCRIPT}"
    # Run in development mode: foreground, non-daemonized
    COMMAND ${CMAKE_COMMAND} -E env MVGAL_NO_DAEMON=1 MVGAL_SOCKET_PATH=${CMAKE_BINARY_DIR}/mvgal.sock $<TARGET_FILE:mvgal-daemon>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Build and run mvgald in foreground (development mode)."
)

# Alternative target that invokes the generated wrapper script.
# This can be useful if you want the wrapper file to be present and reusable.
add_custom_target(run-mvgald-script
    DEPENDS mvgal-daemon "${MVGAL_RUN_SCRIPT}"
    COMMAND "${MVGAL_RUN_SCRIPT}" $<TARGET_FILE:mvgal-daemon>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Run the MVGAL development wrapper script which sets env and execs the daemon."
)

# Helpful status messages at configure time
message(STATUS "MVGAL development run script: ${MVGAL_RUN_SCRIPT}")
message(STATUS "Custom targets available: run-mvgald, run-mvgald-script")
