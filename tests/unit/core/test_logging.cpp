/**
 * @file test_logging.cpp
 * @brief Unit tests for structured logging and sensitive information masking
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/core/logging.h>

#include <string>
#include <vector>
#include <regex>

namespace kcenon::file_transfer::test {

// =============================================================================
// Masking Config Tests
// =============================================================================

class MaskingConfigTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MaskingConfigTest, DefaultConfig) {
    masking_config config;

    EXPECT_FALSE(config.mask_paths);
    EXPECT_FALSE(config.mask_ips);
    EXPECT_FALSE(config.mask_filenames);
    EXPECT_EQ(config.mask_char, "*");
    EXPECT_EQ(config.visible_chars, 4u);
}

TEST_F(MaskingConfigTest, AllMaskedConfig) {
    auto config = masking_config::all_masked();

    EXPECT_TRUE(config.mask_paths);
    EXPECT_TRUE(config.mask_ips);
    EXPECT_TRUE(config.mask_filenames);
}

TEST_F(MaskingConfigTest, NoneConfig) {
    auto config = masking_config::none();

    EXPECT_FALSE(config.mask_paths);
    EXPECT_FALSE(config.mask_ips);
    EXPECT_FALSE(config.mask_filenames);
}

// =============================================================================
// Sensitive Info Masker Tests
// =============================================================================

class SensitiveInfoMaskerTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(SensitiveInfoMaskerTest, NoMaskingByDefault) {
    sensitive_info_masker masker;
    std::string input = "File at /home/user/secret.txt from 192.168.1.100";

    auto result = masker.mask(input);

    EXPECT_EQ(result, input);
}

TEST_F(SensitiveInfoMaskerTest, MaskIPAddresses) {
    masking_config config;
    config.mask_ips = true;
    sensitive_info_masker masker(config);

    auto result = masker.mask_ip("192.168.1.100");

    // Should mask all but the last octet (192.168.1 = 9 chars -> *********)
    EXPECT_EQ(result, "*********.100");
}

TEST_F(SensitiveInfoMaskerTest, MaskIPAddressesInText) {
    masking_config config;
    config.mask_ips = true;
    sensitive_info_masker masker(config);

    auto result = masker.mask("Connection from 192.168.1.100 to 10.0.0.1");

    // 192.168.1 = 9 chars -> *********, 10.0.0 = 6 chars -> ******
    EXPECT_NE(result.find("*********.100"), std::string::npos);
    EXPECT_NE(result.find("******.1"), std::string::npos);
}

TEST_F(SensitiveInfoMaskerTest, MaskFilePath) {
    masking_config config;
    config.mask_paths = true;
    sensitive_info_masker masker(config);

    auto result = masker.mask_path("/home/user/documents/secret.txt");

    // Directory should be masked, filename preserved
    EXPECT_NE(result.find("secret.txt"), std::string::npos);
    EXPECT_EQ(result.find("/home/"), std::string::npos);
}

TEST_F(SensitiveInfoMaskerTest, MaskFilePathWithFilename) {
    masking_config config;
    config.mask_paths = true;
    config.mask_filenames = true;
    config.visible_chars = 4;
    sensitive_info_masker masker(config);

    auto result = masker.mask_path("/home/user/documents/secretfile.txt");

    // First 4 chars visible, rest masked, extension preserved
    EXPECT_NE(result.find("secr"), std::string::npos);
    EXPECT_NE(result.find(".txt"), std::string::npos);
}

TEST_F(SensitiveInfoMaskerTest, MaskPathsInText) {
    masking_config config;
    config.mask_paths = true;
    sensitive_info_masker masker(config);

    auto result = masker.mask("File saved to /home/user/data.zip successfully");

    EXPECT_EQ(result.find("/home/user/"), std::string::npos);
    EXPECT_NE(result.find("data.zip"), std::string::npos);
}

TEST_F(SensitiveInfoMaskerTest, EmptyInput) {
    masking_config config = masking_config::all_masked();
    sensitive_info_masker masker(config);

    EXPECT_EQ(masker.mask(""), "");
    EXPECT_EQ(masker.mask_path(""), "");
    EXPECT_EQ(masker.mask_ip(""), "");
}

TEST_F(SensitiveInfoMaskerTest, UpdateConfig) {
    sensitive_info_masker masker;
    std::string ip = "192.168.1.100";

    EXPECT_EQ(masker.mask_ip(ip), ip);

    masking_config config;
    config.mask_ips = true;
    masker.set_config(config);

    EXPECT_NE(masker.mask_ip(ip), ip);
}

// =============================================================================
// Transfer Log Context Tests
// =============================================================================

class TransferLogContextTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(TransferLogContextTest, EmptyContextToJson) {
    transfer_log_context ctx;
    auto json = ctx.to_json();

    EXPECT_EQ(json, "{}");
}

TEST_F(TransferLogContextTest, BasicFieldsToJson) {
    transfer_log_context ctx;
    ctx.transfer_id = "abc-123";
    ctx.filename = "test.zip";
    ctx.file_size = 1024;

    auto json = ctx.to_json();

    EXPECT_NE(json.find("\"transfer_id\":\"abc-123\""), std::string::npos);
    EXPECT_NE(json.find("\"filename\":\"test.zip\""), std::string::npos);
    EXPECT_NE(json.find("\"size\":1024"), std::string::npos);
}

TEST_F(TransferLogContextTest, AllFieldsToJson) {
    transfer_log_context ctx;
    ctx.transfer_id = "transfer-001";
    ctx.filename = "data.zip";
    ctx.file_size = 1048576;
    ctx.bytes_transferred = 524288;
    ctx.chunk_index = 5;
    ctx.total_chunks = 10;
    ctx.progress_percent = 50.0;
    ctx.rate_mbps = 2.5;
    ctx.duration_ms = 1000;
    ctx.error_message = "Test error";
    ctx.client_id = "client-abc";
    ctx.server_address = "192.168.1.100";

    auto json = ctx.to_json();

    EXPECT_NE(json.find("\"transfer_id\":\"transfer-001\""), std::string::npos);
    EXPECT_NE(json.find("\"filename\":\"data.zip\""), std::string::npos);
    EXPECT_NE(json.find("\"size\":1048576"), std::string::npos);
    EXPECT_NE(json.find("\"bytes_transferred\":524288"), std::string::npos);
    EXPECT_NE(json.find("\"chunk_index\":5"), std::string::npos);
    EXPECT_NE(json.find("\"total_chunks\":10"), std::string::npos);
    EXPECT_NE(json.find("\"progress_percent\":50.00"), std::string::npos);
    EXPECT_NE(json.find("\"rate_mbps\":2.50"), std::string::npos);
    EXPECT_NE(json.find("\"duration_ms\":1000"), std::string::npos);
    EXPECT_NE(json.find("\"error_message\":\"Test error\""), std::string::npos);
    EXPECT_NE(json.find("\"client_id\":\"client-abc\""), std::string::npos);
    EXPECT_NE(json.find("\"server_address\":\"192.168.1.100\""), std::string::npos);
}

TEST_F(TransferLogContextTest, JsonWithMasking) {
    transfer_log_context ctx;
    ctx.server_address = "192.168.1.100";
    ctx.error_message = "Error accessing /home/user/file.txt";

    masking_config config;
    config.mask_ips = true;
    config.mask_paths = true;
    sensitive_info_masker masker(config);

    auto json = ctx.to_json_with_masking(&masker);

    // IP should be masked
    EXPECT_EQ(json.find("192.168.1.100"), std::string::npos);
    EXPECT_NE(json.find(".100"), std::string::npos);

    // Path in error message should be masked
    EXPECT_EQ(json.find("/home/user/"), std::string::npos);
}

TEST_F(TransferLogContextTest, JsonEscaping) {
    transfer_log_context ctx;
    ctx.transfer_id = "id-with-\"quotes\"";
    ctx.error_message = "Error:\nLine break\tand\ttabs";

    auto json = ctx.to_json();

    EXPECT_NE(json.find("\\\""), std::string::npos);
    EXPECT_NE(json.find("\\n"), std::string::npos);
    EXPECT_NE(json.find("\\t"), std::string::npos);
}

// =============================================================================
// Structured Log Entry Tests
// =============================================================================

class StructuredLogEntryTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(StructuredLogEntryTest, BasicEntryToJson) {
    structured_log_entry entry;
    entry.timestamp = "2025-12-11T10:30:00.000Z";
    entry.level = log_level::info;
    entry.category = "file_transfer.client";
    entry.message = "Upload completed";

    auto json = entry.to_json();

    EXPECT_NE(json.find("\"timestamp\":\"2025-12-11T10:30:00.000Z\""), std::string::npos);
    EXPECT_NE(json.find("\"level\":\"INFO\""), std::string::npos);
    EXPECT_NE(json.find("\"category\":\"file_transfer.client\""), std::string::npos);
    EXPECT_NE(json.find("\"message\":\"Upload completed\""), std::string::npos);
}

TEST_F(StructuredLogEntryTest, EntryWithContext) {
    structured_log_entry entry;
    entry.timestamp = "2025-12-11T10:30:00.000Z";
    entry.level = log_level::info;
    entry.category = "file_transfer.client";
    entry.message = "Upload completed";

    transfer_log_context ctx;
    ctx.transfer_id = "abc-123";
    ctx.filename = "data.zip";
    ctx.file_size = 1048576;
    entry.context = ctx;

    auto json = entry.to_json();

    EXPECT_NE(json.find("\"transfer_id\":\"abc-123\""), std::string::npos);
    EXPECT_NE(json.find("\"filename\":\"data.zip\""), std::string::npos);
    EXPECT_NE(json.find("\"size\":1048576"), std::string::npos);
}

TEST_F(StructuredLogEntryTest, EntryWithSourceLocation) {
    structured_log_entry entry;
    entry.timestamp = "2025-12-11T10:30:00.000Z";
    entry.level = log_level::error;
    entry.category = "file_transfer.server";
    entry.message = "Connection failed";
    entry.source_file = "/src/server.cpp";
    entry.source_line = 42;
    entry.function_name = "handle_connection";

    auto json = entry.to_json();

    EXPECT_NE(json.find("\"source\":{"), std::string::npos);
    EXPECT_NE(json.find("\"file\":\"/src/server.cpp\""), std::string::npos);
    EXPECT_NE(json.find("\"line\":42"), std::string::npos);
    EXPECT_NE(json.find("\"function\":\"handle_connection\""), std::string::npos);
}

TEST_F(StructuredLogEntryTest, EntryWithMasking) {
    structured_log_entry entry;
    entry.timestamp = "2025-12-11T10:30:00.000Z";
    entry.level = log_level::error;
    entry.category = "file_transfer.server";
    entry.message = "Connection from 192.168.1.100 failed";
    entry.source_file = "/home/user/src/server.cpp";

    masking_config config;
    config.mask_ips = true;
    config.mask_paths = true;
    sensitive_info_masker masker(config);

    auto json = entry.to_json_with_masking(&masker);

    // IP in message should be masked
    EXPECT_EQ(json.find("192.168.1.100"), std::string::npos);

    // Path in source should be masked
    EXPECT_EQ(json.find("/home/user/"), std::string::npos);
}

// =============================================================================
// Log Entry Builder Tests
// =============================================================================

class LogEntryBuilderTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(LogEntryBuilderTest, BasicBuilder) {
    auto entry = log_entry_builder()
        .with_level(log_level::info)
        .with_category(log_category::client)
        .with_message("Upload started")
        .build();

    EXPECT_EQ(entry.level, log_level::info);
    EXPECT_EQ(entry.category, log_category::client);
    EXPECT_EQ(entry.message, "Upload started");
    EXPECT_FALSE(entry.timestamp.empty());
}

TEST_F(LogEntryBuilderTest, BuilderWithAllContextFields) {
    auto entry = log_entry_builder()
        .with_level(log_level::info)
        .with_category(log_category::client)
        .with_message("Upload completed")
        .with_transfer_id("abc-123")
        .with_filename("data.zip")
        .with_file_size(1048576)
        .with_bytes_transferred(1048576)
        .with_duration_ms(500)
        .with_rate_mbps(2.0)
        .with_progress_percent(100.0)
        .with_chunk_index(10)
        .with_total_chunks(10)
        .with_client_id("client-001")
        .with_server_address("192.168.1.1")
        .build();

    ASSERT_TRUE(entry.context.has_value());
    EXPECT_EQ(entry.context->transfer_id, "abc-123");
    EXPECT_EQ(entry.context->filename, "data.zip");
    EXPECT_EQ(entry.context->file_size.value(), 1048576u);
    EXPECT_EQ(entry.context->bytes_transferred.value(), 1048576u);
    EXPECT_EQ(entry.context->duration_ms.value(), 500u);
    EXPECT_DOUBLE_EQ(entry.context->rate_mbps.value(), 2.0);
    EXPECT_DOUBLE_EQ(entry.context->progress_percent.value(), 100.0);
    EXPECT_EQ(entry.context->chunk_index.value(), 10u);
    EXPECT_EQ(entry.context->total_chunks.value(), 10u);
    EXPECT_EQ(entry.context->client_id.value(), "client-001");
    EXPECT_EQ(entry.context->server_address.value(), "192.168.1.1");
}

TEST_F(LogEntryBuilderTest, BuilderWithSourceLocation) {
    auto entry = log_entry_builder()
        .with_level(log_level::error)
        .with_category(log_category::server)
        .with_message("Error occurred")
        .with_source_location("test.cpp", 100, "test_func")
        .build();

    EXPECT_EQ(entry.source_file.value(), "test.cpp");
    EXPECT_EQ(entry.source_line.value(), 100);
    EXPECT_EQ(entry.function_name.value(), "test_func");
}

TEST_F(LogEntryBuilderTest, BuilderWithExistingContext) {
    transfer_log_context ctx;
    ctx.transfer_id = "existing-id";
    ctx.filename = "existing.zip";

    auto entry = log_entry_builder()
        .with_level(log_level::info)
        .with_category(log_category::transfer)
        .with_message("Transfer started")
        .with_context(ctx)
        .build();

    ASSERT_TRUE(entry.context.has_value());
    EXPECT_EQ(entry.context->transfer_id, "existing-id");
    EXPECT_EQ(entry.context->filename, "existing.zip");
}

TEST_F(LogEntryBuilderTest, BuilderWithErrorMessage) {
    auto entry = log_entry_builder()
        .with_level(log_level::error)
        .with_category(log_category::client)
        .with_message("Upload failed")
        .with_transfer_id("failed-transfer")
        .with_error_message("Connection timeout")
        .build();

    ASSERT_TRUE(entry.context.has_value());
    EXPECT_EQ(entry.context->error_message.value(), "Connection timeout");
}

TEST_F(LogEntryBuilderTest, BuildJson) {
    auto json = log_entry_builder()
        .with_level(log_level::info)
        .with_category(log_category::client)
        .with_message("Test message")
        .build_json();

    EXPECT_NE(json.find("\"level\":\"INFO\""), std::string::npos);
    EXPECT_NE(json.find("\"category\":\"file_transfer.client\""), std::string::npos);
    EXPECT_NE(json.find("\"message\":\"Test message\""), std::string::npos);
}

TEST_F(LogEntryBuilderTest, BuildJsonMasked) {
    masking_config config;
    config.mask_ips = true;
    sensitive_info_masker masker(config);

    auto json = log_entry_builder()
        .with_level(log_level::info)
        .with_category(log_category::client)
        .with_message("Connected to 192.168.1.100")
        .with_server_address("192.168.1.100")
        .build_json_masked(masker);

    EXPECT_EQ(json.find("192.168.1.100"), std::string::npos);
}

TEST_F(LogEntryBuilderTest, TimestampFormat) {
    auto entry = log_entry_builder()
        .with_level(log_level::info)
        .with_category(log_category::client)
        .with_message("Test")
        .build();

    // ISO 8601 format: YYYY-MM-DDTHH:MM:SS.mmmZ
    std::regex iso8601_regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z)");
    EXPECT_TRUE(std::regex_match(entry.timestamp, iso8601_regex));
}

// =============================================================================
// Log Level Tests
// =============================================================================

class LogLevelTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(LogLevelTest, LogLevelToString) {
    EXPECT_EQ(log_level_to_string(log_level::trace), "TRACE");
    EXPECT_EQ(log_level_to_string(log_level::debug), "DEBUG");
    EXPECT_EQ(log_level_to_string(log_level::info), "INFO");
    EXPECT_EQ(log_level_to_string(log_level::warn), "WARN");
    EXPECT_EQ(log_level_to_string(log_level::error), "ERROR");
    EXPECT_EQ(log_level_to_string(log_level::fatal), "FATAL");
}

// =============================================================================
// Logger Integration Tests
// =============================================================================

class FileTransferLoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        get_logger().initialize();
        get_logger().set_level(log_level::trace);
        get_logger().enable_json_output(false);
        get_logger().enable_masking(false);
    }

    void TearDown() override {
        get_logger().set_callback(nullptr);
        get_logger().set_json_callback(nullptr);
    }
};

TEST_F(FileTransferLoggerTest, SetOutputFormat) {
    get_logger().set_output_format(log_output_format::json);
    EXPECT_EQ(get_logger().get_output_format(), log_output_format::json);

    get_logger().set_output_format(log_output_format::text);
    EXPECT_EQ(get_logger().get_output_format(), log_output_format::text);
}

TEST_F(FileTransferLoggerTest, EnableJsonOutput) {
    get_logger().enable_json_output(true);
    EXPECT_TRUE(get_logger().is_json_output_enabled());

    get_logger().enable_json_output(false);
    EXPECT_FALSE(get_logger().is_json_output_enabled());
}

TEST_F(FileTransferLoggerTest, SetMaskingConfig) {
    auto config = masking_config::all_masked();
    get_logger().set_masking_config(config);

    auto retrieved = get_logger().get_masking_config();
    EXPECT_TRUE(retrieved.mask_paths);
    EXPECT_TRUE(retrieved.mask_ips);
    EXPECT_TRUE(retrieved.mask_filenames);
}

TEST_F(FileTransferLoggerTest, EnableMasking) {
    get_logger().enable_masking(true);
    auto config = get_logger().get_masking_config();
    EXPECT_TRUE(config.mask_paths);
    EXPECT_TRUE(config.mask_ips);
    EXPECT_TRUE(config.mask_filenames);

    get_logger().enable_masking(false);
    config = get_logger().get_masking_config();
    EXPECT_FALSE(config.mask_paths);
    EXPECT_FALSE(config.mask_ips);
    EXPECT_FALSE(config.mask_filenames);
}

TEST_F(FileTransferLoggerTest, LogCallback) {
    std::vector<std::tuple<log_level, std::string, std::string>> captured;

    get_logger().set_callback([&](log_level level, std::string_view category,
                                  std::string_view message, const transfer_log_context*) {
        captured.emplace_back(level, std::string(category), std::string(message));
    });

    FT_LOG_INFO(log_category::client, "Test message");

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(std::get<0>(captured[0]), log_level::info);
    EXPECT_EQ(std::get<1>(captured[0]), log_category::client);
    EXPECT_EQ(std::get<2>(captured[0]), "Test message");
}

TEST_F(FileTransferLoggerTest, JsonCallback) {
    std::vector<std::string> captured_json;

    get_logger().enable_json_output(true);
    get_logger().set_json_callback([&](const structured_log_entry&, const std::string& json) {
        captured_json.push_back(json);
    });

    FT_LOG_INFO(log_category::client, "JSON test");

    ASSERT_EQ(captured_json.size(), 1u);
    EXPECT_NE(captured_json[0].find("\"level\":\"INFO\""), std::string::npos);
    EXPECT_NE(captured_json[0].find("\"message\":\"JSON test\""), std::string::npos);
}

TEST_F(FileTransferLoggerTest, LogStructuredEntry) {
    std::vector<std::string> captured_json;

    get_logger().set_json_callback([&](const structured_log_entry&, const std::string& json) {
        captured_json.push_back(json);
    });

    auto entry = log_entry_builder()
        .with_level(log_level::info)
        .with_category(log_category::client)
        .with_message("Structured entry test")
        .with_transfer_id("test-id")
        .build();

    get_logger().log(entry);

    ASSERT_EQ(captured_json.size(), 1u);
    EXPECT_NE(captured_json[0].find("\"transfer_id\":\"test-id\""), std::string::npos);
}

TEST_F(FileTransferLoggerTest, LogLevelFiltering) {
    std::vector<std::string> captured;

    get_logger().set_callback([&](log_level, std::string_view,
                                  std::string_view message, const transfer_log_context*) {
        captured.push_back(std::string(message));
    });

    get_logger().set_level(log_level::warn);

    FT_LOG_DEBUG(log_category::client, "Debug message");
    FT_LOG_INFO(log_category::client, "Info message");
    FT_LOG_WARN(log_category::client, "Warn message");
    FT_LOG_ERROR(log_category::client, "Error message");

    EXPECT_EQ(captured.size(), 2u);
    EXPECT_EQ(captured[0], "Warn message");
    EXPECT_EQ(captured[1], "Error message");
}

TEST_F(FileTransferLoggerTest, MaskingInJsonOutput) {
    std::vector<std::string> captured_json;

    get_logger().enable_json_output(true);
    get_logger().enable_masking(true);
    get_logger().set_json_callback([&](const structured_log_entry&, const std::string& json) {
        captured_json.push_back(json);
    });

    transfer_log_context ctx;
    ctx.server_address = "192.168.1.100";
    FT_LOG_INFO_CTX(log_category::client, "Connected to server", ctx);

    ASSERT_EQ(captured_json.size(), 1u);
    EXPECT_EQ(captured_json[0].find("192.168.1.100"), std::string::npos);
    EXPECT_NE(captured_json[0].find(".100"), std::string::npos);
}

} // namespace kcenon::file_transfer::test
