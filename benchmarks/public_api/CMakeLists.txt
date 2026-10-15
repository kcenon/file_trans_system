# This harness has no Google Benchmark dependency.
if(WIN32)
    message(FATAL_ERROR "Public API RSS benchmark currently supports Linux and macOS")
endif()
add_executable(file_trans_api_benchmark transfer_benchmark.cpp)
target_link_libraries(file_trans_api_benchmark PRIVATE FileTransSystem)
target_compile_features(file_trans_api_benchmark PRIVATE cxx_std_20)
set_target_properties(file_trans_api_benchmark PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
