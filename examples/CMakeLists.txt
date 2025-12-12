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
# Installation
##################################################

install(TARGETS
    simple_server
    simple_client
    upload_example
    batch_upload_example
    download_example
    batch_download_example
    RUNTIME DESTINATION bin/examples
)
