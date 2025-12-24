##################################################
# File Transfer System Tests Configuration
##################################################

# Enable testing
enable_testing()

##################################################
# Google Test Configuration via FetchContent
##################################################

include(FetchContent)

# Declare Google Test dependency
# Note: Using v1.17.0 to match system-installed version and ensure ABI compatibility
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Disable installation of GTest
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

# Ensure GoogleTest uses the same C++ standard as the parent project
# This is critical for ABI compatibility on macOS/Linux with libc++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Make available
FetchContent_MakeAvailable(googletest)

# Explicitly set C++20 on gtest targets for ABI compatibility
set_target_properties(gtest PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
set_target_properties(gtest_main PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
set_target_properties(gmock PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
set_target_properties(gmock_main PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# Include GoogleTest module for test discovery
include(GoogleTest)

message(STATUS "Google Test ${googletest_VERSION} configured via FetchContent")

##################################################
# Unit Tests
##################################################

set(UNIT_TEST_SOURCES
    unit/test_version.cpp
    unit/core/test_checksum.cpp
    unit/core/test_chunk_splitter.cpp
    unit/core/test_chunk_assembler.cpp
    unit/core/test_core_types.cpp
    unit/core/test_resume_handler.cpp
    unit/core/test_bandwidth_limiter.cpp
    unit/core/test_logging.cpp
    unit/compression/test_compression_engine.cpp
    unit/protocol/test_types.cpp
    unit/state/test_state_management.cpp
    unit/server/test_server_pipeline.cpp
    unit/server/test_pipeline_jobs.cpp
    unit/server/test_quota_manager.cpp
    unit/client/test_transfer_control.cpp
    unit/client/test_batch_transfer.cpp
    unit/transport/test_transport_interface.cpp
    unit/transport/quic_transport_test.cpp
    unit/encryption/test_encryption_interface.cpp
    unit/cloud/test_cloud_interface.cpp
)

# Add encryption tests if enabled
if(FILE_TRANS_ENABLE_ENCRYPTION AND OpenSSL_FOUND)
    list(APPEND UNIT_TEST_SOURCES
        unit/encryption/test_aes_gcm_engine.cpp
    )
endif()

add_executable(file_trans_unit_tests ${UNIT_TEST_SOURCES})

target_link_libraries(file_trans_unit_tests PRIVATE
    FileTransSystem
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

# QUIC transport requires OpenSSL for TLS functionality
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_link_libraries(file_trans_unit_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# On Windows MSVC, static library transitive dependencies are not automatically
# propagated to executables, and the linker may not include unreferenced symbols
# from static libraries. We use /WHOLEARCHIVE to force inclusion of all symbols
# from network_system to resolve messaging_client external symbols.
# See: https://gitlab.kitware.com/cmake/cmake/-/issues/18075
if(MSVC AND NETWORK_SYSTEM_LIBRARY)
    # Use WHOLEARCHIVE to force all symbols from network_system to be included
    target_link_options(file_trans_unit_tests PRIVATE
        "/WHOLEARCHIVE:${NETWORK_SYSTEM_LIBRARY}"
    )
    # network_system depends on OpenSSL for TLS support
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        target_link_libraries(file_trans_unit_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
endif()

# Set properties
set_target_properties(file_trans_unit_tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# MSVC warning suppression for external dependencies (logger_system)
# These warnings come from logger_system headers and should not fail our build
if(MSVC)
    target_compile_options(file_trans_unit_tests PRIVATE
        /wd4267  # conversion from 'size_t' to 'unsigned int'
        /wd4996  # deprecated functions (getenv, strerror, etc.)
    )
    target_compile_definitions(file_trans_unit_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Enable test discovery
gtest_discover_tests(file_trans_unit_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES
        TIMEOUT 300
)

message(STATUS "Unit tests enabled")

##################################################
# Integration Tests (conditional)
#
# Integration tests require actual network connections and may be
# unstable in CI environments due to timing issues. Use
# FILE_TRANS_BUILD_INTEGRATION_TESTS=OFF to disable them.
##################################################

if(FILE_TRANS_BUILD_INTEGRATION_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration/CMakeLists.txt)
    add_subdirectory(integration)
    message(STATUS "Integration tests enabled")
else()
    message(STATUS "Integration tests disabled (FILE_TRANS_BUILD_INTEGRATION_TESTS=${FILE_TRANS_BUILD_INTEGRATION_TESTS})")
endif()

##################################################
# Code Coverage Support
##################################################

if(FILE_TRANS_ENABLE_COVERAGE)
    find_program(LCOV lcov)
    find_program(GENHTML genhtml)

    if(LCOV AND GENHTML)
        # Create coverage target
        add_custom_target(coverage
            COMMAND ${LCOV} --capture --directory . --output-file coverage.info
            COMMAND ${LCOV} --remove coverage.info '/usr/*' '*/tests/*' '*/googletest/*' --output-file coverage_filtered.info
            COMMAND ${GENHTML} coverage_filtered.info --output-directory coverage_report
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report..."
        )

        message(STATUS "Code coverage enabled (lcov: ${LCOV})")
    else()
        message(WARNING "lcov/genhtml not found - coverage reports will not be available")
    endif()
endif()

##################################################
# Test Data Setup
##################################################

# Create test data directory
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin/test_data)

# Copy test data files if they exist
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_data)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test_data
         DESTINATION ${CMAKE_BINARY_DIR}/bin)
endif()
