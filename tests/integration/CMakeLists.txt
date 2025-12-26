##################################################
# Integration Tests Configuration
##################################################

set(INTEGRATION_TEST_SOURCES
    test_basic_scenarios.cpp
    test_error_advanced_scenarios.cpp
    test_concurrency.cpp
)

# Add encrypted transfer tests if encryption is enabled
if(FILE_TRANS_ENABLE_ENCRYPTION AND OpenSSL_FOUND)
    list(APPEND INTEGRATION_TEST_SOURCES
        test_encrypted_transfer.cpp
    )
endif()

add_executable(file_trans_integration_tests ${INTEGRATION_TEST_SOURCES})

##################################################
# Cloud Integration Tests (S3/MinIO)
##################################################

if(FILE_TRANS_ENABLE_ENCRYPTION AND OpenSSL_FOUND)
    add_executable(file_trans_cloud_integration_tests
        cloud/test_s3_integration.cpp
    )

    target_include_directories(file_trans_cloud_integration_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(file_trans_cloud_integration_tests PRIVATE
        FileTransSystem
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
    )

    set_target_properties(file_trans_cloud_integration_tests PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    if(MSVC)
        target_compile_options(file_trans_cloud_integration_tests PRIVATE
            /wd4267
            /wd4996
        )
        target_compile_definitions(file_trans_cloud_integration_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    # Cloud tests are not auto-discovered since they require external services
    # Run manually with: MINIO_ENDPOINT=http://localhost:9000 ./bin/file_trans_cloud_integration_tests
    message(STATUS "Cloud integration tests enabled (requires MINIO_ENDPOINT environment variable)")
endif()

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

# MSVC warning suppression for external dependencies (logger_system)
if(MSVC)
    target_compile_options(file_trans_integration_tests PRIVATE
        /wd4267  # conversion from 'size_t' to 'unsigned int'
        /wd4996  # deprecated functions (getenv, strerror, etc.)
    )
    target_compile_definitions(file_trans_integration_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Enable test discovery
gtest_discover_tests(file_trans_integration_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES
        TIMEOUT 300
)

message(STATUS "Integration tests enabled")
