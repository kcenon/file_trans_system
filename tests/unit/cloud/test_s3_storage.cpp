/**
 * @file test_s3_storage.cpp
 * @brief Unit tests for AWS S3 storage backend
 */

#include <gtest/gtest.h>

#include "kcenon/file_transfer/cloud/s3_storage.h"

namespace kcenon::file_transfer {
namespace {

// ============================================================================
// S3 Credential Provider Tests
// ============================================================================

class S3CredentialProviderTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(S3CredentialProviderTest, CreateFromStaticCredentials) {
    static_credentials creds;
    creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
    creds.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";

    auto provider = s3_credential_provider::create(creds);
    ASSERT_NE(provider, nullptr);

    EXPECT_EQ(provider->provider(), cloud_provider::aws_s3);
    EXPECT_EQ(provider->state(), credential_state::valid);
    EXPECT_FALSE(provider->needs_refresh());

    auto retrieved = provider->get_credentials();
    ASSERT_NE(retrieved, nullptr);
}

TEST_F(S3CredentialProviderTest, CreateFromEmptyCredentialsFails) {
    static_credentials creds;
    // Empty credentials

    auto provider = s3_credential_provider::create(creds);
    EXPECT_EQ(provider, nullptr);
}

TEST_F(S3CredentialProviderTest, CreateFromMissingSecretFails) {
    static_credentials creds;
    creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
    // Missing secret key

    auto provider = s3_credential_provider::create(creds);
    EXPECT_EQ(provider, nullptr);
}

TEST_F(S3CredentialProviderTest, RefreshStaticCredentials) {
    static_credentials creds;
    creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
    creds.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";

    auto provider = s3_credential_provider::create(creds);
    ASSERT_NE(provider, nullptr);

    // Refresh should succeed (no-op for static credentials)
    EXPECT_TRUE(provider->refresh());
    EXPECT_EQ(provider->state(), credential_state::valid);
}

// ============================================================================
// S3 Storage Creation Tests
// ============================================================================

class S3StorageCreationTest : public ::testing::Test {
protected:
    void SetUp() override {
        creds_.access_key_id = "AKIAIOSFODNN7EXAMPLE";
        creds_.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
        provider_ = s3_credential_provider::create(creds_);
    }

    static_credentials creds_;
    std::shared_ptr<credential_provider> provider_;
};

TEST_F(S3StorageCreationTest, CreateWithValidConfig) {
    auto config = cloud_config_builder::s3()
        .with_bucket("my-test-bucket")
        .with_region("us-east-1")
        .build_s3();

    auto storage = s3_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    EXPECT_EQ(storage->provider(), cloud_provider::aws_s3);
    EXPECT_EQ(storage->provider_name(), "aws-s3");
    EXPECT_EQ(storage->bucket(), "my-test-bucket");
    EXPECT_EQ(storage->region(), "us-east-1");
    EXPECT_EQ(storage->state(), cloud_storage_state::disconnected);
    EXPECT_FALSE(storage->is_connected());
}

TEST_F(S3StorageCreationTest, CreateWithCustomEndpoint) {
    auto config = cloud_config_builder::s3()
        .with_bucket("my-bucket")
        .with_region("us-east-1")
        .with_endpoint("http://localhost:9000")
        .with_path_style(true)
        .build_s3();

    auto storage = s3_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    EXPECT_EQ(storage->endpoint_url(), "http://localhost:9000");
}

TEST_F(S3StorageCreationTest, CreateWithTransferAcceleration) {
    auto config = cloud_config_builder::s3()
        .with_bucket("my-bucket")
        .with_region("us-east-1")
        .with_transfer_acceleration(true)
        .build_s3();

    auto storage = s3_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    EXPECT_TRUE(storage->is_transfer_acceleration_enabled());
}

TEST_F(S3StorageCreationTest, CreateWithEmptyBucketFails) {
    auto config = cloud_config_builder::s3()
        .with_region("us-east-1")
        .build_s3();
    // No bucket set

    auto storage = s3_storage::create(config, provider_);
    EXPECT_EQ(storage, nullptr);
}

TEST_F(S3StorageCreationTest, CreateWithEmptyRegionAndNoEndpointFails) {
    auto config = cloud_config_builder::s3()
        .with_bucket("my-bucket")
        .build_s3();
    // No region or endpoint set

    auto storage = s3_storage::create(config, provider_);
    EXPECT_EQ(storage, nullptr);
}

TEST_F(S3StorageCreationTest, CreateWithNullCredentialsFails) {
    auto config = cloud_config_builder::s3()
        .with_bucket("my-bucket")
        .with_region("us-east-1")
        .build_s3();

    auto storage = s3_storage::create(config, nullptr);
    EXPECT_EQ(storage, nullptr);
}

// ============================================================================
// S3 Storage Connection Tests
// ============================================================================

class S3StorageConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        static_credentials creds;
        creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
        creds.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
        provider_ = s3_credential_provider::create(creds);

        auto config = cloud_config_builder::s3()
            .with_bucket("test-bucket")
            .with_region("us-east-1")
            .build_s3();

        storage_ = s3_storage::create(config, provider_);
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<s3_storage> storage_;
};

TEST_F(S3StorageConnectionTest, Connect) {
    ASSERT_NE(storage_, nullptr);
    EXPECT_EQ(storage_->state(), cloud_storage_state::disconnected);

    auto result = storage_->connect();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(storage_->state(), cloud_storage_state::connected);
    EXPECT_TRUE(storage_->is_connected());
}

TEST_F(S3StorageConnectionTest, Disconnect) {
    ASSERT_NE(storage_, nullptr);

    auto connect_result = storage_->connect();
    EXPECT_TRUE(connect_result.has_value());

    auto disconnect_result = storage_->disconnect();
    EXPECT_TRUE(disconnect_result.has_value());
    EXPECT_EQ(storage_->state(), cloud_storage_state::disconnected);
    EXPECT_FALSE(storage_->is_connected());
}

TEST_F(S3StorageConnectionTest, StateChangedCallback) {
    ASSERT_NE(storage_, nullptr);

    std::vector<cloud_storage_state> states;
    storage_->on_state_changed([&states](cloud_storage_state state) {
        states.push_back(state);
    });

    storage_->connect();
    storage_->disconnect();

    ASSERT_GE(states.size(), 2);
    EXPECT_EQ(states[0], cloud_storage_state::connecting);
    EXPECT_EQ(states[1], cloud_storage_state::connected);
}

// ============================================================================
// S3 Storage Upload Tests
// ============================================================================

class S3StorageUploadTest : public ::testing::Test {
protected:
    void SetUp() override {
        static_credentials creds;
        creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
        creds.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
        provider_ = s3_credential_provider::create(creds);

        auto config = cloud_config_builder::s3()
            .with_bucket("test-bucket")
            .with_region("us-east-1")
            .build_s3();

        storage_ = s3_storage::create(config, provider_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<s3_storage> storage_;
};

TEST_F(S3StorageUploadTest, UploadSmallData) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    std::vector<std::byte> data(1024);
    std::fill(data.begin(), data.end(), std::byte{0x42});

    auto result = storage_->upload("test/file.bin", data);
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_EQ(result.value().key, "test/file.bin");
        EXPECT_EQ(result.value().bytes_uploaded, 1024);
        EXPECT_FALSE(result.value().etag.empty());
    }
}

TEST_F(S3StorageUploadTest, UploadWithOptions) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    std::vector<std::byte> data(512);
    std::fill(data.begin(), data.end(), std::byte{0x01});

    cloud_transfer_options options;
    options.content_type = "application/octet-stream";
    options.storage_class = "STANDARD";

    auto result = storage_->upload("test/data.bin", data, options);
    EXPECT_TRUE(result.has_value());
}

TEST_F(S3StorageUploadTest, UploadNotConnectedFails) {
    ASSERT_NE(storage_, nullptr);
    storage_->disconnect();

    std::vector<std::byte> data(100);
    auto result = storage_->upload("test/file.bin", data);
    EXPECT_FALSE(result.has_value());
}

TEST_F(S3StorageUploadTest, UploadAsync) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    std::vector<std::byte> data(256);
    std::fill(data.begin(), data.end(), std::byte{0xAB});

    auto future = storage_->upload_async("async/file.bin", data);
    auto result = future.get();

    EXPECT_TRUE(result.has_value());
    if (result.has_value()) {
        EXPECT_EQ(result.value().key, "async/file.bin");
    }
}

// ============================================================================
// S3 Storage Statistics Tests
// ============================================================================

class S3StorageStatisticsTest : public ::testing::Test {
protected:
    void SetUp() override {
        static_credentials creds;
        creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
        creds.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
        provider_ = s3_credential_provider::create(creds);

        auto config = cloud_config_builder::s3()
            .with_bucket("test-bucket")
            .with_region("us-east-1")
            .build_s3();

        storage_ = s3_storage::create(config, provider_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<s3_storage> storage_;
};

TEST_F(S3StorageStatisticsTest, InitialStatistics) {
    ASSERT_NE(storage_, nullptr);

    auto stats = storage_->get_statistics();
    EXPECT_EQ(stats.bytes_uploaded, 0);
    EXPECT_EQ(stats.bytes_downloaded, 0);
    EXPECT_EQ(stats.upload_count, 0);
    EXPECT_EQ(stats.download_count, 0);
    EXPECT_EQ(stats.list_count, 0);
    EXPECT_EQ(stats.delete_count, 0);
    EXPECT_EQ(stats.errors, 0);
}

TEST_F(S3StorageStatisticsTest, StatisticsAfterUpload) {
    ASSERT_NE(storage_, nullptr);

    std::vector<std::byte> data(1024);
    storage_->upload("test/file.bin", data);

    auto stats = storage_->get_statistics();
    EXPECT_EQ(stats.bytes_uploaded, 1024);
    EXPECT_EQ(stats.upload_count, 1);
}

TEST_F(S3StorageStatisticsTest, ResetStatistics) {
    ASSERT_NE(storage_, nullptr);

    std::vector<std::byte> data(512);
    storage_->upload("test/file.bin", data);

    storage_->reset_statistics();

    auto stats = storage_->get_statistics();
    EXPECT_EQ(stats.bytes_uploaded, 0);
    EXPECT_EQ(stats.upload_count, 0);
}

// ============================================================================
// S3 Upload Stream Tests
// ============================================================================

class S3UploadStreamTest : public ::testing::Test {
protected:
    void SetUp() override {
        static_credentials creds;
        creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
        creds.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
        provider_ = s3_credential_provider::create(creds);

        auto config = cloud_config_builder::s3()
            .with_bucket("test-bucket")
            .with_region("us-east-1")
            .build_s3();

        storage_ = s3_storage::create(config, provider_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<s3_storage> storage_;
};

TEST_F(S3UploadStreamTest, CreateUploadStream) {
    ASSERT_NE(storage_, nullptr);

    auto stream = storage_->create_upload_stream("stream/file.bin");
    ASSERT_NE(stream, nullptr);

    EXPECT_TRUE(stream->upload_id().has_value());
    EXPECT_EQ(stream->bytes_written(), 0);
}

TEST_F(S3UploadStreamTest, WriteToStream) {
    ASSERT_NE(storage_, nullptr);

    auto stream = storage_->create_upload_stream("stream/file.bin");
    ASSERT_NE(stream, nullptr);

    std::vector<std::byte> data(1024);
    std::fill(data.begin(), data.end(), std::byte{0x55});

    auto result = stream->write(data);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1024);
    EXPECT_EQ(stream->bytes_written(), 1024);
}

TEST_F(S3UploadStreamTest, FinalizeStream) {
    ASSERT_NE(storage_, nullptr);

    auto stream = storage_->create_upload_stream("stream/file.bin");
    ASSERT_NE(stream, nullptr);

    std::vector<std::byte> data(512);
    stream->write(data);

    auto result = stream->finalize();
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_EQ(result.value().key, "stream/file.bin");
        EXPECT_EQ(result.value().bytes_uploaded, 512);
    }
}

TEST_F(S3UploadStreamTest, AbortStream) {
    ASSERT_NE(storage_, nullptr);

    auto stream = storage_->create_upload_stream("stream/file.bin");
    ASSERT_NE(stream, nullptr);

    std::vector<std::byte> data(512);
    stream->write(data);

    auto result = stream->abort();
    EXPECT_TRUE(result.has_value());
}

// ============================================================================
// S3 Presigned URL Tests
// ============================================================================

#ifdef FILE_TRANS_ENABLE_ENCRYPTION

class S3PresignedUrlTest : public ::testing::Test {
protected:
    void SetUp() override {
        static_credentials creds;
        creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
        creds.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
        provider_ = s3_credential_provider::create(creds);

        auto config = cloud_config_builder::s3()
            .with_bucket("test-bucket")
            .with_region("us-east-1")
            .build_s3();

        storage_ = s3_storage::create(config, provider_);
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<s3_storage> storage_;
};

TEST_F(S3PresignedUrlTest, GenerateGetUrl) {
    ASSERT_NE(storage_, nullptr);

    presigned_url_options options;
    options.method = "GET";
    options.expiration = std::chrono::seconds{3600};

    auto result = storage_->generate_presigned_url("test/file.bin", options);
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        const auto& url = result.value();
        EXPECT_TRUE(url.find("X-Amz-Algorithm=AWS4-HMAC-SHA256") != std::string::npos);
        EXPECT_TRUE(url.find("X-Amz-Credential=") != std::string::npos);
        EXPECT_TRUE(url.find("X-Amz-Signature=") != std::string::npos);
        EXPECT_TRUE(url.find("test/file.bin") != std::string::npos);
    }
}

TEST_F(S3PresignedUrlTest, GeneratePutUrl) {
    ASSERT_NE(storage_, nullptr);

    presigned_url_options options;
    options.method = "PUT";
    options.expiration = std::chrono::seconds{300};
    options.content_type = "application/octet-stream";

    auto result = storage_->generate_presigned_url("upload/file.bin", options);
    EXPECT_TRUE(result.has_value());
}

#endif  // FILE_TRANS_ENABLE_ENCRYPTION

// ============================================================================
// S3 Configuration Tests
// ============================================================================

class S3ConfigurationTest : public ::testing::Test {
protected:
    void SetUp() override {
        static_credentials creds;
        creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
        creds.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
        provider_ = s3_credential_provider::create(creds);
    }

    std::shared_ptr<credential_provider> provider_;
};

TEST_F(S3ConfigurationTest, GetConfiguration) {
    auto config = cloud_config_builder::s3()
        .with_bucket("my-bucket")
        .with_region("eu-west-1")
        .with_connect_timeout(std::chrono::milliseconds{5000})
        .with_connection_pool_size(10)
        .build_s3();

    auto storage = s3_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    const auto& stored_config = storage->config();
    EXPECT_EQ(stored_config.bucket, "my-bucket");
    EXPECT_EQ(stored_config.region, "eu-west-1");
    EXPECT_EQ(stored_config.connect_timeout, std::chrono::milliseconds{5000});
    EXPECT_EQ(stored_config.connection_pool_size, 10);
}

TEST_F(S3ConfigurationTest, GetS3SpecificConfiguration) {
    auto config = cloud_config_builder::s3()
        .with_bucket("my-bucket")
        .with_region("us-west-2")
        .with_transfer_acceleration(true)
        .with_dualstack(true)
        .build_s3();

    auto storage = s3_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    const auto& s3_cfg = storage->get_s3_config();
    EXPECT_TRUE(s3_cfg.use_transfer_acceleration);
    EXPECT_TRUE(s3_cfg.use_dualstack);
    EXPECT_EQ(s3_cfg.signature_version, "v4");
}

}  // namespace
}  // namespace kcenon::file_transfer
