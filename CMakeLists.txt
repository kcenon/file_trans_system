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
option(FILE_TRANS_BUILD_INTEGRATION_TESTS "Build integration tests (requires network)" ON)
option(FILE_TRANS_BUILD_EXAMPLES "Build examples" ON)
option(FILE_TRANS_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(FILE_TRANS_ENABLE_LZ4 "Enable LZ4 compression" ON)
option(FILE_TRANS_ENABLE_ENCRYPTION "Enable encryption support (requires OpenSSL)" ON)
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
#     - network_system (TCP/TLS transport layer) - MANDATORY
#     - container_system (bounded_queue for backpressure)
#   Optional:
#     - LZ4 (compression)
#
# Note: network_system is a mandatory dependency as of v0.3.0.
# The BUILD_WITH_NETWORK_SYSTEM option has been removed.
##################################################

option(BUILD_WITH_COMMON_SYSTEM "Build with common_system integration (REQUIRED)" ON)
option(BUILD_WITH_THREAD_SYSTEM "Build with thread_system integration (REQUIRED)" ON)
option(BUILD_WITH_CONTAINER_SYSTEM "Build with container_system integration (REQUIRED)" ON)
option(BUILD_WITH_LOGGER_SYSTEM "Build with logger_system integration (structured logging)" ON)

# network_system is now mandatory - always enabled
# Use FILE_TRANS_ALLOW_MISSING_DEPS=ON in CI environments where deps are unavailable
option(FILE_TRANS_ALLOW_MISSING_DEPS "Allow missing dependencies (CI mode)" OFF)
set(BUILD_WITH_NETWORK_SYSTEM ON CACHE INTERNAL "network_system is mandatory" FORCE)

##################################################
# Dependency Validation
##################################################

if(NOT BUILD_WITH_COMMON_SYSTEM)
    message(WARNING "common_system is disabled - some features may be unavailable")
endif()

if(NOT BUILD_WITH_THREAD_SYSTEM)
    message(WARNING "thread_system is disabled - some features may be unavailable")
endif()

# network_system is mandatory - validation happens in FileTransDependencies.cmake

if(NOT BUILD_WITH_CONTAINER_SYSTEM)
    message(WARNING "container_system is disabled - some features may be unavailable")
endif()

##################################################
# Global Configuration
##################################################

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

##################################################
# C++20 Feature Detection
# Detect std::jthread to match thread_system ABI
# This MUST use identical detection logic as thread_system's
# ThreadSystemFeatures.cmake to ensure ABI compatibility.
##################################################

include(CheckCXXSourceCompiles)

# Check for std::jthread support (must match thread_system detection exactly)
function(check_std_jthread_support)
    # Must match thread_system's check_cxx20_feature() exactly
    set(CMAKE_REQUIRED_FLAGS "")
    set(CMAKE_REQUIRED_LIBRARIES "${CMAKE_EXE_LINKER_FLAGS}")

    if(MSVC)
        set(CMAKE_REQUIRED_FLAGS "/std:c++20 /EHsc /Zc:__cplusplus /permissive-")
    else()
        set(CMAKE_REQUIRED_FLAGS "-std=c++20 ${CMAKE_CXX_FLAGS}")
    endif()

    # Test code must match thread_system exactly
    check_cxx_source_compiles("
        #include <cstddef>
        #include <cstdint>
        #include <utility>
        #include <thread>
        #include <stop_token>
        int main() {
            std::jthread t([](std::stop_token st) {});
            return 0;
        }
    " HAS_STD_JTHREAD)

    if(HAS_STD_JTHREAD)
        add_definitions(-DUSE_STD_JTHREAD)
        message(STATUS "✅ Using std::jthread (matches thread_system ABI)")
    else()
        message(STATUS "Using std::thread fallback (matches thread_system ABI)")
    endif()
endfunction()

check_std_jthread_support()

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
# OpenSSL Encryption Support
##################################################

if(FILE_TRANS_ENABLE_ENCRYPTION)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        message(STATUS "OpenSSL encryption enabled")
        message(STATUS "  OpenSSL version: ${OPENSSL_VERSION}")
        message(STATUS "  OpenSSL include: ${OPENSSL_INCLUDE_DIR}")
        message(STATUS "  OpenSSL libraries: ${OPENSSL_LIBRARIES}")
    else()
        message(STATUS "OpenSSL not found - encryption will be disabled")
        set(FILE_TRANS_ENABLE_ENCRYPTION OFF)
    endif()
endif()

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

set(FILE_TRANS_SOURCES
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
    src/server/pipeline_jobs.cpp
    src/server/quota_manager.cpp
    src/server/storage_manager.cpp
    src/server/storage_policy.cpp
    src/client/file_transfer_client.cpp
    src/transport/tcp_transport.cpp
    src/transport/quic_transport.cpp
    src/transport/session_resumption.cpp
    src/transport/connection_migration.cpp
)

# Add encryption sources if enabled
if(FILE_TRANS_ENABLE_ENCRYPTION AND OpenSSL_FOUND)
    list(APPEND FILE_TRANS_SOURCES
        src/encryption/aes_gcm_engine.cpp
        src/encryption/key_manager.cpp
    )
endif()

# Add cloud storage sources (requires OpenSSL for signing)
if(FILE_TRANS_ENABLE_ENCRYPTION AND OpenSSL_FOUND)
    list(APPEND FILE_TRANS_SOURCES
        src/cloud/s3_storage.cpp
        src/cloud/azure_blob_storage.cpp
        src/cloud/gcs_storage.cpp
    )
endif()

add_library(FileTransSystem ${FILE_TRANS_SOURCES})

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

# OpenSSL encryption support (compile definitions and includes only)
# Note: Actual library linking is done after network_system to ensure
# proper symbol resolution order on Unix linkers (GCC's ld)
if(FILE_TRANS_ENABLE_ENCRYPTION AND OpenSSL_FOUND)
    target_compile_definitions(FileTransSystem PUBLIC FILE_TRANS_ENABLE_ENCRYPTION)
    target_include_directories(FileTransSystem PRIVATE ${OPENSSL_INCLUDE_DIR})
endif()

# Ecosystem dependencies
if(BUILD_WITH_COMMON_SYSTEM AND COMMON_SYSTEM_INCLUDE_DIR)
    target_include_directories(FileTransSystem PUBLIC ${COMMON_SYSTEM_INCLUDE_DIR})
    # Define legacy macro for compatibility
    # Note: KCENON_WITH_COMMON_SYSTEM is intentionally NOT defined to maintain
    # ABI compatibility with network_system which uses its local Result type
    target_compile_definitions(FileTransSystem PUBLIC
        BUILD_WITH_COMMON_SYSTEM
    )
endif()

if(BUILD_WITH_THREAD_SYSTEM AND THREAD_SYSTEM_INCLUDE_DIR)
    target_include_directories(FileTransSystem PUBLIC ${THREAD_SYSTEM_INCLUDE_DIR})
    # Define both legacy and new unified macros for compatibility
    target_compile_definitions(FileTransSystem PUBLIC
        BUILD_WITH_THREAD_SYSTEM
        KCENON_WITH_THREAD_SYSTEM=1
    )

    # Link thread_system library
    if(THREAD_SYSTEM_LIBRARY)
        target_link_libraries(FileTransSystem PUBLIC ${THREAD_SYSTEM_LIBRARY})
    endif()
endif()

if(BUILD_WITH_NETWORK_SYSTEM AND NETWORK_SYSTEM_INCLUDE_DIR)
    target_include_directories(FileTransSystem PUBLIC ${NETWORK_SYSTEM_INCLUDE_DIR})
    # Define both legacy and new unified macros for compatibility
    # KCENON_WITH_COMMON_SYSTEM=1 is required for ABI compatibility with network_system
    # when linking against the library file (not CMake target). network_system's
    # Result<T> type changes based on this flag, so both must use the same setting.
    target_compile_definitions(FileTransSystem PUBLIC
        BUILD_WITH_NETWORK_SYSTEM
        KCENON_WITH_NETWORK_SYSTEM=1
        KCENON_WITH_COMMON_SYSTEM=1
    )

    # Link network_system library
    if(NETWORK_SYSTEM_LIBRARY)
        target_link_libraries(FileTransSystem PUBLIC ${NETWORK_SYSTEM_LIBRARY})
        # NetworkSystem may depend on ThreadSystem internally, so link ThreadSystem
        # again to ensure proper symbol resolution on Unix linkers
        if(THREAD_SYSTEM_LIBRARY)
            target_link_libraries(FileTransSystem PUBLIC ${THREAD_SYSTEM_LIBRARY})
        endif()
    endif()

    # ASIO is required for network_system headers (they include <asio.hpp>)
    if(ASIO_TARGET AND TARGET ${ASIO_TARGET})
        target_link_libraries(FileTransSystem PUBLIC ${ASIO_TARGET})
        target_compile_definitions(FileTransSystem PUBLIC ASIO_STANDALONE ASIO_NO_DEPRECATED)
    elseif(ASIO_INCLUDE_DIR)
        # Use BUILD_INTERFACE to avoid exporting build-tree paths during installation
        target_include_directories(FileTransSystem PUBLIC
            $<BUILD_INTERFACE:${ASIO_INCLUDE_DIR}>
        )
        target_compile_definitions(FileTransSystem PUBLIC ASIO_STANDALONE ASIO_NO_DEPRECATED)
    endif()

    # OpenSSL TLS/Crypto libraries must be linked AFTER network_system
    # network_system's QUIC transport uses OpenSSL for TLS 1.3
    # Unix linkers (GCC's ld) require libraries to be specified after
    # the objects that reference them for proper symbol resolution
    if(FILE_TRANS_ENABLE_ENCRYPTION AND OpenSSL_FOUND)
        target_link_libraries(FileTransSystem PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    endif()
endif()

if(BUILD_WITH_CONTAINER_SYSTEM AND CONTAINER_SYSTEM_INCLUDE_DIR)
    target_include_directories(FileTransSystem PUBLIC ${CONTAINER_SYSTEM_INCLUDE_DIR})
    # Define both legacy and new unified macros for compatibility
    target_compile_definitions(FileTransSystem PUBLIC
        BUILD_WITH_CONTAINER_SYSTEM
        KCENON_WITH_CONTAINER_SYSTEM=1
    )
endif()

if(BUILD_WITH_LOGGER_SYSTEM AND LOGGER_SYSTEM_INCLUDE_DIR)
    target_include_directories(FileTransSystem PUBLIC ${LOGGER_SYSTEM_INCLUDE_DIR})
    # Define both legacy and new unified macros for compatibility
    target_compile_definitions(FileTransSystem PUBLIC
        BUILD_WITH_LOGGER_SYSTEM
        KCENON_WITH_LOGGER_SYSTEM=1
    )

    # Link logger_system library
    if(LOGGER_SYSTEM_LIBRARY)
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
        # Disable /WX for now due to warnings in external dependency headers (logger_system)
        # TODO: Re-enable once upstream dependencies are fixed
        # /WX
    )
endif()

##################################################
# Tests
##################################################

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
message(STATUS "  ENABLE_ENCRYPTION: ${FILE_TRANS_ENABLE_ENCRYPTION}")
message(STATUS "  ENABLE_COVERAGE: ${FILE_TRANS_ENABLE_COVERAGE}")
message(STATUS "  ENABLE_ASAN: ${FILE_TRANS_ENABLE_ASAN}")
message(STATUS "  ENABLE_TSAN: ${FILE_TRANS_ENABLE_TSAN}")
message(STATUS "  ENABLE_UBSAN: ${FILE_TRANS_ENABLE_UBSAN}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  common_system: ${BUILD_WITH_COMMON_SYSTEM}")
message(STATUS "  thread_system: ${BUILD_WITH_THREAD_SYSTEM}")
message(STATUS "  network_system: ${BUILD_WITH_NETWORK_SYSTEM}")
message(STATUS "  container_system: ${BUILD_WITH_CONTAINER_SYSTEM}")
message(STATUS "  logger_system: ${BUILD_WITH_LOGGER_SYSTEM}")
message(STATUS "========================================")
message(STATUS "")
