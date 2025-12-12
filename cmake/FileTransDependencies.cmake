##################################################
# File Transfer System Dependencies
#
# This module handles finding and configuring
# all dependencies for file_trans_system.
##################################################

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
                if(EXISTS "${_path}/common_system")
                    get_filename_component(COMMON_SYSTEM_INCLUDE_DIR "${_path}" ABSOLUTE)
                    break()
                endif()
            endforeach()
        endif()

        if(COMMON_SYSTEM_INCLUDE_DIR)
            message(STATUS "Found common_system: ${COMMON_SYSTEM_INCLUDE_DIR}")
            set(COMMON_SYSTEM_INCLUDE_DIR ${COMMON_SYSTEM_INCLUDE_DIR} PARENT_SCOPE)
        else()
            message(WARNING "common_system not found - some features will be unavailable")
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
                if(EXISTS "${_path}/thread_system")
                    get_filename_component(THREAD_SYSTEM_INCLUDE_DIR "${_path}" ABSOLUTE)
                    break()
                endif()
            endforeach()
        endif()

        if(THREAD_SYSTEM_INCLUDE_DIR)
            message(STATUS "Found thread_system: ${THREAD_SYSTEM_INCLUDE_DIR}")
            set(THREAD_SYSTEM_INCLUDE_DIR ${THREAD_SYSTEM_INCLUDE_DIR} PARENT_SCOPE)
        else()
            message(WARNING "thread_system not found - some features will be unavailable")
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
        else()
            message(WARNING "network_system not found - some features will be unavailable")
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
                if(EXISTS "${_path}/container_system")
                    get_filename_component(CONTAINER_SYSTEM_INCLUDE_DIR "${_path}" ABSOLUTE)
                    break()
                endif()
            endforeach()
        endif()

        if(CONTAINER_SYSTEM_INCLUDE_DIR)
            message(STATUS "Found container_system: ${CONTAINER_SYSTEM_INCLUDE_DIR}")
            set(CONTAINER_SYSTEM_INCLUDE_DIR ${CONTAINER_SYSTEM_INCLUDE_DIR} PARENT_SCOPE)
        else()
            message(WARNING "container_system not found - some features will be unavailable")
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
        else()
            message(WARNING "logger_system not found - logging features will be unavailable")
        endif()
    endif()

endfunction()
