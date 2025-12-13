##################################################
# File Transfer System Dependencies
#
# This module handles finding and configuring
# all dependencies for file_trans_system.
#
# Dependencies are automatically fetched and built from GitHub
# if not found locally (e.g., in unified_system structure).
##################################################

include(FetchContent)

# Git tags for ecosystem dependencies
set(COMMON_SYSTEM_GIT_TAG "main" CACHE STRING "Git tag/branch for common_system")
set(THREAD_SYSTEM_GIT_TAG "main" CACHE STRING "Git tag/branch for thread_system")
set(NETWORK_SYSTEM_GIT_TAG "main" CACHE STRING "Git tag/branch for network_system")
set(CONTAINER_SYSTEM_GIT_TAG "main" CACHE STRING "Git tag/branch for container_system")
set(LOGGER_SYSTEM_GIT_TAG "main" CACHE STRING "Git tag/branch for logger_system")

# Track which dependencies were fetched vs found locally
set(COMMON_SYSTEM_FETCHED FALSE CACHE INTERNAL "")
set(THREAD_SYSTEM_FETCHED FALSE CACHE INTERNAL "")
set(NETWORK_SYSTEM_FETCHED FALSE CACHE INTERNAL "")
set(CONTAINER_SYSTEM_FETCHED FALSE CACHE INTERNAL "")
set(LOGGER_SYSTEM_FETCHED FALSE CACHE INTERNAL "")

##################################################
# Find ASIO (required for network_system headers)
##################################################
function(find_asio_library)
    message(STATUS "Looking for ASIO library...")

    # Try CMake config package first (vcpkg provides asioConfig.cmake)
    find_package(asio QUIET CONFIG)
    if(TARGET asio::asio)
        message(STATUS "Found ASIO via CMake package (target: asio::asio)")
        set(ASIO_FOUND TRUE PARENT_SCOPE)
        set(ASIO_TARGET asio::asio PARENT_SCOPE)
        return()
    endif()

    # Standard header search (respects CMAKE_PREFIX_PATH set by vcpkg)
    find_path(ASIO_INCLUDE_DIR
        NAMES asio.hpp
        DOC "Path to standalone ASIO headers"
    )

    # Fallback: explicit common locations
    if(NOT ASIO_INCLUDE_DIR)
        find_path(ASIO_INCLUDE_DIR
            NAMES asio.hpp
            PATHS
                /opt/homebrew/include
                /usr/local/include
                /usr/include
            NO_DEFAULT_PATH
            DOC "Path to standalone ASIO headers in common locations"
        )
    endif()

    if(ASIO_INCLUDE_DIR)
        message(STATUS "Found standalone ASIO at: ${ASIO_INCLUDE_DIR}")
        set(ASIO_INCLUDE_DIR ${ASIO_INCLUDE_DIR} PARENT_SCOPE)
        set(ASIO_FOUND TRUE PARENT_SCOPE)
        return()
    endif()

    # Fetch standalone ASIO as a last resort
    message(STATUS "Standalone ASIO not found locally - fetching asio-1-36-0 from upstream...")

    FetchContent_Declare(file_trans_asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-36-0
    )

    FetchContent_GetProperties(file_trans_asio)
    if(NOT file_trans_asio_POPULATED)
        FetchContent_Populate(file_trans_asio)
    endif()

    set(_asio_include_dir "${file_trans_asio_SOURCE_DIR}/asio/include")

    if(EXISTS "${_asio_include_dir}/asio.hpp")
        message(STATUS "Fetched standalone ASIO headers at: ${_asio_include_dir}")
        set(ASIO_INCLUDE_DIR ${_asio_include_dir} PARENT_SCOPE)
        set(ASIO_FOUND TRUE PARENT_SCOPE)
        set(ASIO_FETCHED TRUE PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR "Failed to find or fetch standalone ASIO - cannot build without ASIO")
endfunction()

##################################################
# Helper: Check if local path exists
##################################################
function(check_local_dependency NAME HEADER_SUBPATH RESULT_VAR INCLUDE_DIR_VAR)
    set(_paths
        "${CMAKE_CURRENT_SOURCE_DIR}/../${NAME}/include"
        "${CMAKE_SOURCE_DIR}/../${NAME}/include"
    )

    foreach(_path ${_paths})
        if(EXISTS "${_path}/${HEADER_SUBPATH}")
            get_filename_component(_abs_path "${_path}" ABSOLUTE)
            set(${RESULT_VAR} TRUE PARENT_SCOPE)
            set(${INCLUDE_DIR_VAR} "${_abs_path}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${RESULT_VAR} FALSE PARENT_SCOPE)
endfunction()

##################################################
# Helper: Find local library
##################################################
function(find_local_library NAME LIB_NAMES RESULT_VAR)
    set(_lib_paths
        "${CMAKE_CURRENT_SOURCE_DIR}/../${NAME}/build/lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/../${NAME}/build"
        "${CMAKE_SOURCE_DIR}/../${NAME}/build/lib"
        "${CMAKE_SOURCE_DIR}/../${NAME}/build"
    )

    # Use unique cache variable name to avoid cache conflicts
    set(_cache_var "_${NAME}_LIBRARY_PATH")

    # Clear previous cache entry
    unset(${_cache_var} CACHE)

    find_library(${_cache_var}
        NAMES ${LIB_NAMES}
        PATHS ${_lib_paths}
        NO_DEFAULT_PATH
    )

    if(${_cache_var})
        set(${RESULT_VAR} "${${_cache_var}}" PARENT_SCOPE)
    else()
        set(${RESULT_VAR} "" PARENT_SCOPE)
    endif()
endfunction()

##################################################
# Main dependency discovery function
##################################################
function(find_file_trans_dependencies)
    # First, find ASIO (required for network_system)
    find_asio_library()
    set(ASIO_FOUND ${ASIO_FOUND} PARENT_SCOPE)
    set(ASIO_INCLUDE_DIR ${ASIO_INCLUDE_DIR} PARENT_SCOPE)
    set(ASIO_TARGET ${ASIO_TARGET} PARENT_SCOPE)

    ##################################################
    # common_system (Result<T>, error handling, time utilities)
    # Tier 0 - no dependencies
    ##################################################
    if(BUILD_WITH_COMMON_SYSTEM)
        check_local_dependency(common_system "kcenon/common" _found COMMON_SYSTEM_INCLUDE_DIR)

        if(_found)
            message(STATUS "Found common_system locally: ${COMMON_SYSTEM_INCLUDE_DIR}")
            set(COMMON_SYSTEM_INCLUDE_DIR ${COMMON_SYSTEM_INCLUDE_DIR} PARENT_SCOPE)
            find_local_library(common_system "CommonSystem;common_system" COMMON_SYSTEM_LIBRARY)
            if(COMMON_SYSTEM_LIBRARY)
                message(STATUS "Found common_system library: ${COMMON_SYSTEM_LIBRARY}")
                set(COMMON_SYSTEM_LIBRARY ${COMMON_SYSTEM_LIBRARY} PARENT_SCOPE)
            endif()
        else()
            message(STATUS "common_system not found locally - fetching and building from GitHub...")
            set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)

            FetchContent_Declare(common_system
                GIT_REPOSITORY https://github.com/kcenon/common_system.git
                GIT_TAG ${COMMON_SYSTEM_GIT_TAG}
            )
            FetchContent_MakeAvailable(common_system)

            set(COMMON_SYSTEM_INCLUDE_DIR "${common_system_SOURCE_DIR}/include" PARENT_SCOPE)
            set(COMMON_SYSTEM_FETCHED TRUE CACHE INTERNAL "")
            message(STATUS "Fetched and built common_system")
        endif()
    endif()

    ##################################################
    # thread_system (typed_thread_pool for pipeline parallelism)
    # Tier 1 - depends on common_system
    ##################################################
    if(BUILD_WITH_THREAD_SYSTEM)
        check_local_dependency(thread_system "kcenon/thread" _found THREAD_SYSTEM_INCLUDE_DIR)

        if(_found)
            message(STATUS "Found thread_system locally: ${THREAD_SYSTEM_INCLUDE_DIR}")
            set(THREAD_SYSTEM_INCLUDE_DIR ${THREAD_SYSTEM_INCLUDE_DIR} PARENT_SCOPE)
            find_local_library(thread_system "ThreadSystem;thread_system" THREAD_SYSTEM_LIBRARY)
            if(THREAD_SYSTEM_LIBRARY)
                message(STATUS "Found thread_system library: ${THREAD_SYSTEM_LIBRARY}")
                set(THREAD_SYSTEM_LIBRARY ${THREAD_SYSTEM_LIBRARY} PARENT_SCOPE)
            endif()
        else()
            message(STATUS "thread_system not found locally - fetching and building from GitHub...")
            set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)

            FetchContent_Declare(thread_system
                GIT_REPOSITORY https://github.com/kcenon/thread_system.git
                GIT_TAG ${THREAD_SYSTEM_GIT_TAG}
            )
            FetchContent_MakeAvailable(thread_system)

            set(THREAD_SYSTEM_INCLUDE_DIR "${thread_system_SOURCE_DIR}/include" PARENT_SCOPE)
            set(THREAD_SYSTEM_FETCHED TRUE CACHE INTERNAL "")
            message(STATUS "Fetched and built thread_system")
        endif()
    endif()

    ##################################################
    # logger_system (structured logging support)
    # Tier 2 - depends on common_system, thread_system
    ##################################################
    if(BUILD_WITH_LOGGER_SYSTEM)
        check_local_dependency(logger_system "kcenon/logger" _found LOGGER_SYSTEM_INCLUDE_DIR)

        if(_found)
            message(STATUS "Found logger_system locally: ${LOGGER_SYSTEM_INCLUDE_DIR}")
            set(LOGGER_SYSTEM_INCLUDE_DIR ${LOGGER_SYSTEM_INCLUDE_DIR} PARENT_SCOPE)
            find_local_library(logger_system "LoggerSystem;logger_system" LOGGER_SYSTEM_LIBRARY)
            if(LOGGER_SYSTEM_LIBRARY)
                message(STATUS "Found logger_system library: ${LOGGER_SYSTEM_LIBRARY}")
                set(LOGGER_SYSTEM_LIBRARY ${LOGGER_SYSTEM_LIBRARY} PARENT_SCOPE)
            endif()
        else()
            message(STATUS "logger_system not found locally - fetching and building from GitHub...")
            set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)

            FetchContent_Declare(logger_system
                GIT_REPOSITORY https://github.com/kcenon/logger_system.git
                GIT_TAG ${LOGGER_SYSTEM_GIT_TAG}
            )
            FetchContent_MakeAvailable(logger_system)

            set(LOGGER_SYSTEM_INCLUDE_DIR "${logger_system_SOURCE_DIR}/include" PARENT_SCOPE)
            set(LOGGER_SYSTEM_FETCHED TRUE CACHE INTERNAL "")
            message(STATUS "Fetched and built logger_system")
        endif()
    endif()

    ##################################################
    # container_system (bounded_queue for backpressure)
    # Header-only library
    ##################################################
    if(BUILD_WITH_CONTAINER_SYSTEM)
        check_local_dependency(container_system "container" _found CONTAINER_SYSTEM_INCLUDE_DIR)

        if(_found)
            message(STATUS "Found container_system locally: ${CONTAINER_SYSTEM_INCLUDE_DIR}")
            set(CONTAINER_SYSTEM_INCLUDE_DIR ${CONTAINER_SYSTEM_INCLUDE_DIR} PARENT_SCOPE)
        else()
            message(STATUS "container_system not found locally - fetching from GitHub...")
            set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)

            FetchContent_Declare(container_system
                GIT_REPOSITORY https://github.com/kcenon/container_system.git
                GIT_TAG ${CONTAINER_SYSTEM_GIT_TAG}
            )
            FetchContent_MakeAvailable(container_system)

            set(CONTAINER_SYSTEM_INCLUDE_DIR "${container_system_SOURCE_DIR}/include" PARENT_SCOPE)
            set(CONTAINER_SYSTEM_FETCHED TRUE CACHE INTERNAL "")
            message(STATUS "Fetched container_system")
        endif()
    endif()

    ##################################################
    # network_system (TCP/TLS transport layer)
    # Tier 4 - depends on common_system, thread_system, logger_system
    ##################################################
    if(BUILD_WITH_NETWORK_SYSTEM)
        check_local_dependency(network_system "kcenon/network" _found NETWORK_SYSTEM_INCLUDE_DIR)

        # Also check legacy path
        if(NOT _found)
            check_local_dependency(network_system "network_system" _found NETWORK_SYSTEM_INCLUDE_DIR)
        endif()

        if(_found)
            message(STATUS "Found network_system locally: ${NETWORK_SYSTEM_INCLUDE_DIR}")
            set(NETWORK_SYSTEM_INCLUDE_DIR ${NETWORK_SYSTEM_INCLUDE_DIR} PARENT_SCOPE)
            find_local_library(network_system "NetworkSystem;network_system" NETWORK_SYSTEM_LIBRARY)
            if(NETWORK_SYSTEM_LIBRARY)
                message(STATUS "Found network_system library: ${NETWORK_SYSTEM_LIBRARY}")
                set(NETWORK_SYSTEM_LIBRARY ${NETWORK_SYSTEM_LIBRARY} PARENT_SCOPE)
            endif()
        else()
            message(STATUS "network_system not found locally - fetching and building from GitHub...")
            set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
            set(BUILD_WEBSOCKET_SUPPORT OFF CACHE BOOL "" FORCE)
            set(BUILD_MESSAGING_BRIDGE OFF CACHE BOOL "" FORCE)
            set(NETWORK_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
            set(NETWORK_BUILD_INTEGRATION_TESTS OFF CACHE BOOL "" FORCE)

            FetchContent_Declare(network_system
                GIT_REPOSITORY https://github.com/kcenon/network_system.git
                GIT_TAG ${NETWORK_SYSTEM_GIT_TAG}
            )
            FetchContent_MakeAvailable(network_system)

            set(NETWORK_SYSTEM_INCLUDE_DIR "${network_system_SOURCE_DIR}/include" PARENT_SCOPE)
            set(NETWORK_SYSTEM_FETCHED TRUE CACHE INTERNAL "")
            message(STATUS "Fetched and built network_system")
        endif()
    endif()

endfunction()
