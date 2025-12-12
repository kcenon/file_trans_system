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
# Installation
##################################################

install(TARGETS simple_server simple_client
    RUNTIME DESTINATION bin/examples
)
