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
# Compression Benchmarks
##################################################

add_executable(compression_benchmarks
    compression/bench_lz4_compression.cpp
)

target_link_libraries(compression_benchmarks PRIVATE
    FileTransSystem
    benchmark_utils
    benchmark::benchmark
    benchmark::benchmark_main
    Threads::Threads
)

set_target_properties(compression_benchmarks PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

##################################################
# Latency Benchmarks
##################################################

add_executable(latency_benchmarks
    latency/bench_latency.cpp
)

target_link_libraries(latency_benchmarks PRIVATE
    FileTransSystem
    benchmark_utils
    benchmark::benchmark
    benchmark::benchmark_main
    Threads::Threads
)

set_target_properties(latency_benchmarks PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

##################################################
# Memory Benchmarks
##################################################

add_executable(memory_benchmarks
    memory/bench_memory_usage.cpp
)

target_link_libraries(memory_benchmarks PRIVATE
    FileTransSystem
    benchmark_utils
    benchmark::benchmark
    benchmark::benchmark_main
    Threads::Threads
)

set_target_properties(memory_benchmarks PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

##################################################
# Scalability Benchmarks
##################################################

add_executable(scalability_benchmarks
    scalability/bench_scalability.cpp
)

target_link_libraries(scalability_benchmarks PRIVATE
    FileTransSystem
    benchmark_utils
    benchmark::benchmark
    benchmark::benchmark_main
    Threads::Threads
)

set_target_properties(scalability_benchmarks PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

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

add_custom_target(run_compression_benchmarks
    COMMAND $<TARGET_FILE:compression_benchmarks>
        --benchmark_format=console
        --benchmark_out=${CMAKE_BINARY_DIR}/benchmark_results/compression.json
        --benchmark_out_format=json
    DEPENDS compression_benchmarks
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    COMMENT "Running compression benchmarks..."
)

add_custom_target(run_latency_benchmarks
    COMMAND $<TARGET_FILE:latency_benchmarks>
        --benchmark_format=console
        --benchmark_out=${CMAKE_BINARY_DIR}/benchmark_results/latency.json
        --benchmark_out_format=json
    DEPENDS latency_benchmarks
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    COMMENT "Running latency benchmarks..."
)

add_custom_target(run_memory_benchmarks
    COMMAND $<TARGET_FILE:memory_benchmarks>
        --benchmark_format=console
        --benchmark_out=${CMAKE_BINARY_DIR}/benchmark_results/memory.json
        --benchmark_out_format=json
    DEPENDS memory_benchmarks
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    COMMENT "Running memory benchmarks..."
)

add_custom_target(run_scalability_benchmarks
    COMMAND $<TARGET_FILE:scalability_benchmarks>
        --benchmark_format=console
        --benchmark_out=${CMAKE_BINARY_DIR}/benchmark_results/scalability.json
        --benchmark_out_format=json
    DEPENDS scalability_benchmarks
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    COMMENT "Running scalability benchmarks..."
)

add_custom_target(run_benchmarks
    DEPENDS run_throughput_benchmarks run_compression_benchmarks run_latency_benchmarks run_memory_benchmarks run_scalability_benchmarks
    COMMENT "Running all benchmarks..."
)

# Create results directory
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/benchmark_results)

message(STATUS "Benchmarks configured")
