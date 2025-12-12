##################################################
# File Transfer System Benchmarks Configuration
##################################################

##################################################
# Google Benchmark Configuration via FetchContent
##################################################

include(FetchContent)

# Declare Google Benchmark dependency
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
    GIT_SHALLOW TRUE
)

# Disable testing in Google Benchmark
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

# Make available
FetchContent_MakeAvailable(googlebenchmark)

message(STATUS "Google Benchmark configured via FetchContent")

##################################################
# Benchmark Utilities Library
##################################################

add_library(benchmark_utils STATIC
    utils/benchmark_helpers.cpp
)

target_include_directories(benchmark_utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(benchmark_utils
    PUBLIC
        FileTransSystem
        benchmark::benchmark
)

set_target_properties(benchmark_utils PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Throughput Benchmarks
##################################################

add_executable(throughput_benchmarks
    throughput/bench_single_file_throughput.cpp
    throughput/bench_chunk_operations.cpp
)

target_link_libraries(throughput_benchmarks PRIVATE
    FileTransSystem
    benchmark_utils
    benchmark::benchmark
    benchmark::benchmark_main
    Threads::Threads
)

set_target_properties(throughput_benchmarks PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

##################################################
# Compression Benchmarks (Future)
##################################################

# add_executable(compression_benchmarks
#     compression/bench_lz4_compression.cpp
# )

##################################################
# Memory Benchmarks (Future)
##################################################

# add_executable(memory_benchmarks
#     memory/bench_memory_usage.cpp
# )

##################################################
# Benchmark Data Setup
##################################################

# Create benchmark data directory
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin/benchmark_data)

##################################################
# Custom Targets for Running Benchmarks
##################################################

add_custom_target(run_throughput_benchmarks
    COMMAND $<TARGET_FILE:throughput_benchmarks>
        --benchmark_format=console
        --benchmark_out=${CMAKE_BINARY_DIR}/benchmark_results/throughput.json
        --benchmark_out_format=json
    DEPENDS throughput_benchmarks
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    COMMENT "Running throughput benchmarks..."
)

add_custom_target(run_benchmarks
    DEPENDS run_throughput_benchmarks
    COMMENT "Running all benchmarks..."
)

# Create results directory
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/benchmark_results)

message(STATUS "Benchmarks configured")
