cmake_minimum_required(VERSION 3.20)

##################################################
# File Transfer System CMakeLists.txt
#
# High-performance file transfer library
# Built on kcenon ecosystem
##################################################

project(FileTransSystem
    VERSION 0.2.0.0
    DESCRIPTION "High-Performance File Transfer System"
    LANGUAGES CXX
)

##################################################
# C++ Standard
##################################################

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

##################################################
# Options
##################################################

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
option(FILE_TRANS_BUILD_TESTS "Build tests" ON)
option(FILE_TRANS_BUILD_EXAMPLES "Build examples" ON)
option(FILE_TRANS_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(FILE_TRANS_ENABLE_LZ4 "Enable LZ4 compression" ON)
option(FILE_TRANS_ENABLE_COVERAGE "Enable code coverage" OFF)

# Sanitizer options (mutually exclusive: ASAN and TSAN cannot be used together)
option(FILE_TRANS_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(FILE_TRANS_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(FILE_TRANS_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

##################################################
# Dependency Configuration
#
# File Transfer System Dependencies:
#   Required:
#     - common_system (Result<T>, error handling)
#     - thread_system (typed_thread_pool for pipeline)
#     - network_system (TCP/TLS transport layer)
#     - container_system (bounded_queue for backpressure)
#   Optional:
#     - LZ4 (compression)
##################################################

# Required dependencies (always enabled)
set(BUILD_WITH_COMMON_SYSTEM ON CACHE BOOL "Build with common_system integration (REQUIRED)" FORCE)
set(BUILD_WITH_THREAD_SYSTEM ON CACHE BOOL "Build with thread_system integration (REQUIRED)" FORCE)
set(BUILD_WITH_NETWORK_SYSTEM ON CACHE BOOL "Build with network_system integration (REQUIRED)" FORCE)
set(BUILD_WITH_CONTAINER_SYSTEM ON CACHE BOOL "Build with container_system integration (REQUIRED)" FORCE)

# Optional dependencies
option(BUILD_WITH_LOGGER_SYSTEM "Build with logger_system integration (structured logging)" ON)

##################################################
# Dependency Validation
##################################################

# Note: common_system, thread_system, network_system, and container_system
# are required dependencies. The BUILD_WITH_* variables are kept for
# backwards compatibility but are always enabled.

##################################################
# Global Configuration
##################################################

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Coverage settings
if(FILE_TRANS_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage -fprofile-update=atomic")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage -fprofile-update=atomic")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")

        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -latomic")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -latomic")
        endif()
    endif()
endif()

# Sanitizer settings
# Note: ASAN and TSAN cannot be used together
if(FILE_TRANS_ENABLE_ASAN AND FILE_TRANS_ENABLE_TSAN)
    message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be enabled simultaneously")
endif()

if(FILE_TRANS_ENABLE_ASAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
        message(STATUS "AddressSanitizer enabled")
    else()
        message(WARNING "AddressSanitizer is only supported with GCC or Clang")
    endif()
endif()

if(FILE_TRANS_ENABLE_TSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -fno-omit-frame-pointer -g")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread -fno-omit-frame-pointer -g")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=thread")
        message(STATUS "ThreadSanitizer enabled")
    else()
        message(WARNING "ThreadSanitizer is only supported with GCC or Clang")
    endif()
endif()

if(FILE_TRANS_ENABLE_UBSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined -fno-omit-frame-pointer -g")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined -fno-omit-frame-pointer -g")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=undefined")
        message(STATUS "UndefinedBehaviorSanitizer enabled")
    else()
        message(WARNING "UndefinedBehaviorSanitizer is only supported with GCC or Clang")
    endif()
endif()

##################################################
# Include CMake Modules
##################################################

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(FileTransDependencies)
include(FileTransInstall)

##################################################
# Dependency Detection
##################################################

find_package(PkgConfig QUIET)
find_package(Threads REQUIRED)

find_file_trans_dependencies()

##################################################
# LZ4 Compression Support
##################################################

if(FILE_TRANS_ENABLE_LZ4)
    find_library(LZ4_LIBRARY NAMES lz4)
    find_path(LZ4_INCLUDE_DIR NAMES lz4.h)

    if(LZ4_LIBRARY AND LZ4_INCLUDE_DIR)
        set(LZ4_FOUND TRUE)
        message(STATUS "LZ4 compression enabled")
        message(STATUS "  LZ4 library: ${LZ4_LIBRARY}")
        message(STATUS "  LZ4 include: ${LZ4_INCLUDE_DIR}")
    else()
        # Fallback to pkg-config
        if(PkgConfig_FOUND)
            pkg_check_modules(LZ4 QUIET liblz4)
        endif()

        if(NOT LZ4_FOUND)
            message(WARNING "LZ4 not found - compression will be disabled")
            set(FILE_TRANS_ENABLE_LZ4 OFF)
        endif()
    endif()
endif()

##################################################
# Create Main Library
##################################################

add_library(FileTransSystem
    src/file_transfer.cpp
    src/core/checksum.cpp
    src/core/chunk_splitter.cpp
    src/core/chunk_assembler.cpp
    src/core/compression_engine.cpp
    src/core/transfer_id.cpp
    src/core/resume_handler.cpp
    src/core/bandwidth_limiter.cpp
    src/core/statistics_collector.cpp
    src/server/file_transfer_server.cpp
    src/server/server_pipeline.cpp
    src/server/quota_manager.cpp
    src/client/file_transfer_client.cpp
    src/transport/tcp_transport.cpp
)

add_library(kcenon::file_transfer ALIAS FileTransSystem)

# Set target properties
set_target_properties(FileTransSystem PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME file_transfer
)

# Include directories
target_include_directories(FileTransSystem
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link threading library
target_link_libraries(FileTransSystem PUBLIC Threads::Threads)

# LZ4 compression
if(FILE_TRANS_ENABLE_LZ4 AND LZ4_FOUND)
    target_compile_definitions(FileTransSystem PUBLIC FILE_TRANS_ENABLE_LZ4)
    if(LZ4_INCLUDE_DIR)
        target_include_directories(FileTransSystem PRIVATE ${LZ4_INCLUDE_DIR})
    endif()
    if(LZ4_LIBRARY)
        target_link_libraries(FileTransSystem PRIVATE ${LZ4_LIBRARY})
    elseif(LZ4_LIBRARIES)
        target_link_libraries(FileTransSystem PRIVATE ${LZ4_LIBRARIES})
    endif()
endif()

# Ecosystem dependencies
# Use BUILD_INTERFACE to avoid exporting build-tree paths during installation
# This is especially important when dependencies are fetched via FetchContent
# When dependencies are fetched, link to CMake targets; otherwise, use library files

if(BUILD_WITH_COMMON_SYSTEM)
    if(COMMON_SYSTEM_INCLUDE_DIR)
        target_include_directories(FileTransSystem PUBLIC
            $<BUILD_INTERFACE:${COMMON_SYSTEM_INCLUDE_DIR}>
        )
    endif()
    target_compile_definitions(FileTransSystem PUBLIC BUILD_WITH_COMMON_SYSTEM)
endif()

# IMPORTANT: Link order matters for static libraries on Linux/Windows.
# NetworkSystem depends on ThreadSystem, so NetworkSystem must be linked first.
# This ensures the linker processes dependencies in the correct order.

if(BUILD_WITH_NETWORK_SYSTEM)
    if(NETWORK_SYSTEM_INCLUDE_DIR)
        target_include_directories(FileTransSystem PUBLIC
            $<BUILD_INTERFACE:${NETWORK_SYSTEM_INCLUDE_DIR}>
        )
    endif()
    target_compile_definitions(FileTransSystem PUBLIC BUILD_WITH_NETWORK_SYSTEM)

    # Link network_system library (prefer CMake target if available)
    if(TARGET NetworkSystem)
        target_link_libraries(FileTransSystem PUBLIC NetworkSystem)
    elseif(NETWORK_SYSTEM_LIBRARY)
        target_link_libraries(FileTransSystem PUBLIC ${NETWORK_SYSTEM_LIBRARY})
    endif()

    # ASIO is required for network_system headers (they include <asio.hpp>)
    if(ASIO_TARGET AND TARGET ${ASIO_TARGET})
        target_link_libraries(FileTransSystem PUBLIC ${ASIO_TARGET})
        target_compile_definitions(FileTransSystem PUBLIC ASIO_STANDALONE ASIO_NO_DEPRECATED)
    elseif(ASIO_INCLUDE_DIR)
        target_include_directories(FileTransSystem PUBLIC
            $<BUILD_INTERFACE:${ASIO_INCLUDE_DIR}>
        )
        target_compile_definitions(FileTransSystem PUBLIC ASIO_STANDALONE ASIO_NO_DEPRECATED)
    endif()
endif()

if(BUILD_WITH_THREAD_SYSTEM)
    if(THREAD_SYSTEM_INCLUDE_DIR)
        target_include_directories(FileTransSystem PUBLIC
            $<BUILD_INTERFACE:${THREAD_SYSTEM_INCLUDE_DIR}>
        )
    endif()
    target_compile_definitions(FileTransSystem PUBLIC BUILD_WITH_THREAD_SYSTEM)

    # Link thread_system library (prefer CMake target if available)
    if(TARGET ThreadSystem)
        target_link_libraries(FileTransSystem PUBLIC ThreadSystem)
    elseif(THREAD_SYSTEM_LIBRARY)
        target_link_libraries(FileTransSystem PUBLIC ${THREAD_SYSTEM_LIBRARY})
    endif()
endif()

if(BUILD_WITH_CONTAINER_SYSTEM)
    if(CONTAINER_SYSTEM_INCLUDE_DIR)
        target_include_directories(FileTransSystem PUBLIC
            $<BUILD_INTERFACE:${CONTAINER_SYSTEM_INCLUDE_DIR}>
        )
    endif()
    target_compile_definitions(FileTransSystem PUBLIC BUILD_WITH_CONTAINER_SYSTEM)
endif()

if(BUILD_WITH_LOGGER_SYSTEM)
    if(LOGGER_SYSTEM_INCLUDE_DIR)
        target_include_directories(FileTransSystem PUBLIC
            $<BUILD_INTERFACE:${LOGGER_SYSTEM_INCLUDE_DIR}>
        )
    endif()
    target_compile_definitions(FileTransSystem PUBLIC BUILD_WITH_LOGGER_SYSTEM)

    # Link logger_system library (prefer CMake target if available)
    if(TARGET LoggerSystem)
        target_link_libraries(FileTransSystem PUBLIC LoggerSystem)
    elseif(LOGGER_SYSTEM_LIBRARY)
        target_link_libraries(FileTransSystem PUBLIC ${LOGGER_SYSTEM_LIBRARY})
    endif()
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(FileTransSystem PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror=return-type
    )
elseif(MSVC)
    target_compile_options(FileTransSystem PRIVATE
        /W4
        /WX
    )
endif()

##################################################
# Tests
##################################################

# Disable tests when dependencies are fetched via FetchContent
# This avoids static destruction order issues with singleton patterns
# in network_system (io_context_thread_manager) that cause heap corruption
# during test teardown. See PR #103 for details.
if(NETWORK_SYSTEM_FETCHED OR THREAD_SYSTEM_FETCHED OR COMMON_SYSTEM_FETCHED)
    if(FILE_TRANS_BUILD_TESTS)
        message(WARNING "Tests disabled: Dependencies were fetched via FetchContent. "
                        "Static destruction order issues may cause heap corruption. "
                        "Build with local dependencies to run tests.")
        set(FILE_TRANS_BUILD_TESTS OFF)
    endif()
endif()

if(FILE_TRANS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

##################################################
# Examples
##################################################

if(FILE_TRANS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

##################################################
# Benchmarks
##################################################

if(FILE_TRANS_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

##################################################
# Installation
##################################################

setup_file_trans_installation()

##################################################
# Summary
##################################################

message(STATUS "")
message(STATUS "========================================")
message(STATUS "File Transfer System Configuration")
message(STATUS "========================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")
message(STATUS "Options:")
message(STATUS "  BUILD_TESTS: ${FILE_TRANS_BUILD_TESTS}")
message(STATUS "  BUILD_EXAMPLES: ${FILE_TRANS_BUILD_EXAMPLES}")
message(STATUS "  BUILD_BENCHMARKS: ${FILE_TRANS_BUILD_BENCHMARKS}")
message(STATUS "  ENABLE_LZ4: ${FILE_TRANS_ENABLE_LZ4}")
message(STATUS "  ENABLE_COVERAGE: ${FILE_TRANS_ENABLE_COVERAGE}")
message(STATUS "  ENABLE_ASAN: ${FILE_TRANS_ENABLE_ASAN}")
message(STATUS "  ENABLE_TSAN: ${FILE_TRANS_ENABLE_TSAN}")
message(STATUS "  ENABLE_UBSAN: ${FILE_TRANS_ENABLE_UBSAN}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  common_system: ${BUILD_WITH_COMMON_SYSTEM} (fetched: ${COMMON_SYSTEM_FETCHED})")
message(STATUS "  thread_system: ${BUILD_WITH_THREAD_SYSTEM} (fetched: ${THREAD_SYSTEM_FETCHED})")
message(STATUS "  network_system: ${BUILD_WITH_NETWORK_SYSTEM} (fetched: ${NETWORK_SYSTEM_FETCHED})")
message(STATUS "  container_system: ${BUILD_WITH_CONTAINER_SYSTEM} (fetched: ${CONTAINER_SYSTEM_FETCHED})")
message(STATUS "  logger_system: ${BUILD_WITH_LOGGER_SYSTEM} (fetched: ${LOGGER_SYSTEM_FETCHED})")
if(NETWORK_SYSTEM_FETCHED OR THREAD_SYSTEM_FETCHED OR COMMON_SYSTEM_FETCHED)
    message(STATUS "")
    message(STATUS "NOTE: Tests disabled due to FetchContent build.")
    message(STATUS "      Use unified_system structure for full test support.")
endif()
message(STATUS "========================================")
message(STATUS "")
