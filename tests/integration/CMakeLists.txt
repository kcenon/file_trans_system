##################################################
# Integration Tests Configuration
##################################################

add_executable(file_trans_integration_tests
    test_basic_scenarios.cpp
    test_error_advanced_scenarios.cpp
    test_concurrency.cpp
)

target_include_directories(file_trans_integration_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(file_trans_integration_tests PRIVATE
    FileTransSystem
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

# Set properties
set_target_properties(file_trans_integration_tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Enable test discovery
gtest_discover_tests(file_trans_integration_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES
        TIMEOUT 300
)

message(STATUS "Integration tests enabled")
