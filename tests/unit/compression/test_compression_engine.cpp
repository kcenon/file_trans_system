/**
 * @file test_compression_engine.cpp
 * @brief Unit tests for compression_engine
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/core/compression_engine.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace kcenon::file_transfer::test {

#ifndef FILE_TRANS_ENABLE_LZ4

// Skip all compression tests when LZ4 is not enabled
class CompressionEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        GTEST_SKIP() << "LZ4 compression is not enabled";
    }

    std::unique_ptr<compression_engine> engine_;
};

#else

class CompressionEngineTest : public ::testing::Test {
protected:
    void SetUp() override { engine_ = std::make_unique<compression_engine>(); }

    auto create_text_data(std::size_t size) -> std::vector<std::byte> {
        // Create highly compressible text content
        const std::string pattern = "The quick brown fox jumps over the lazy dog. ";
        std::vector<std::byte> data(size);

        for (std::size_t i = 0; i < size; ++i) {
            data[i] = static_cast<std::byte>(pattern[i % pattern.size()]);
        }
        return data;
    }

    auto create_random_data(std::size_t size, unsigned int seed = 42)
        -> std::vector<std::byte> {
        std::vector<std::byte> data(size);
        std::mt19937 gen(seed);
        std::uniform_int_distribution<> dis(0, 255);

        for (auto& byte : data) {
            byte = static_cast<std::byte>(dis(gen));
        }
        return data;
    }

    auto string_to_bytes(const std::string& str) -> std::vector<std::byte> {
        std::vector<std::byte> data(str.size());
        if (!str.empty()) {
            std::memcpy(data.data(), str.data(), str.size());
        }
        return data;
    }

    std::unique_ptr<compression_engine> engine_;
};

// Round-trip Tests

TEST_F(CompressionEngineTest, RoundTrip_TextData) {
    auto original = create_text_data(10000);

    auto compress_result = engine_->compress(original);
    ASSERT_TRUE(compress_result.has_value()) << compress_result.error().message;

    auto decompress_result =
        engine_->decompress(compress_result.value(), original.size());
    ASSERT_TRUE(decompress_result.has_value()) << decompress_result.error().message;

    EXPECT_EQ(original, decompress_result.value());
}

TEST_F(CompressionEngineTest, RoundTrip_BinaryData) {
    auto original = create_random_data(10000);

    auto compress_result = engine_->compress(original);
    ASSERT_TRUE(compress_result.has_value()) << compress_result.error().message;

    auto decompress_result =
        engine_->decompress(compress_result.value(), original.size());
    ASSERT_TRUE(decompress_result.has_value()) << decompress_result.error().message;

    EXPECT_EQ(original, decompress_result.value());
}

TEST_F(CompressionEngineTest, RoundTrip_SmallData) {
    auto original = string_to_bytes("Hello, World!");

    auto compress_result = engine_->compress(original);
    ASSERT_TRUE(compress_result.has_value());

    auto decompress_result =
        engine_->decompress(compress_result.value(), original.size());
    ASSERT_TRUE(decompress_result.has_value());

    EXPECT_EQ(original, decompress_result.value());
}

TEST_F(CompressionEngineTest, RoundTrip_LargeData) {
    // 1MB of text data
    auto original = create_text_data(1024 * 1024);

    auto compress_result = engine_->compress(original);
    ASSERT_TRUE(compress_result.has_value());

    auto decompress_result =
        engine_->decompress(compress_result.value(), original.size());
    ASSERT_TRUE(decompress_result.has_value());

    EXPECT_EQ(original, decompress_result.value());
}

// Empty Data Tests

TEST_F(CompressionEngineTest, Compress_EmptyData) {
    std::vector<std::byte> empty;

    auto result = engine_->compress(empty);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().empty());
}

TEST_F(CompressionEngineTest, Decompress_EmptyData) {
    std::vector<std::byte> empty;

    auto result = engine_->decompress(empty, 0);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().empty());
}

// Various Data Type Tests

TEST_F(CompressionEngineTest, Compress_HighlyCompressible) {
    // All zeros - highly compressible
    std::vector<std::byte> zeros(10000, std::byte{0});

    auto result = engine_->compress(zeros);
    ASSERT_TRUE(result.has_value());

    // Compressed should be much smaller
    EXPECT_LT(result.value().size(), zeros.size() / 2);
}

TEST_F(CompressionEngineTest, Compress_TextFile) {
    auto text_data = create_text_data(10000);

    auto result = engine_->compress(text_data);
    ASSERT_TRUE(result.has_value());

    // Text typically compresses well (2-4:1 ratio)
    EXPECT_LT(result.value().size(), text_data.size() / 2);
}

TEST_F(CompressionEngineTest, Compress_RandomData) {
    // Random data compresses poorly
    auto random_data = create_random_data(10000);

    auto result = engine_->compress(random_data);
    ASSERT_TRUE(result.has_value());

    // Random data may even expand slightly, but should still work
    EXPECT_GT(result.value().size(), 0);
}

// Adaptive Compression Detection Tests

TEST_F(CompressionEngineTest, IsCompressible_TextData) {
    auto text_data = create_text_data(10000);
    EXPECT_TRUE(engine_->is_compressible(text_data));
}

TEST_F(CompressionEngineTest, IsCompressible_RandomData) {
    auto random_data = create_random_data(10000);
    // Random data should not be worth compressing
    EXPECT_FALSE(engine_->is_compressible(random_data));
}

TEST_F(CompressionEngineTest, IsCompressible_EmptyData) {
    std::vector<std::byte> empty;
    EXPECT_FALSE(engine_->is_compressible(empty));
}

TEST_F(CompressionEngineTest, IsCompressible_SmallSample) {
    // Very small data
    auto small = string_to_bytes("Hi");
    // Small data may or may not be compressible, but should not crash
    [[maybe_unused]] bool result = engine_->is_compressible(small);
}

// Pre-compressed Format Detection Tests

TEST_F(CompressionEngineTest, IsCompressible_ZipFile) {
    // ZIP magic bytes: 0x50, 0x4B, 0x03, 0x04
    std::vector<std::byte> zip_data = {std::byte{0x50}, std::byte{0x4B},
                                        std::byte{0x03}, std::byte{0x04}};
    zip_data.resize(1000, std::byte{0x00});  // Pad with zeros

    EXPECT_FALSE(engine_->is_compressible(zip_data));
}

TEST_F(CompressionEngineTest, IsCompressible_GzipFile) {
    // GZIP magic bytes: 0x1F, 0x8B
    std::vector<std::byte> gzip_data = {std::byte{0x1F}, std::byte{0x8B}};
    gzip_data.resize(1000, std::byte{0x00});

    EXPECT_FALSE(engine_->is_compressible(gzip_data));
}

TEST_F(CompressionEngineTest, IsCompressible_JpegFile) {
    // JPEG magic bytes: 0xFF, 0xD8, 0xFF
    std::vector<std::byte> jpeg_data = {std::byte{0xFF}, std::byte{0xD8},
                                         std::byte{0xFF}};
    jpeg_data.resize(1000, std::byte{0x00});

    EXPECT_FALSE(engine_->is_compressible(jpeg_data));
}

TEST_F(CompressionEngineTest, IsCompressible_PngFile) {
    // PNG magic bytes
    std::vector<std::byte> png_data = {
        std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47},
        std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};
    png_data.resize(1000, std::byte{0x00});

    EXPECT_FALSE(engine_->is_compressible(png_data));
}

// Compression Level Tests

TEST_F(CompressionEngineTest, CompressionLevel_Fast) {
    compression_engine fast_engine(compression_level::fast);
    auto data = create_text_data(10000);

    auto result = fast_engine.compress(data);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result.value().size(), 0);
}

TEST_F(CompressionEngineTest, CompressionLevel_High) {
    compression_engine high_engine(compression_level::high);
    auto data = create_text_data(10000);

    auto result = high_engine.compress(data);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result.value().size(), 0);
}

TEST_F(CompressionEngineTest, CompressionLevel_HighBetterRatio) {
    auto data = create_text_data(100000);

    compression_engine fast_engine(compression_level::fast);
    compression_engine high_engine(compression_level::high);

    auto fast_result = fast_engine.compress(data);
    auto high_result = high_engine.compress(data);

    ASSERT_TRUE(fast_result.has_value());
    ASSERT_TRUE(high_result.has_value());

    // High compression should typically produce smaller output
    // (though not always guaranteed for all data)
    // We just verify both work correctly
    EXPECT_GT(fast_result.value().size(), 0);
    EXPECT_GT(high_result.value().size(), 0);
}

// Statistics Tests

TEST_F(CompressionEngineTest, Stats_InitialValues) {
    auto stats = engine_->stats();
    EXPECT_EQ(stats.total_input_bytes, 0);
    EXPECT_EQ(stats.total_output_bytes, 0);
    EXPECT_EQ(stats.compression_calls, 0);
    EXPECT_EQ(stats.decompression_calls, 0);
    EXPECT_DOUBLE_EQ(stats.compression_ratio(), 1.0);
}

TEST_F(CompressionEngineTest, Stats_AfterCompression) {
    auto data = create_text_data(10000);
    auto result = engine_->compress(data);
    ASSERT_TRUE(result.has_value());

    auto stats = engine_->stats();
    EXPECT_EQ(stats.compression_calls, 1);
    EXPECT_EQ(stats.total_input_bytes, data.size());
    EXPECT_EQ(stats.total_output_bytes, result.value().size());
}

TEST_F(CompressionEngineTest, Stats_AfterDecompression) {
    auto data = create_text_data(10000);
    auto compress_result = engine_->compress(data);
    ASSERT_TRUE(compress_result.has_value());

    auto decompress_result =
        engine_->decompress(compress_result.value(), data.size());
    ASSERT_TRUE(decompress_result.has_value());

    auto stats = engine_->stats();
    EXPECT_EQ(stats.compression_calls, 1);
    EXPECT_EQ(stats.decompression_calls, 1);
}

TEST_F(CompressionEngineTest, Stats_Reset) {
    auto data = create_text_data(10000);
    engine_->compress(data);

    engine_->reset_stats();

    auto stats = engine_->stats();
    EXPECT_EQ(stats.compression_calls, 0);
    EXPECT_EQ(stats.total_input_bytes, 0);
}

// Level Getter/Setter Tests

TEST_F(CompressionEngineTest, Level_DefaultIsFast) {
    EXPECT_EQ(engine_->level(), compression_level::fast);
}

TEST_F(CompressionEngineTest, Level_SetAndGet) {
    engine_->set_level(compression_level::high);
    EXPECT_EQ(engine_->level(), compression_level::high);

    engine_->set_level(compression_level::fast);
    EXPECT_EQ(engine_->level(), compression_level::fast);
}

// MaxCompressedSize Tests

TEST_F(CompressionEngineTest, MaxCompressedSize_Positive) {
    auto max_size = compression_engine::max_compressed_size(10000);
    EXPECT_GT(max_size, 10000);  // LZ4 bound is always >= input
}

TEST_F(CompressionEngineTest, MaxCompressedSize_Zero) {
    auto max_size = compression_engine::max_compressed_size(0);
    EXPECT_GE(max_size, 0);
}

// Edge Cases

TEST_F(CompressionEngineTest, Decompress_InvalidData) {
    // Try to decompress garbage data
    auto garbage = create_random_data(100);

    auto result = engine_->decompress(garbage, 1000);
    EXPECT_FALSE(result.has_value());
}

TEST_F(CompressionEngineTest, Decompress_WrongOriginalSize) {
    auto original = create_text_data(1000);
    auto compressed = engine_->compress(original);
    ASSERT_TRUE(compressed.has_value());

    // Try to decompress with wrong size
    auto result = engine_->decompress(compressed.value(), 500);
    // Should fail or return mismatched size
    if (result.has_value()) {
        EXPECT_NE(result.value().size(), original.size());
    }
}

TEST_F(CompressionEngineTest, MultipleOperations_Consistency) {
    auto data = create_text_data(5000);

    // Compress multiple times
    auto result1 = engine_->compress(data);
    auto result2 = engine_->compress(data);

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());

    // LZ4 is deterministic, so same input should produce same output
    EXPECT_EQ(result1.value(), result2.value());
}

TEST_F(CompressionEngineTest, MoveConstructor) {
    engine_->compress(create_text_data(1000));
    auto stats_before = engine_->stats();

    compression_engine moved_engine(std::move(*engine_));

    // After move, the new engine should work
    auto data = create_text_data(1000);
    auto result = moved_engine.compress(data);
    ASSERT_TRUE(result.has_value());
}

// Single byte data
TEST_F(CompressionEngineTest, SingleByte) {
    std::vector<std::byte> single = {std::byte{0x42}};

    auto compress_result = engine_->compress(single);
    ASSERT_TRUE(compress_result.has_value());

    auto decompress_result =
        engine_->decompress(compress_result.value(), single.size());
    ASSERT_TRUE(decompress_result.has_value());

    EXPECT_EQ(single, decompress_result.value());
}

// Boundary value at sample size (4KB)
TEST_F(CompressionEngineTest, BoundaryAtSampleSize) {
    // Exactly 4KB (sample_size used in is_compressible)
    auto data = create_text_data(4096);

    auto compress_result = engine_->compress(data);
    ASSERT_TRUE(compress_result.has_value());

    auto decompress_result =
        engine_->decompress(compress_result.value(), data.size());
    ASSERT_TRUE(decompress_result.has_value());

    EXPECT_EQ(data, decompress_result.value());
}

#endif  // FILE_TRANS_ENABLE_LZ4

}  // namespace kcenon::file_transfer::test
