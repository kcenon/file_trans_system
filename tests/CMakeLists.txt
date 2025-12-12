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
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Disable installation of GTest
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

# Make available
FetchContent_MakeAvailable(googletest)

# Include GoogleTest module for test discovery
include(GoogleTest)

message(STATUS "Google Test ${googletest_VERSION} configured via FetchContent")

##################################################
# Unit Tests
##################################################

add_executable(file_trans_unit_tests
    unit/test_version.cpp
    unit/core/test_checksum.cpp
    unit/core/test_chunk_splitter.cpp
    unit/core/test_chunk_assembler.cpp
    unit/core/test_core_types.cpp
    unit/compression/test_compression_engine.cpp
    unit/protocol/test_types.cpp
    unit/state/test_state_management.cpp
)

target_link_libraries(file_trans_unit_tests PRIVATE
    FileTransSystem
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

# Set properties
set_target_properties(file_trans_unit_tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Enable test discovery
gtest_discover_tests(file_trans_unit_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES
        TIMEOUT 300
)

message(STATUS "Unit tests enabled")

##################################################
# Integration Tests (conditional)
##################################################

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration/CMakeLists.txt)
    add_subdirectory(integration)
    message(STATUS "Integration tests enabled")
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
