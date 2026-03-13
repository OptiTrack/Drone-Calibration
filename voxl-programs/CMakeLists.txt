cmake_minimum_required(VERSION 3.16)

project(voxl-offboard-missions C)

# ---------------------------------------------------------------------------
# Compiler settings
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable all common warnings
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
)

# ---------------------------------------------------------------------------
# VOXL SDK include / library paths
#
# These variables should be set by the VOXL cross-compilation toolchain file
# (e.g.  cmake/toolchain-qrb5165.cmake from voxl-vision-hub) or by the
# package's pkg-config entries.  Override them from the command line if
# building in a non-standard environment:
#
#   cmake .. -DVOXL_SDK_DIR=/path/to/voxl-sdk
# ---------------------------------------------------------------------------
if(NOT DEFINED VOXL_SDK_DIR)
    # Default: assume the headers are installed to the system sysroot
    set(VOXL_SDK_INCLUDE_DIRS
        /usr/include/voxl-sdk
        /usr/include/mavlink/v2.0
        /usr/include/mavlink/v2.0/common
        /data/vision_hub  # fallback for on-device builds
    )
    set(VOXL_SDK_LIBRARIES
        mavlink_io
        autopilot_monitor
        pipe
        pthread
        m
    )
else()
    set(VOXL_SDK_INCLUDE_DIRS
        ${VOXL_SDK_DIR}/include
        ${VOXL_SDK_DIR}/include/voxl-sdk
        ${VOXL_SDK_DIR}/include/mavlink/v2.0
        ${VOXL_SDK_DIR}/include/mavlink/v2.0/common
    )
    set(VOXL_SDK_LIBRARIES
        ${VOXL_SDK_DIR}/lib/libmavlink_io.so
        ${VOXL_SDK_DIR}/lib/libautopilot_monitor.so
        ${VOXL_SDK_DIR}/lib/libpipe.so
        pthread
        m
    )
endif()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
set(SQUARE_SOURCES
    src/offboard_square.c
)

set(RECORDER_SOURCES
    src/offboard_flight_recorder.c
)

# ---------------------------------------------------------------------------
# offboard_square  – static library (link into voxl-vision-hub)
# ---------------------------------------------------------------------------
add_library(offboard_square STATIC ${SQUARE_SOURCES})

target_include_directories(offboard_square
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${VOXL_SDK_INCLUDE_DIRS}
)

target_link_libraries(offboard_square
    PRIVATE ${VOXL_SDK_LIBRARIES}
)

# ---------------------------------------------------------------------------
# offboard_flight_recorder  – static library (link into voxl-vision-hub)
# ---------------------------------------------------------------------------
add_library(offboard_flight_recorder STATIC ${RECORDER_SOURCES})

target_include_directories(offboard_flight_recorder
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${VOXL_SDK_INCLUDE_DIRS}
)

target_link_libraries(offboard_flight_recorder
    PRIVATE ${VOXL_SDK_LIBRARIES}
)

# ---------------------------------------------------------------------------
# Combined convenience target
# ---------------------------------------------------------------------------
add_library(voxl_offboard_missions INTERFACE)
target_link_libraries(voxl_offboard_missions
    INTERFACE
        offboard_square
        offboard_flight_recorder
)

# ---------------------------------------------------------------------------
# Optional: standalone test harness (host build only, skipped on cross builds)
# ---------------------------------------------------------------------------
option(BUILD_TEST_STUBS "Build stub test executables (host build only)" OFF)

if(BUILD_TEST_STUBS)
    # A minimal stub that exercises the public APIs without a real drone.
    add_executable(test_square_stub tests/test_square_stub.c)
    target_include_directories(test_square_stub
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs
    )
    target_link_libraries(test_square_stub PRIVATE pthread m)

    add_executable(test_recorder_stub tests/test_recorder_stub.c)
    target_include_directories(test_recorder_stub
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs
    )
    target_link_libraries(test_recorder_stub PRIVATE pthread m)
endif()

# ---------------------------------------------------------------------------
# Install (for use in an ipkg / opkg package)
# ---------------------------------------------------------------------------
install(TARGETS offboard_square offboard_flight_recorder
    ARCHIVE DESTINATION lib
)
install(FILES
    src/offboard_square.h
    src/offboard_flight_recorder.h
    DESTINATION include/voxl-offboard-missions
)

# ---------------------------------------------------------------------------
# Print configuration summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== voxl-offboard-missions configuration ===")
message(STATUS "  Square side      : 1.8288 m  (6 ft)")
message(STATUS "  Flight altitude  : 1.5 m")
message(STATUS "  Cruise speed     : 0.8 m/s")
message(STATUS "  Recorder buffer  : ${RECORDER_MAX_MINUTES} minutes")
message(STATUS "  VOXL SDK dir     : ${VOXL_SDK_DIR}")
message(STATUS "  Build test stubs : ${BUILD_TEST_STUBS}")
message(STATUS "============================================")
message(STATUS "")
