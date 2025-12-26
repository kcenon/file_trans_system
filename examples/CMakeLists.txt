##################################################
# File Transfer System Examples
##################################################

message(STATUS "Building examples")

# Output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

##################################################
# Simple Server Example
##################################################

add_executable(simple_server
    simple_server.cpp
)

target_link_libraries(simple_server
    PRIVATE
        kcenon::file_transfer
)

set_target_properties(simple_server PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Simple Client Example
##################################################

add_executable(simple_client
    simple_client.cpp
)

target_link_libraries(simple_client
    PRIVATE
        kcenon::file_transfer
)

set_target_properties(simple_client PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Upload Example
##################################################

add_executable(upload_example
    upload_example.cpp
)

target_link_libraries(upload_example
    PRIVATE
        kcenon::file_transfer
)

set_target_properties(upload_example PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Batch Upload Example
##################################################

add_executable(batch_upload_example
    batch_upload_example.cpp
)

target_link_libraries(batch_upload_example
    PRIVATE
        kcenon::file_transfer
)

set_target_properties(batch_upload_example PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Download Example
##################################################

add_executable(download_example
    download_example.cpp
)

target_link_libraries(download_example
    PRIVATE
        kcenon::file_transfer
)

set_target_properties(download_example PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Batch Download Example
##################################################

add_executable(batch_download_example
    batch_download_example.cpp
)

target_link_libraries(batch_download_example
    PRIVATE
        kcenon::file_transfer
)

set_target_properties(batch_download_example PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Resume Transfer Example
##################################################

add_executable(resume_transfer
    resume_transfer.cpp
)

target_link_libraries(resume_transfer
    PRIVATE
        kcenon::file_transfer
)

set_target_properties(resume_transfer PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Auto Reconnect Example
##################################################

add_executable(auto_reconnect
    auto_reconnect.cpp
)

target_link_libraries(auto_reconnect
    PRIVATE
        kcenon::file_transfer
)

set_target_properties(auto_reconnect PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Custom Pipeline Example
##################################################

add_executable(custom_pipeline
    custom_pipeline.cpp
)

target_link_libraries(custom_pipeline
    PRIVATE
        kcenon::file_transfer
)

set_target_properties(custom_pipeline PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Bandwidth Throttling Example
##################################################

add_executable(bandwidth_throttling
    bandwidth_throttling.cpp
)

target_link_libraries(bandwidth_throttling
    PRIVATE
        kcenon::file_transfer
)

set_target_properties(bandwidth_throttling PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Server Callbacks Example
##################################################

add_executable(server_callbacks
    server_callbacks.cpp
)

target_link_libraries(server_callbacks
    PRIVATE
        kcenon::file_transfer
)

set_target_properties(server_callbacks PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Quota Management Example
##################################################

add_executable(quota_management
    quota_management.cpp
)

target_link_libraries(quota_management
    PRIVATE
        kcenon::file_transfer
)

set_target_properties(quota_management PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Cloud Storage Examples (require encryption for signing)
##################################################

if(FILE_TRANS_ENABLE_ENCRYPTION AND OpenSSL_FOUND)
    add_executable(s3_example
        cloud/s3_example.cpp
    )

    target_link_libraries(s3_example
        PRIVATE
            kcenon::file_transfer
    )

    set_target_properties(s3_example PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

    add_executable(azure_blob_example
        cloud/azure_blob_example.cpp
    )

    target_link_libraries(azure_blob_example
        PRIVATE
            kcenon::file_transfer
    )

    set_target_properties(azure_blob_example PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

    add_executable(gcs_example
        cloud/gcs_example.cpp
    )

    target_link_libraries(gcs_example
        PRIVATE
            kcenon::file_transfer
    )

    set_target_properties(gcs_example PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

    add_executable(hybrid_storage_example
        cloud/hybrid_storage_example.cpp
    )

    target_link_libraries(hybrid_storage_example
        PRIVATE
            kcenon::file_transfer
    )

    set_target_properties(hybrid_storage_example PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

    add_executable(multi_cloud_failover_example
        cloud/multi_cloud_failover_example.cpp
    )

    target_link_libraries(multi_cloud_failover_example
        PRIVATE
            kcenon::file_transfer
    )

    set_target_properties(multi_cloud_failover_example PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

    add_executable(large_file_transfer_example
        cloud/large_file_transfer_example.cpp
    )

    target_link_libraries(large_file_transfer_example
        PRIVATE
            kcenon::file_transfer
    )

    set_target_properties(large_file_transfer_example PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

    message(STATUS "Building cloud storage examples")
endif()

##################################################
# Installation
##################################################

set(EXAMPLE_TARGETS
    simple_server
    simple_client
    upload_example
    batch_upload_example
    download_example
    batch_download_example
    resume_transfer
    auto_reconnect
    custom_pipeline
    bandwidth_throttling
    server_callbacks
    quota_management
)

if(FILE_TRANS_ENABLE_ENCRYPTION AND OpenSSL_FOUND)
    list(APPEND EXAMPLE_TARGETS
        s3_example
        azure_blob_example
        gcs_example
        hybrid_storage_example
        multi_cloud_failover_example
        large_file_transfer_example
    )
endif()

install(TARGETS ${EXAMPLE_TARGETS}
    RUNTIME DESTINATION bin/examples
)
