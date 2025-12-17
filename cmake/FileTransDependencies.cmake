##################################################
# File Transfer System Dependencies
#
# This module handles finding and configuring
# all dependencies for file_trans_system.
##################################################

include(FetchContent)

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

function(find_file_trans_dependencies)
    ##################################################
    # Find ecosystem dependencies
    ##################################################

    # common_system (Result<T>, error handling, time utilities)
    if(BUILD_WITH_COMMON_SYSTEM)
        if(NOT COMMON_SYSTEM_INCLUDE_DIR)
            # Try to find in parent directory (unified_system structure)
            set(_common_paths
                "${CMAKE_CURRENT_SOURCE_DIR}/../common_system/include"
                "${CMAKE_SOURCE_DIR}/../common_system/include"
            )

            foreach(_path ${_common_paths})
                if(EXISTS "${_path}/kcenon/common")
                    get_filename_component(COMMON_SYSTEM_INCLUDE_DIR "${_path}" ABSOLUTE)
                    break()
                endif()
            endforeach()
        endif()

        if(COMMON_SYSTEM_INCLUDE_DIR)
            message(STATUS "Found common_system: ${COMMON_SYSTEM_INCLUDE_DIR}")
            set(COMMON_SYSTEM_INCLUDE_DIR ${COMMON_SYSTEM_INCLUDE_DIR} PARENT_SCOPE)
        else()
            message(FATAL_ERROR "common_system not found - this is a required dependency. "
                "Please ensure common_system is available in the parent directory.")
        endif()
    endif()

    # thread_system (typed_thread_pool for pipeline parallelism)
    if(BUILD_WITH_THREAD_SYSTEM)
        if(NOT THREAD_SYSTEM_INCLUDE_DIR)
            set(_thread_paths
                "${CMAKE_CURRENT_SOURCE_DIR}/../thread_system/include"
                "${CMAKE_SOURCE_DIR}/../thread_system/include"
            )

            foreach(_path ${_thread_paths})
                if(EXISTS "${_path}/kcenon/thread")
                    get_filename_component(THREAD_SYSTEM_INCLUDE_DIR "${_path}" ABSOLUTE)
                    break()
                endif()
            endforeach()
        endif()

        if(THREAD_SYSTEM_INCLUDE_DIR)
            message(STATUS "Found thread_system: ${THREAD_SYSTEM_INCLUDE_DIR}")
            set(THREAD_SYSTEM_INCLUDE_DIR ${THREAD_SYSTEM_INCLUDE_DIR} PARENT_SCOPE)

            # Find thread_system library
            set(_thread_lib_paths
                "${CMAKE_CURRENT_SOURCE_DIR}/../thread_system/build/lib"
                "${CMAKE_CURRENT_SOURCE_DIR}/../thread_system/build"
                "${CMAKE_SOURCE_DIR}/../thread_system/build/lib"
                "${CMAKE_SOURCE_DIR}/../thread_system/build"
            )

            find_library(THREAD_SYSTEM_LIBRARY
                NAMES ThreadSystem thread_system
                PATHS ${_thread_lib_paths}
                NO_DEFAULT_PATH
            )

            if(THREAD_SYSTEM_LIBRARY)
                message(STATUS "Found thread_system library: ${THREAD_SYSTEM_LIBRARY}")
                set(THREAD_SYSTEM_LIBRARY ${THREAD_SYSTEM_LIBRARY} PARENT_SCOPE)
            else()
                message(WARNING "thread_system library not found - linking may fail")
            endif()
        else()
            message(FATAL_ERROR "thread_system not found - this is a required dependency. "
                "Please ensure thread_system is available in the parent directory.")
        endif()
    endif()

    # network_system (TCP/TLS transport layer)
    if(BUILD_WITH_NETWORK_SYSTEM)
        if(NOT NETWORK_SYSTEM_INCLUDE_DIR)
            set(_network_paths
                "${CMAKE_CURRENT_SOURCE_DIR}/../network_system/include"
                "${CMAKE_SOURCE_DIR}/../network_system/include"
            )

            foreach(_path ${_network_paths})
                if(EXISTS "${_path}/network_system")
                    get_filename_component(NETWORK_SYSTEM_INCLUDE_DIR "${_path}" ABSOLUTE)
                    break()
                endif()
            endforeach()
        endif()

        if(NETWORK_SYSTEM_INCLUDE_DIR)
            message(STATUS "Found network_system: ${NETWORK_SYSTEM_INCLUDE_DIR}")
            set(NETWORK_SYSTEM_INCLUDE_DIR ${NETWORK_SYSTEM_INCLUDE_DIR} PARENT_SCOPE)

            # Find network_system library
            set(_network_lib_paths
                "${CMAKE_CURRENT_SOURCE_DIR}/../network_system/build/lib"
                "${CMAKE_CURRENT_SOURCE_DIR}/../network_system/build"
                "${CMAKE_SOURCE_DIR}/../network_system/build/lib"
                "${CMAKE_SOURCE_DIR}/../network_system/build"
            )

            find_library(NETWORK_SYSTEM_LIBRARY
                NAMES NetworkSystem network_system
                PATHS ${_network_lib_paths}
                NO_DEFAULT_PATH
            )

            if(NETWORK_SYSTEM_LIBRARY)
                message(STATUS "Found network_system library: ${NETWORK_SYSTEM_LIBRARY}")
                set(NETWORK_SYSTEM_LIBRARY ${NETWORK_SYSTEM_LIBRARY} PARENT_SCOPE)
            else()
                message(WARNING "network_system library not found - linking may fail")
            endif()

            # ASIO is required when using network_system (network_system headers include asio.hpp)
            find_asio_library()
            set(ASIO_FOUND ${ASIO_FOUND} PARENT_SCOPE)
            set(ASIO_INCLUDE_DIR ${ASIO_INCLUDE_DIR} PARENT_SCOPE)
            set(ASIO_TARGET ${ASIO_TARGET} PARENT_SCOPE)
        else()
            message(FATAL_ERROR "network_system not found - this is a required dependency. "
                "Please ensure network_system is available in the parent directory.")
        endif()
    endif()

    # container_system (bounded_queue for backpressure)
    if(BUILD_WITH_CONTAINER_SYSTEM)
        if(NOT CONTAINER_SYSTEM_INCLUDE_DIR)
            set(_container_paths
                "${CMAKE_CURRENT_SOURCE_DIR}/../container_system/include"
                "${CMAKE_SOURCE_DIR}/../container_system/include"
            )

            foreach(_path ${_container_paths})
                if(EXISTS "${_path}/container")
                    get_filename_component(CONTAINER_SYSTEM_INCLUDE_DIR "${_path}" ABSOLUTE)
                    break()
                endif()
            endforeach()
        endif()

        if(CONTAINER_SYSTEM_INCLUDE_DIR)
            message(STATUS "Found container_system: ${CONTAINER_SYSTEM_INCLUDE_DIR}")
            set(CONTAINER_SYSTEM_INCLUDE_DIR ${CONTAINER_SYSTEM_INCLUDE_DIR} PARENT_SCOPE)
        else()
            message(FATAL_ERROR "container_system not found - this is a required dependency. "
                "Please ensure container_system is available in the parent directory.")
        endif()
    endif()

    # logger_system (structured logging support)
    if(BUILD_WITH_LOGGER_SYSTEM)
        if(NOT LOGGER_SYSTEM_INCLUDE_DIR)
            set(_logger_paths
                "${CMAKE_CURRENT_SOURCE_DIR}/../logger_system/include"
                "${CMAKE_SOURCE_DIR}/../logger_system/include"
            )

            foreach(_path ${_logger_paths})
                if(EXISTS "${_path}/kcenon/logger")
                    get_filename_component(LOGGER_SYSTEM_INCLUDE_DIR "${_path}" ABSOLUTE)
                    break()
                endif()
            endforeach()
        endif()

        if(LOGGER_SYSTEM_INCLUDE_DIR)
            message(STATUS "Found logger_system: ${LOGGER_SYSTEM_INCLUDE_DIR}")
            set(LOGGER_SYSTEM_INCLUDE_DIR ${LOGGER_SYSTEM_INCLUDE_DIR} PARENT_SCOPE)

            # Find logger_system library
            set(_logger_lib_paths
                "${CMAKE_CURRENT_SOURCE_DIR}/../logger_system/build/lib"
                "${CMAKE_CURRENT_SOURCE_DIR}/../logger_system/build"
                "${CMAKE_SOURCE_DIR}/../logger_system/build/lib"
                "${CMAKE_SOURCE_DIR}/../logger_system/build"
            )

            find_library(LOGGER_SYSTEM_LIBRARY
                NAMES LoggerSystem logger_system
                PATHS ${_logger_lib_paths}
                NO_DEFAULT_PATH
            )

            if(LOGGER_SYSTEM_LIBRARY)
                message(STATUS "Found logger_system library: ${LOGGER_SYSTEM_LIBRARY}")
                set(LOGGER_SYSTEM_LIBRARY ${LOGGER_SYSTEM_LIBRARY} PARENT_SCOPE)
            else()
                message(WARNING "logger_system library not found - linking may fail")
            endif()
        else()
            message(WARNING "logger_system not found - logging features will be unavailable")
        endif()
    endif()

endfunction()
