/**
 * @file test_gcs_storage.cpp
 * @brief Unit tests for Google Cloud Storage backend
 */

#include <gtest/gtest.h>

#include "kcenon/file_transfer/cloud/gcs_storage.h"

namespace kcenon::file_transfer {
namespace {

// ============================================================================
// GCS Credential Provider Tests
// ============================================================================

class GcsCredentialProviderTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(GcsCredentialProviderTest, CreateFromGcsCredentials) {
    gcs_credentials creds;
    creds.project_id = "my-project-id";
    creds.service_account_json = R"({
        "type": "service_account",
        "project_id": "my-project-id",
        "private_key_id": "key123",
        "private_key": "-----BEGIN RSA PRIVATE KEY-----\nMIIBogIBAAJBALRi\n-----END RSA PRIVATE KEY-----\n",
        "client_email": "test@my-project-id.iam.gserviceaccount.com",
        "client_id": "123456789",
        "auth_uri": "https://accounts.google.com/o/oauth2/auth",
        "token_uri": "https://oauth2.googleapis.com/token"
    })";

    auto provider = gcs_credential_provider::create(creds);
    ASSERT_NE(provider, nullptr);

    EXPECT_EQ(provider->provider(), cloud_provider::google_cloud);
    EXPECT_EQ(provider->state(), credential_state::valid);
    EXPECT_FALSE(provider->needs_refresh());
    EXPECT_EQ(provider->project_id(), "my-project-id");

    auto retrieved = provider->get_credentials();
    ASSERT_NE(retrieved, nullptr);
}

TEST_F(GcsCredentialProviderTest, CreateFromEmptyCredentialsFails) {
    gcs_credentials creds;
    // Empty credentials

    auto provider = gcs_credential_provider::create(creds);
    EXPECT_EQ(provider, nullptr);
}

TEST_F(GcsCredentialProviderTest, CreateFromServiceAccountJson) {
    std::string json = R"({
        "type": "service_account",
        "project_id": "test-project",
        "private_key_id": "abc123",
        "private_key": "-----BEGIN RSA PRIVATE KEY-----\nMIIBogIBAAJBALRi\n-----END RSA PRIVATE KEY-----\n",
        "client_email": "sa@test-project.iam.gserviceaccount.com",
        "client_id": "987654321",
        "auth_uri": "https://accounts.google.com/o/oauth2/auth",
        "token_uri": "https://oauth2.googleapis.com/token"
    })";

    auto provider = gcs_credential_provider::create_from_service_account_json(json);
    ASSERT_NE(provider, nullptr);

    EXPECT_EQ(provider->project_id(), "test-project");
    EXPECT_EQ(provider->service_account_email(), "sa@test-project.iam.gserviceaccount.com");
    EXPECT_EQ(provider->auth_type(), "service-account-json");
}

TEST_F(GcsCredentialProviderTest, CreateFromInvalidJsonFails) {
    std::string json = "invalid json content";

    auto provider = gcs_credential_provider::create_from_service_account_json(json);
    EXPECT_EQ(provider, nullptr);
}

TEST_F(GcsCredentialProviderTest, CreateFromIncompleteJsonFails) {
    std::string json = R"({
        "type": "service_account",
        "project_id": "test-project"
    })";
    // Missing required fields: private_key, client_email

    auto provider = gcs_credential_provider::create_from_service_account_json(json);
    EXPECT_EQ(provider, nullptr);
}

TEST_F(GcsCredentialProviderTest, RefreshStaticCredentials) {
    gcs_credentials creds;
    creds.project_id = "my-project-id";
    creds.service_account_json = R"({
        "type": "service_account",
        "project_id": "my-project-id",
        "private_key_id": "key123",
        "private_key": "-----BEGIN RSA PRIVATE KEY-----\nMIIBogIBAAJBALRi\n-----END RSA PRIVATE KEY-----\n",
        "client_email": "test@my-project-id.iam.gserviceaccount.com",
        "client_id": "123456789",
        "auth_uri": "https://accounts.google.com/o/oauth2/auth",
        "token_uri": "https://oauth2.googleapis.com/token"
    })";

    auto provider = gcs_credential_provider::create(creds);
    ASSERT_NE(provider, nullptr);

    // Refresh should succeed (no-op for static credentials)
    EXPECT_TRUE(provider->refresh());
    EXPECT_EQ(provider->state(), credential_state::valid);
}

// ============================================================================
// GCS Storage Creation Tests
// ============================================================================

class GcsStorageCreationTest : public ::testing::Test {
protected:
    void SetUp() override {
        creds_.project_id = "my-project-id";
        creds_.service_account_json = R"({
            "type": "service_account",
            "project_id": "my-project-id",
            "private_key_id": "key123",
            "private_key": "-----BEGIN RSA PRIVATE KEY-----\nMIIBogIBAAJBALRi\n-----END RSA PRIVATE KEY-----\n",
            "client_email": "test@my-project-id.iam.gserviceaccount.com",
            "client_id": "123456789",
            "auth_uri": "https://accounts.google.com/o/oauth2/auth",
            "token_uri": "https://oauth2.googleapis.com/token"
        })";
        provider_ = gcs_credential_provider::create(creds_);
    }

    gcs_credentials creds_;
    std::shared_ptr<credential_provider> provider_;
};

TEST_F(GcsStorageCreationTest, CreateWithValidConfig) {
    auto config = cloud_config_builder::gcs()
        .with_project_id("my-project-id")
        .with_bucket("my-bucket")
        .build_gcs();

    auto storage = gcs_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    EXPECT_EQ(storage->provider(), cloud_provider::google_cloud);
    EXPECT_EQ(storage->provider_name(), "google-cloud");
    EXPECT_EQ(storage->bucket(), "my-bucket");
    EXPECT_EQ(storage->project_id(), "my-project-id");
    EXPECT_EQ(storage->state(), cloud_storage_state::disconnected);
    EXPECT_FALSE(storage->is_connected());
}

TEST_F(GcsStorageCreationTest, CreateWithCustomEndpoint) {
    auto config = cloud_config_builder::gcs()
        .with_project_id("my-project-id")
        .with_bucket("my-bucket")
        .with_endpoint("http://localhost:4443")
        .build_gcs();

    auto storage = gcs_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    EXPECT_EQ(storage->endpoint_url(), "http://localhost:4443");
}

TEST_F(GcsStorageCreationTest, CreateWithRegion) {
    auto config = cloud_config_builder::gcs()
        .with_project_id("my-project-id")
        .with_bucket("my-bucket")
        .with_region("us-central1")
        .build_gcs();

    auto storage = gcs_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    EXPECT_EQ(storage->region(), "us-central1");
}

TEST_F(GcsStorageCreationTest, CreateWithEmptyBucketFails) {
    auto config = cloud_config_builder::gcs()
        .with_project_id("my-project-id")
        .build_gcs();
    // No bucket set

    auto storage = gcs_storage::create(config, provider_);
    EXPECT_EQ(storage, nullptr);
}

TEST_F(GcsStorageCreationTest, CreateWithNullCredentialsFails) {
    auto config = cloud_config_builder::gcs()
        .with_project_id("my-project-id")
        .with_bucket("my-bucket")
        .build_gcs();

    auto storage = gcs_storage::create(config, nullptr);
    EXPECT_EQ(storage, nullptr);
}

// ============================================================================
// GCS Storage Connection Tests
// ============================================================================

class GcsStorageConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        gcs_credentials creds;
        creds.project_id = "my-project-id";
        creds.service_account_json = R"({
            "type": "service_account",
            "project_id": "my-project-id",
            "private_key_id": "key123",
            "private_key": "-----BEGIN RSA PRIVATE KEY-----\nMIIBogIBAAJBALRi\n-----END RSA PRIVATE KEY-----\n",
            "client_email": "test@my-project-id.iam.gserviceaccount.com",
            "client_id": "123456789",
            "auth_uri": "https://accounts.google.com/o/oauth2/auth",
            "token_uri": "https://oauth2.googleapis.com/token"
        })";
        provider_ = gcs_credential_provider::create(creds);

        auto config = cloud_config_builder::gcs()
            .with_project_id("my-project-id")
            .with_bucket("my-bucket")
            .build_gcs();

        storage_ = gcs_storage::create(config, provider_);
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<gcs_storage> storage_;
};

TEST_F(GcsStorageConnectionTest, ConnectSuccessfully) {
    ASSERT_NE(storage_, nullptr);
    EXPECT_FALSE(storage_->is_connected());

    auto result = storage_->connect();
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(storage_->is_connected());
    EXPECT_EQ(storage_->state(), cloud_storage_state::connected);
}

TEST_F(GcsStorageConnectionTest, DisconnectSuccessfully) {
    ASSERT_NE(storage_, nullptr);

    auto connect_result = storage_->connect();
    ASSERT_TRUE(connect_result.has_value());

    auto disconnect_result = storage_->disconnect();
    EXPECT_TRUE(disconnect_result.has_value());
    EXPECT_FALSE(storage_->is_connected());
    EXPECT_EQ(storage_->state(), cloud_storage_state::disconnected);
}

TEST_F(GcsStorageConnectionTest, StateChangedCallback) {
    ASSERT_NE(storage_, nullptr);

    std::vector<cloud_storage_state> state_changes;
    storage_->on_state_changed([&state_changes](cloud_storage_state state) {
        state_changes.push_back(state);
    });

    storage_->connect();
    storage_->disconnect();

    ASSERT_GE(state_changes.size(), 2);
    EXPECT_EQ(state_changes[0], cloud_storage_state::connecting);
    EXPECT_EQ(state_changes[1], cloud_storage_state::connected);
}

// ============================================================================
// GCS Storage Operation Tests
// ============================================================================

class GcsStorageOperationTest : public ::testing::Test {
protected:
    void SetUp() override {
        gcs_credentials creds;
        creds.project_id = "my-project-id";
        creds.service_account_json = R"({
            "type": "service_account",
            "project_id": "my-project-id",
            "private_key_id": "key123",
            "private_key": "-----BEGIN RSA PRIVATE KEY-----\nMIIBogIBAAJBALRi\n-----END RSA PRIVATE KEY-----\n",
            "client_email": "test@my-project-id.iam.gserviceaccount.com",
            "client_id": "123456789",
            "auth_uri": "https://accounts.google.com/o/oauth2/auth",
            "token_uri": "https://oauth2.googleapis.com/token"
        })";
        provider_ = gcs_credential_provider::create(creds);

        auto config = cloud_config_builder::gcs()
            .with_project_id("my-project-id")
            .with_bucket("my-bucket")
            .build_gcs();

        storage_ = gcs_storage::create(config, provider_);
        storage_->connect();
    }

    void TearDown() override {
        if (storage_ && storage_->is_connected()) {
            storage_->disconnect();
        }
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<gcs_storage> storage_;
};

TEST_F(GcsStorageOperationTest, UploadData) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    std::vector<std::byte> data = {
        std::byte{0x48}, std::byte{0x65}, std::byte{0x6c}, std::byte{0x6c}, std::byte{0x6f}  // "Hello"
    };

    auto result = storage_->upload("test/hello.txt", data);
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_EQ(result->key, "test/hello.txt");
        EXPECT_EQ(result->bytes_uploaded, data.size());
        EXPECT_FALSE(result->etag.empty());
    }
}

TEST_F(GcsStorageOperationTest, UploadDataWithOptions) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    std::vector<std::byte> data(100, std::byte{0x42});

    cloud_transfer_options options;
    options.content_type = "application/octet-stream";
    options.storage_class = "NEARLINE";

    auto result = storage_->upload("test/data.bin", data, options);
    EXPECT_TRUE(result.has_value());
}

TEST_F(GcsStorageOperationTest, OperationsRequireConnection) {
    ASSERT_NE(storage_, nullptr);

    storage_->disconnect();
    EXPECT_FALSE(storage_->is_connected());

    std::vector<std::byte> data = {std::byte{0x00}};

    auto upload_result = storage_->upload("test.txt", data);
    EXPECT_FALSE(upload_result.has_value());

    auto download_result = storage_->download("test.txt");
    EXPECT_FALSE(download_result.has_value());

    auto delete_result = storage_->delete_object("test.txt");
    EXPECT_FALSE(delete_result.has_value());

    auto list_result = storage_->list_objects();
    EXPECT_FALSE(list_result.has_value());
}

TEST_F(GcsStorageOperationTest, GetMetadata) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    auto result = storage_->get_metadata("test/file.txt");
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_EQ(result->key, "test/file.txt");
        EXPECT_EQ(result->content_type, "text/plain");
    }
}

TEST_F(GcsStorageOperationTest, DeleteObject) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    auto result = storage_->delete_object("test/to_delete.txt");
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_EQ(result->key, "test/to_delete.txt");
    }
}

TEST_F(GcsStorageOperationTest, DeleteMultipleObjects) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    std::vector<std::string> keys = {"file1.txt", "file2.txt", "file3.txt"};

    auto result = storage_->delete_objects(keys);
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_EQ(result->size(), keys.size());
    }
}

TEST_F(GcsStorageOperationTest, ListObjects) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    list_objects_options options;
    options.prefix = "test/";
    options.max_keys = 100;

    auto result = storage_->list_objects(options);
    EXPECT_TRUE(result.has_value());
}

TEST_F(GcsStorageOperationTest, CopyObject) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    auto result = storage_->copy_object("source/file.txt", "dest/file.txt");
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_EQ(result->key, "dest/file.txt");
    }
}

// ============================================================================
// GCS Storage Statistics Tests
// ============================================================================

class GcsStorageStatisticsTest : public ::testing::Test {
protected:
    void SetUp() override {
        gcs_credentials creds;
        creds.project_id = "my-project-id";
        creds.service_account_json = R"({
            "type": "service_account",
            "project_id": "my-project-id",
            "private_key_id": "key123",
            "private_key": "-----BEGIN RSA PRIVATE KEY-----\nMIIBogIBAAJBALRi\n-----END RSA PRIVATE KEY-----\n",
            "client_email": "test@my-project-id.iam.gserviceaccount.com",
            "client_id": "123456789",
            "auth_uri": "https://accounts.google.com/o/oauth2/auth",
            "token_uri": "https://oauth2.googleapis.com/token"
        })";
        provider_ = gcs_credential_provider::create(creds);

        auto config = cloud_config_builder::gcs()
            .with_project_id("my-project-id")
            .with_bucket("my-bucket")
            .build_gcs();

        storage_ = gcs_storage::create(config, provider_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<gcs_storage> storage_;
};

TEST_F(GcsStorageStatisticsTest, TrackUploadStatistics) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    storage_->reset_statistics();

    std::vector<std::byte> data(1024, std::byte{0x42});
    storage_->upload("test1.txt", data);
    storage_->upload("test2.txt", data);

    auto stats = storage_->get_statistics();
    EXPECT_EQ(stats.upload_count, 2);
    EXPECT_GE(stats.bytes_uploaded, 2048);
}

TEST_F(GcsStorageStatisticsTest, TrackListStatistics) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    storage_->reset_statistics();

    storage_->list_objects();
    storage_->list_objects();

    auto stats = storage_->get_statistics();
    EXPECT_EQ(stats.list_count, 2);
}

TEST_F(GcsStorageStatisticsTest, TrackDeleteStatistics) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    storage_->reset_statistics();

    storage_->delete_object("test1.txt");
    storage_->delete_object("test2.txt");

    auto stats = storage_->get_statistics();
    EXPECT_EQ(stats.delete_count, 2);
}

TEST_F(GcsStorageStatisticsTest, ResetStatistics) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    std::vector<std::byte> data(100, std::byte{0x42});
    storage_->upload("test.txt", data);

    auto stats_before = storage_->get_statistics();
    EXPECT_GT(stats_before.upload_count, 0);

    storage_->reset_statistics();

    auto stats_after = storage_->get_statistics();
    EXPECT_EQ(stats_after.upload_count, 0);
    EXPECT_EQ(stats_after.bytes_uploaded, 0);
}

// ============================================================================
// GCS Storage Stream Tests
// ============================================================================

class GcsStorageStreamTest : public ::testing::Test {
protected:
    void SetUp() override {
        gcs_credentials creds;
        creds.project_id = "my-project-id";
        creds.service_account_json = R"({
            "type": "service_account",
            "project_id": "my-project-id",
            "private_key_id": "key123",
            "private_key": "-----BEGIN RSA PRIVATE KEY-----\nMIIBogIBAAJBALRi\n-----END RSA PRIVATE KEY-----\n",
            "client_email": "test@my-project-id.iam.gserviceaccount.com",
            "client_id": "123456789",
            "auth_uri": "https://accounts.google.com/o/oauth2/auth",
            "token_uri": "https://oauth2.googleapis.com/token"
        })";
        provider_ = gcs_credential_provider::create(creds);

        auto config = cloud_config_builder::gcs()
            .with_project_id("my-project-id")
            .with_bucket("my-bucket")
            .build_gcs();

        storage_ = gcs_storage::create(config, provider_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<gcs_storage> storage_;
};

TEST_F(GcsStorageStreamTest, CreateUploadStream) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    auto stream = storage_->create_upload_stream("test/streamed.txt");
    ASSERT_NE(stream, nullptr);

    EXPECT_EQ(stream->bytes_written(), 0);
    EXPECT_TRUE(stream->upload_id().has_value());
}

TEST_F(GcsStorageStreamTest, WriteToUploadStream) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    auto stream = storage_->create_upload_stream("test/streamed.txt");
    ASSERT_NE(stream, nullptr);

    std::vector<std::byte> chunk1(1000, std::byte{0x41});
    auto result1 = stream->write(chunk1);
    EXPECT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value(), chunk1.size());

    std::vector<std::byte> chunk2(2000, std::byte{0x42});
    auto result2 = stream->write(chunk2);
    EXPECT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value(), chunk2.size());

    EXPECT_EQ(stream->bytes_written(), 3000);
}

TEST_F(GcsStorageStreamTest, FinalizeUploadStream) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    auto stream = storage_->create_upload_stream("test/streamed.txt");
    ASSERT_NE(stream, nullptr);

    std::vector<std::byte> data(500, std::byte{0x43});
    stream->write(data);

    auto result = stream->finalize();
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_EQ(result->key, "test/streamed.txt");
        EXPECT_EQ(result->bytes_uploaded, 500);
    }
}

TEST_F(GcsStorageStreamTest, AbortUploadStream) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    auto stream = storage_->create_upload_stream("test/aborted.txt");
    ASSERT_NE(stream, nullptr);

    std::vector<std::byte> data(500, std::byte{0x44});
    stream->write(data);

    auto result = stream->abort();
    EXPECT_TRUE(result.has_value());
}

TEST_F(GcsStorageStreamTest, CreateDownloadStream) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    auto stream = storage_->create_download_stream("test/file.txt");
    ASSERT_NE(stream, nullptr);

    EXPECT_EQ(stream->bytes_read(), 0);
}

// ============================================================================
// GCS-specific Feature Tests
// ============================================================================

class GcsSpecificFeatureTest : public ::testing::Test {
protected:
    void SetUp() override {
        gcs_credentials creds;
        creds.project_id = "my-project-id";
        creds.service_account_json = R"({
            "type": "service_account",
            "project_id": "my-project-id",
            "private_key_id": "key123",
            "private_key": "-----BEGIN RSA PRIVATE KEY-----\nMIIBogIBAAJBALRi\n-----END RSA PRIVATE KEY-----\n",
            "client_email": "test@my-project-id.iam.gserviceaccount.com",
            "client_id": "123456789",
            "auth_uri": "https://accounts.google.com/o/oauth2/auth",
            "token_uri": "https://oauth2.googleapis.com/token"
        })";
        provider_ = gcs_credential_provider::create(creds);

        auto config = cloud_config_builder::gcs()
            .with_project_id("my-project-id")
            .with_bucket("my-bucket")
            .build_gcs();

        storage_ = gcs_storage::create(config, provider_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<gcs_storage> storage_;
};

TEST_F(GcsSpecificFeatureTest, SetStorageClass) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    auto result = storage_->set_storage_class("test/file.txt", "NEARLINE");
    EXPECT_TRUE(result.has_value());
}

TEST_F(GcsSpecificFeatureTest, GetStorageClass) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    auto result = storage_->get_storage_class("test/file.txt");
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_EQ(result.value(), "STANDARD");
    }
}

TEST_F(GcsSpecificFeatureTest, ComposeObjects) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    std::vector<std::string> source_keys = {"part1.txt", "part2.txt", "part3.txt"};

    auto result = storage_->compose_objects(source_keys, "composed.txt");
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_EQ(result->key, "composed.txt");
    }
}

TEST_F(GcsSpecificFeatureTest, ComposeObjectsEmptySourceFails) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    std::vector<std::string> empty_keys;

    auto result = storage_->compose_objects(empty_keys, "composed.txt");
    EXPECT_FALSE(result.has_value());
}

TEST_F(GcsSpecificFeatureTest, ComposeObjectsTooManySourcesFails) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    std::vector<std::string> too_many_keys(33);  // Max is 32
    for (int i = 0; i < 33; ++i) {
        too_many_keys[i] = "part" + std::to_string(i) + ".txt";
    }

    auto result = storage_->compose_objects(too_many_keys, "composed.txt");
    EXPECT_FALSE(result.has_value());
}

#ifdef FILE_TRANS_ENABLE_ENCRYPTION
TEST_F(GcsSpecificFeatureTest, GenerateSignedUrl) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    presigned_url_options options;
    options.expiration = std::chrono::seconds{3600};
    options.method = "GET";

    auto result = storage_->generate_signed_url("test/file.txt", options);
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_TRUE(result->find("https://") != std::string::npos ||
                    result->find("http://") != std::string::npos);
        EXPECT_TRUE(result->find("my-bucket") != std::string::npos);
    }
}
#endif

}  // namespace
}  // namespace kcenon::file_transfer
