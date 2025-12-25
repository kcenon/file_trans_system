/**
 * @file test_azure_blob_storage.cpp
 * @brief Unit tests for Azure Blob Storage backend
 */

#include <gtest/gtest.h>

#include "kcenon/file_transfer/cloud/azure_blob_storage.h"

namespace kcenon::file_transfer {
namespace {

// ============================================================================
// Azure Blob Credential Provider Tests
// ============================================================================

class AzureBlobCredentialProviderTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(AzureBlobCredentialProviderTest, CreateFromAzureCredentials) {
    azure_credentials creds;
    creds.account_name = "mystorageaccount";
    creds.account_key = "dGVzdGFjY291bnRrZXkxMjM0NTY3ODkwYWJjZGVmZ2hpams=";

    auto provider = azure_blob_credential_provider::create(creds);
    ASSERT_NE(provider, nullptr);

    EXPECT_EQ(provider->provider(), cloud_provider::azure_blob);
    EXPECT_EQ(provider->state(), credential_state::valid);
    EXPECT_FALSE(provider->needs_refresh());
    EXPECT_EQ(provider->account_name(), "mystorageaccount");

    auto retrieved = provider->get_credentials();
    ASSERT_NE(retrieved, nullptr);
}

TEST_F(AzureBlobCredentialProviderTest, CreateFromEmptyCredentialsFails) {
    azure_credentials creds;
    // Empty credentials

    auto provider = azure_blob_credential_provider::create(creds);
    EXPECT_EQ(provider, nullptr);
}

TEST_F(AzureBlobCredentialProviderTest, CreateFromMissingAccountNameFails) {
    azure_credentials creds;
    creds.account_key = "dGVzdGFjY291bnRrZXkxMjM0NTY3ODkwYWJjZGVmZ2hpams=";
    // Missing account name

    auto provider = azure_blob_credential_provider::create(creds);
    EXPECT_EQ(provider, nullptr);
}

TEST_F(AzureBlobCredentialProviderTest, CreateFromConnectionString) {
    std::string conn_str =
        "DefaultEndpointsProtocol=https;"
        "AccountName=mystorageaccount;"
        "AccountKey=dGVzdGFjY291bnRrZXkxMjM0NTY3ODkwYWJjZGVmZ2hpams=;"
        "EndpointSuffix=core.windows.net";

    auto provider = azure_blob_credential_provider::create_from_connection_string(conn_str);
    ASSERT_NE(provider, nullptr);

    EXPECT_EQ(provider->account_name(), "mystorageaccount");
    EXPECT_EQ(provider->auth_type(), "connection-string");
}

TEST_F(AzureBlobCredentialProviderTest, CreateFromInvalidConnectionStringFails) {
    std::string conn_str = "InvalidConnectionString";

    auto provider = azure_blob_credential_provider::create_from_connection_string(conn_str);
    EXPECT_EQ(provider, nullptr);
}

TEST_F(AzureBlobCredentialProviderTest, CreateFromSasToken) {
    auto provider = azure_blob_credential_provider::create_from_sas_token(
        "mystorageaccount",
        "sv=2023-11-03&ss=b&srt=sco&sp=rwdlacup&se=2024-12-31T23:59:59Z&sig=testsig");

    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->account_name(), "mystorageaccount");
    EXPECT_EQ(provider->auth_type(), "sas-token");
}

TEST_F(AzureBlobCredentialProviderTest, CreateFromClientCredentials) {
    auto provider = azure_blob_credential_provider::create_from_client_credentials(
        "tenant-id-12345",
        "client-id-12345",
        "client-secret-12345",
        "mystorageaccount");

    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->account_name(), "mystorageaccount");
    EXPECT_EQ(provider->auth_type(), "client-credentials");
}

TEST_F(AzureBlobCredentialProviderTest, RefreshStaticCredentials) {
    azure_credentials creds;
    creds.account_name = "mystorageaccount";
    creds.account_key = "dGVzdGFjY291bnRrZXkxMjM0NTY3ODkwYWJjZGVmZ2hpams=";

    auto provider = azure_blob_credential_provider::create(creds);
    ASSERT_NE(provider, nullptr);

    // Refresh should succeed (no-op for static credentials)
    EXPECT_TRUE(provider->refresh());
    EXPECT_EQ(provider->state(), credential_state::valid);
}

// ============================================================================
// Azure Blob Storage Creation Tests
// ============================================================================

class AzureBlobStorageCreationTest : public ::testing::Test {
protected:
    void SetUp() override {
        creds_.account_name = "mystorageaccount";
        creds_.account_key = "dGVzdGFjY291bnRrZXkxMjM0NTY3ODkwYWJjZGVmZ2hpams=";
        provider_ = azure_blob_credential_provider::create(creds_);
    }

    azure_credentials creds_;
    std::shared_ptr<credential_provider> provider_;
};

TEST_F(AzureBlobStorageCreationTest, CreateWithValidConfig) {
    auto config = cloud_config_builder::azure_blob()
        .with_account_name("mystorageaccount")
        .with_bucket("mycontainer")
        .build_azure_blob();

    auto storage = azure_blob_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    EXPECT_EQ(storage->provider(), cloud_provider::azure_blob);
    EXPECT_EQ(storage->provider_name(), "azure-blob");
    EXPECT_EQ(storage->container(), "mycontainer");
    EXPECT_EQ(storage->account_name(), "mystorageaccount");
    EXPECT_EQ(storage->state(), cloud_storage_state::disconnected);
    EXPECT_FALSE(storage->is_connected());
}

TEST_F(AzureBlobStorageCreationTest, CreateWithCustomEndpoint) {
    auto config = cloud_config_builder::azure_blob()
        .with_account_name("mystorageaccount")
        .with_bucket("mycontainer")
        .with_endpoint("http://localhost:10000/devstoreaccount1")
        .build_azure_blob();

    auto storage = azure_blob_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    EXPECT_EQ(storage->endpoint_url(), "http://localhost:10000/devstoreaccount1");
}

TEST_F(AzureBlobStorageCreationTest, CreateWithAccessTier) {
    auto config = cloud_config_builder::azure_blob()
        .with_account_name("mystorageaccount")
        .with_bucket("mycontainer")
        .with_access_tier("Cool")
        .build_azure_blob();

    auto storage = azure_blob_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    const auto& azure_config = storage->get_azure_config();
    EXPECT_EQ(azure_config.access_tier.value_or(""), "Cool");
}

TEST_F(AzureBlobStorageCreationTest, CreateWithEmptyContainerFails) {
    auto config = cloud_config_builder::azure_blob()
        .with_account_name("mystorageaccount")
        .build_azure_blob();
    // No container set

    auto storage = azure_blob_storage::create(config, provider_);
    EXPECT_EQ(storage, nullptr);
}

TEST_F(AzureBlobStorageCreationTest, CreateWithEmptyAccountNameFails) {
    auto config = cloud_config_builder::azure_blob()
        .with_bucket("mycontainer")
        .build_azure_blob();
    // No account name set

    auto storage = azure_blob_storage::create(config, provider_);
    EXPECT_EQ(storage, nullptr);
}

TEST_F(AzureBlobStorageCreationTest, CreateWithNullCredentialsFails) {
    auto config = cloud_config_builder::azure_blob()
        .with_account_name("mystorageaccount")
        .with_bucket("mycontainer")
        .build_azure_blob();

    auto storage = azure_blob_storage::create(config, nullptr);
    EXPECT_EQ(storage, nullptr);
}

// ============================================================================
// Azure Blob Storage Connection Tests
// ============================================================================

class AzureBlobStorageConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        azure_credentials creds;
        creds.account_name = "mystorageaccount";
        creds.account_key = "dGVzdGFjY291bnRrZXkxMjM0NTY3ODkwYWJjZGVmZ2hpams=";
        provider_ = azure_blob_credential_provider::create(creds);

        auto config = cloud_config_builder::azure_blob()
            .with_account_name("mystorageaccount")
            .with_bucket("test-container")
            .build_azure_blob();

        storage_ = azure_blob_storage::create(config, provider_);
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<azure_blob_storage> storage_;
};

TEST_F(AzureBlobStorageConnectionTest, Connect) {
    ASSERT_NE(storage_, nullptr);
    EXPECT_EQ(storage_->state(), cloud_storage_state::disconnected);

    auto result = storage_->connect();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(storage_->state(), cloud_storage_state::connected);
    EXPECT_TRUE(storage_->is_connected());
}

TEST_F(AzureBlobStorageConnectionTest, Disconnect) {
    ASSERT_NE(storage_, nullptr);

    auto connect_result = storage_->connect();
    EXPECT_TRUE(connect_result.has_value());

    auto disconnect_result = storage_->disconnect();
    EXPECT_TRUE(disconnect_result.has_value());
    EXPECT_EQ(storage_->state(), cloud_storage_state::disconnected);
    EXPECT_FALSE(storage_->is_connected());
}

TEST_F(AzureBlobStorageConnectionTest, StateChangedCallback) {
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
// Azure Blob Storage Upload Tests
// ============================================================================

class AzureBlobStorageUploadTest : public ::testing::Test {
protected:
    void SetUp() override {
        azure_credentials creds;
        creds.account_name = "mystorageaccount";
        creds.account_key = "dGVzdGFjY291bnRrZXkxMjM0NTY3ODkwYWJjZGVmZ2hpams=";
        provider_ = azure_blob_credential_provider::create(creds);

        auto config = cloud_config_builder::azure_blob()
            .with_account_name("mystorageaccount")
            .with_bucket("test-container")
            .build_azure_blob();

        storage_ = azure_blob_storage::create(config, provider_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<azure_blob_storage> storage_;
};

TEST_F(AzureBlobStorageUploadTest, UploadSmallData) {
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

TEST_F(AzureBlobStorageUploadTest, UploadWithOptions) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    std::vector<std::byte> data(512);
    std::fill(data.begin(), data.end(), std::byte{0x01});

    cloud_transfer_options options;
    options.content_type = "application/octet-stream";
    options.storage_class = "Hot";

    auto result = storage_->upload("test/data.bin", data, options);
    EXPECT_TRUE(result.has_value());
}

TEST_F(AzureBlobStorageUploadTest, UploadNotConnectedFails) {
    ASSERT_NE(storage_, nullptr);
    storage_->disconnect();

    std::vector<std::byte> data(100);
    auto result = storage_->upload("test/file.bin", data);
    EXPECT_FALSE(result.has_value());
}

TEST_F(AzureBlobStorageUploadTest, UploadAsync) {
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
// Azure Blob Storage Statistics Tests
// ============================================================================

class AzureBlobStorageStatisticsTest : public ::testing::Test {
protected:
    void SetUp() override {
        azure_credentials creds;
        creds.account_name = "mystorageaccount";
        creds.account_key = "dGVzdGFjY291bnRrZXkxMjM0NTY3ODkwYWJjZGVmZ2hpams=";
        provider_ = azure_blob_credential_provider::create(creds);

        auto config = cloud_config_builder::azure_blob()
            .with_account_name("mystorageaccount")
            .with_bucket("test-container")
            .build_azure_blob();

        storage_ = azure_blob_storage::create(config, provider_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<azure_blob_storage> storage_;
};

TEST_F(AzureBlobStorageStatisticsTest, InitialStatistics) {
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

TEST_F(AzureBlobStorageStatisticsTest, StatisticsAfterUpload) {
    ASSERT_NE(storage_, nullptr);

    std::vector<std::byte> data(1024);
    storage_->upload("test/file.bin", data);

    auto stats = storage_->get_statistics();
    EXPECT_EQ(stats.bytes_uploaded, 1024);
    EXPECT_EQ(stats.upload_count, 1);
}

TEST_F(AzureBlobStorageStatisticsTest, ResetStatistics) {
    ASSERT_NE(storage_, nullptr);

    std::vector<std::byte> data(512);
    storage_->upload("test/file.bin", data);

    storage_->reset_statistics();

    auto stats = storage_->get_statistics();
    EXPECT_EQ(stats.bytes_uploaded, 0);
    EXPECT_EQ(stats.upload_count, 0);
}

// ============================================================================
// Azure Blob Upload Stream Tests
// ============================================================================

class AzureBlobUploadStreamTest : public ::testing::Test {
protected:
    void SetUp() override {
        azure_credentials creds;
        creds.account_name = "mystorageaccount";
        creds.account_key = "dGVzdGFjY291bnRrZXkxMjM0NTY3ODkwYWJjZGVmZ2hpams=";
        provider_ = azure_blob_credential_provider::create(creds);

        auto config = cloud_config_builder::azure_blob()
            .with_account_name("mystorageaccount")
            .with_bucket("test-container")
            .build_azure_blob();

        storage_ = azure_blob_storage::create(config, provider_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<azure_blob_storage> storage_;
};

TEST_F(AzureBlobUploadStreamTest, CreateUploadStream) {
    ASSERT_NE(storage_, nullptr);

    auto stream = storage_->create_upload_stream("stream/file.bin");
    ASSERT_NE(stream, nullptr);

    // Azure doesn't have upload_id like S3
    EXPECT_FALSE(stream->upload_id().has_value());
    EXPECT_EQ(stream->bytes_written(), 0);
}

TEST_F(AzureBlobUploadStreamTest, WriteToStream) {
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

TEST_F(AzureBlobUploadStreamTest, FinalizeStream) {
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

TEST_F(AzureBlobUploadStreamTest, AbortStream) {
    ASSERT_NE(storage_, nullptr);

    auto stream = storage_->create_upload_stream("stream/file.bin");
    ASSERT_NE(stream, nullptr);

    std::vector<std::byte> data(512);
    stream->write(data);

    auto result = stream->abort();
    EXPECT_TRUE(result.has_value());
}

// ============================================================================
// Azure Blob SAS Token Tests
// ============================================================================

#ifdef FILE_TRANS_ENABLE_ENCRYPTION

class AzureBlobSasTokenTest : public ::testing::Test {
protected:
    void SetUp() override {
        azure_credentials creds;
        creds.account_name = "mystorageaccount";
        creds.account_key = "dGVzdGFjY291bnRrZXkxMjM0NTY3ODkwYWJjZGVmZ2hpams=";
        provider_ = azure_blob_credential_provider::create(creds);

        auto config = cloud_config_builder::azure_blob()
            .with_account_name("mystorageaccount")
            .with_bucket("test-container")
            .build_azure_blob();

        storage_ = azure_blob_storage::create(config, provider_);
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<azure_blob_storage> storage_;
};

TEST_F(AzureBlobSasTokenTest, GenerateBlobSas) {
    ASSERT_NE(storage_, nullptr);

    presigned_url_options options;
    options.method = "GET";
    options.expiration = std::chrono::seconds{3600};

    auto result = storage_->generate_blob_sas("test/file.bin", options);
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        const auto& url = result.value();
        EXPECT_TRUE(url.find("sv=") != std::string::npos);
        EXPECT_TRUE(url.find("sr=b") != std::string::npos);
        EXPECT_TRUE(url.find("sig=") != std::string::npos);
        EXPECT_TRUE(url.find("test/file.bin") != std::string::npos);
    }
}

TEST_F(AzureBlobSasTokenTest, GenerateContainerSas) {
    ASSERT_NE(storage_, nullptr);

    presigned_url_options options;
    options.method = "GET";
    options.expiration = std::chrono::seconds{300};

    auto result = storage_->generate_container_sas(options);
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        const auto& url = result.value();
        EXPECT_TRUE(url.find("sv=") != std::string::npos);
        EXPECT_TRUE(url.find("sig=") != std::string::npos);
    }
}

TEST_F(AzureBlobSasTokenTest, GeneratePresignedUrl) {
    ASSERT_NE(storage_, nullptr);

    presigned_url_options options;
    options.method = "PUT";
    options.expiration = std::chrono::seconds{600};

    auto result = storage_->generate_presigned_url("upload/file.bin", options);
    EXPECT_TRUE(result.has_value());
}

#endif  // FILE_TRANS_ENABLE_ENCRYPTION

// ============================================================================
// Azure Blob Access Tier Tests
// ============================================================================

class AzureBlobAccessTierTest : public ::testing::Test {
protected:
    void SetUp() override {
        azure_credentials creds;
        creds.account_name = "mystorageaccount";
        creds.account_key = "dGVzdGFjY291bnRrZXkxMjM0NTY3ODkwYWJjZGVmZ2hpams=";
        provider_ = azure_blob_credential_provider::create(creds);

        auto config = cloud_config_builder::azure_blob()
            .with_account_name("mystorageaccount")
            .with_bucket("test-container")
            .build_azure_blob();

        storage_ = azure_blob_storage::create(config, provider_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<azure_blob_storage> storage_;
};

TEST_F(AzureBlobAccessTierTest, GetAccessTier) {
    ASSERT_NE(storage_, nullptr);

    auto result = storage_->get_access_tier("test/file.bin");
    EXPECT_TRUE(result.has_value());
    // Default tier is "Hot"
    EXPECT_EQ(result.value(), "Hot");
}

TEST_F(AzureBlobAccessTierTest, SetAccessTier) {
    ASSERT_NE(storage_, nullptr);

    auto result = storage_->set_access_tier("test/file.bin", "Cool");
    EXPECT_TRUE(result.has_value());
}

TEST_F(AzureBlobAccessTierTest, SetAccessTierNotConnectedFails) {
    ASSERT_NE(storage_, nullptr);
    storage_->disconnect();

    auto result = storage_->set_access_tier("test/file.bin", "Archive");
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// Azure Blob Configuration Tests
// ============================================================================

class AzureBlobConfigurationTest : public ::testing::Test {
protected:
    void SetUp() override {
        azure_credentials creds;
        creds.account_name = "mystorageaccount";
        creds.account_key = "dGVzdGFjY291bnRrZXkxMjM0NTY3ODkwYWJjZGVmZ2hpams=";
        provider_ = azure_blob_credential_provider::create(creds);
    }

    std::shared_ptr<credential_provider> provider_;
};

TEST_F(AzureBlobConfigurationTest, GetConfiguration) {
    auto config = cloud_config_builder::azure_blob()
        .with_account_name("mystorageaccount")
        .with_bucket("my-container")
        .with_region("eastus")
        .with_connect_timeout(std::chrono::milliseconds{5000})
        .with_connection_pool_size(10)
        .build_azure_blob();

    auto storage = azure_blob_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    const auto& stored_config = storage->config();
    EXPECT_EQ(stored_config.bucket, "my-container");
    EXPECT_EQ(stored_config.region, "eastus");
    EXPECT_EQ(stored_config.connect_timeout, std::chrono::milliseconds{5000});
    EXPECT_EQ(stored_config.connection_pool_size, 10);
}

TEST_F(AzureBlobConfigurationTest, GetAzureSpecificConfiguration) {
    auto config = cloud_config_builder::azure_blob()
        .with_account_name("mystorageaccount")
        .with_bucket("my-container")
        .with_access_tier("Cool")
        .build_azure_blob();

    auto storage = azure_blob_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    const auto& azure_config = storage->get_azure_config();
    EXPECT_EQ(azure_config.account_name, "mystorageaccount");
    EXPECT_EQ(azure_config.container, "my-container");
    EXPECT_EQ(azure_config.access_tier.value_or(""), "Cool");
    EXPECT_EQ(azure_config.api_version, "2023-11-03");
}

TEST_F(AzureBlobConfigurationTest, DefaultEndpointUrl) {
    auto config = cloud_config_builder::azure_blob()
        .with_account_name("mystorageaccount")
        .with_bucket("my-container")
        .build_azure_blob();

    auto storage = azure_blob_storage::create(config, provider_);
    ASSERT_NE(storage, nullptr);

    EXPECT_EQ(storage->endpoint_url(), "https://mystorageaccount.blob.core.windows.net");
}

// ============================================================================
// Azure Blob Object Operations Tests
// ============================================================================

class AzureBlobObjectOperationsTest : public ::testing::Test {
protected:
    void SetUp() override {
        azure_credentials creds;
        creds.account_name = "mystorageaccount";
        creds.account_key = "dGVzdGFjY291bnRrZXkxMjM0NTY3ODkwYWJjZGVmZ2hpams=";
        provider_ = azure_blob_credential_provider::create(creds);

        auto config = cloud_config_builder::azure_blob()
            .with_account_name("mystorageaccount")
            .with_bucket("test-container")
            .build_azure_blob();

        storage_ = azure_blob_storage::create(config, provider_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<azure_blob_storage> storage_;
};

TEST_F(AzureBlobObjectOperationsTest, DeleteObject) {
    ASSERT_NE(storage_, nullptr);

    auto result = storage_->delete_object("test/file.bin");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value().key, "test/file.bin");
}

TEST_F(AzureBlobObjectOperationsTest, DeleteMultipleObjects) {
    ASSERT_NE(storage_, nullptr);

    std::vector<std::string> keys = {"file1.bin", "file2.bin", "file3.bin"};
    auto result = storage_->delete_objects(keys);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3);
}

TEST_F(AzureBlobObjectOperationsTest, Exists) {
    ASSERT_NE(storage_, nullptr);

    auto result = storage_->exists("test/file.bin");
    EXPECT_TRUE(result.has_value());
}

TEST_F(AzureBlobObjectOperationsTest, GetMetadata) {
    ASSERT_NE(storage_, nullptr);

    auto result = storage_->get_metadata("test/file.txt");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value().key, "test/file.txt");
    EXPECT_EQ(result.value().content_type, "text/plain");
}

TEST_F(AzureBlobObjectOperationsTest, ListObjects) {
    ASSERT_NE(storage_, nullptr);

    list_objects_options options;
    options.prefix = "test/";
    options.max_keys = 100;

    auto result = storage_->list_objects(options);
    EXPECT_TRUE(result.has_value());
}

TEST_F(AzureBlobObjectOperationsTest, CopyObject) {
    ASSERT_NE(storage_, nullptr);

    auto result = storage_->copy_object("source/file.bin", "dest/file.bin");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value().key, "dest/file.bin");
}

}  // namespace
}  // namespace kcenon::file_transfer
