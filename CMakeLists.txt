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

option(BUILD_WITH_COMMON_SYSTEM "Build with common_system integration (REQUIRED)" ON)
option(BUILD_WITH_THREAD_SYSTEM "Build with thread_system integration (REQUIRED)" ON)
option(BUILD_WITH_NETWORK_SYSTEM "Build with network_system integration (REQUIRED)" ON)
option(BUILD_WITH_CONTAINER_SYSTEM "Build with container_system integration (REQUIRED)" ON)

##################################################
# Dependency Validation
##################################################

if(NOT BUILD_WITH_COMMON_SYSTEM)
    message(WARNING "common_system is disabled - some features may be unavailable")
endif()

if(NOT BUILD_WITH_THREAD_SYSTEM)
    message(WARNING "thread_system is disabled - some features may be unavailable")
endif()

if(NOT BUILD_WITH_NETWORK_SYSTEM)
    message(WARNING "network_system is disabled - some features may be unavailable")
endif()

if(NOT BUILD_WITH_CONTAINER_SYSTEM)
    message(WARNING "container_system is disabled - some features may be unavailable")
endif()

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
    src/server/file_transfer_server.cpp
    src/client/file_transfer_client.cpp
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
if(BUILD_WITH_COMMON_SYSTEM AND COMMON_SYSTEM_INCLUDE_DIR)
    target_include_directories(FileTransSystem PUBLIC ${COMMON_SYSTEM_INCLUDE_DIR})
    target_compile_definitions(FileTransSystem PUBLIC BUILD_WITH_COMMON_SYSTEM)
endif()

if(BUILD_WITH_THREAD_SYSTEM AND THREAD_SYSTEM_INCLUDE_DIR)
    target_include_directories(FileTransSystem PUBLIC ${THREAD_SYSTEM_INCLUDE_DIR})
    target_compile_definitions(FileTransSystem PUBLIC BUILD_WITH_THREAD_SYSTEM)
endif()

if(BUILD_WITH_NETWORK_SYSTEM AND NETWORK_SYSTEM_INCLUDE_DIR)
    target_include_directories(FileTransSystem PUBLIC ${NETWORK_SYSTEM_INCLUDE_DIR})
    target_compile_definitions(FileTransSystem PUBLIC BUILD_WITH_NETWORK_SYSTEM)
endif()

if(BUILD_WITH_CONTAINER_SYSTEM AND CONTAINER_SYSTEM_INCLUDE_DIR)
    target_include_directories(FileTransSystem PUBLIC ${CONTAINER_SYSTEM_INCLUDE_DIR})
    target_compile_definitions(FileTransSystem PUBLIC BUILD_WITH_CONTAINER_SYSTEM)
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
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  common_system: ${BUILD_WITH_COMMON_SYSTEM}")
message(STATUS "  thread_system: ${BUILD_WITH_THREAD_SYSTEM}")
message(STATUS "  network_system: ${BUILD_WITH_NETWORK_SYSTEM}")
message(STATUS "  container_system: ${BUILD_WITH_CONTAINER_SYSTEM}")
message(STATUS "========================================")
message(STATUS "")
