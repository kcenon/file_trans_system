/**
 * @file test_gcs_storage.cpp
 * @brief Unit tests for Google Cloud Storage backend
 */

#include <gtest/gtest.h>

#include "kcenon/file_transfer/cloud/gcs_storage.h"

namespace kcenon::file_transfer {
namespace {

// ============================================================================
// Mock HTTP Client for Testing
// ============================================================================

/**
 * @brief Helper function to create test service account JSON with valid RSA key
 */
inline std::string create_test_service_account_json() {
    // Valid 2048-bit RSA private key for testing (not for production use)
    return R"({
        "type": "service_account",
        "project_id": "my-project-id",
        "private_key_id": "key123",
        "private_key": "-----BEGIN RSA PRIVATE KEY-----\nMIIEowIBAAKCAQEA0Z3VS5JJcds3xfn/ygWyF8PbnGy0AHB7MxExmUZ8M+cQ8bpy\nHDmNRbZ+/Mc6D3G4rQhE0J0aQMJGZ7qWQVk4BFTrBfF7fZ5PY7M7CsYNl7Q5i1P0\nSdT1xjyWaRVlz3BfhCKz8/4MZEKtfPxJi/VdG8uGGfKG6QXvKsFn/bP2dxQWpRq0\ntXqG1q3o8IZNxT4xmQqhN1xJ7K8qREeoHxVj8nE1QqGYXLQjP+b0z8v6T6Y2NG1i\n3HWLKSyK8QMJEb1P8t8Qnmq9GOPY3vF4WYU5TXjKTRjNwFHmWS8X0F1NMuXPCqNJ\nV6hR0tYR6gHbRMYZ+7B8P5MkZ3C5CvPPt6qy3wIDAQABAoIBAAL0VHLgA8GU1y3/\nJtG+7hkQGNqFRPMPuOEmYKLJWvt8EGUq4pv5IGIhlQ5HsYlSiMcR3xQwJkP8T1cP\nPelON1kYI4D7k3VjQPrzPYQ5YjGwhoezqBJJL6SAxVQQGCdZaM9AoVq7n6XLIID7\nxhQ9NjLhUFl0lQopBwqDYjAOu4+d5ixjLPFVCrPAQ0YpXprxY+J5G5Q5/3xPGTcQ\njnz4XoRLuGgn1pHXmkxZVHMj5APNQZ6P3+LmE9VWVtPh3JqmihS9DLqN7QoM9FEK\npxtmCt9cV/Q8INYI3LxpNvZPL3q0TZxP3HhvHU+nFP3rMvF8PvfJt5iQpN0qF9Ec\n3S7HEhECgYEA7pZR2vGY0Y8E/P4H8pFAqSAH9cExDUbD1S5HjLLl6QU8UEs8MRdN\nfqM8pfexOT9kJVFlG5e8qJQmqK+5y6m+qNk6hU3vERHNaM4RcQ2qJz3AYzhz0uzn\nkPR/hXsA3nL2I9PqEGGZj3E6LPPJ3XpKr4cPh7L3q5vg7OJzDR6P+3kCgYEA4OB8\nt4+gEu8p5c8QpVxP/zylQJdLY7u5QsPl3+cQ6GWGK9rkF6P8OZHt3cQ8GNvMxAyQ\nh0R0xhQqM0l7vPetE/OEKF+1jc8JaRhBKLq9LXeQEUP+9Sq7hJ7tODhJ5bGikxLu\nhQjH7NyLOFU7N5u0W6jLp4gJHr4XJ3K7R6WuXH8CgYBNwMH3DXBopmX+2yb0xyxP\n0lT3qJQAWuwD3+wF0M0T4w6P8jqKT0v3Q7M3V6o8HJtLw5xP0Y8JfPJKqM3kHnD9\nOqVn2T6o3bE4f9zPMh+T/k4jVLnCbifj/z2q3h3cKHvM7BoWYxKWLZM3+BqPq6Lt\nZq8F+t5pKRz+EVdne4HhsQKBgGiLU0GlMGaFo7bIfOHaKze7QKFP1hK7d3LqQk9h\n0T1J0YfxR1oQ5gPg7XM5D1B0k9c7m1iG6aIqhz7qPJqLVy0gTNpKYFqT1OYo/xo7\ng4JqLXpKXkL9K3x2n2iK5rY2lXxXNJPM/G4rTl6LxKpJ6T8d0I7OifWJGF3g5vN9\nv4HRAoGBALn8qW3o1X3sMi6cH+q4u9/NhEZsT2F7KRd0rQ5nqBKr6z6xLpqEeAQo\nHdLcQ3OVfYT6I8I5Y8JYPQ3hM3qQQxXe5h7c5I4mM2+d8M3f0kvvfj5k9bj6MKQM\nHX8b7HBo0aLfJj7WlQ3I8fvoVz5M5qU8rAQ9PvMh7f5Q2p8mJdWz\n-----END RSA PRIVATE KEY-----\n",
        "client_email": "test@my-project-id.iam.gserviceaccount.com",
        "client_id": "123456789",
        "auth_uri": "https://accounts.google.com/o/oauth2/auth",
        "token_uri": "https://oauth2.googleapis.com/token"
    })";
}

/**
 * @brief Mock HTTP client that returns configurable responses
 */
class mock_gcs_http_client : public gcs_http_client_interface {
public:
    // Default responses for common operations
    struct mock_response {
        int status_code = 200;
        std::string body;
    };

    mock_response token_response{200, R"({"access_token":"test_token","expires_in":3600})"};
    mock_response upload_response{200, R"({"name":"test/hello.txt","etag":"\"abc123\"","size":"5"})"};
    mock_response download_response{200, ""};
    mock_response delete_response{204, ""};
    mock_response list_response{200, R"({"items":[{"name":"file1.txt"},{"name":"file2.txt"}]})"};
    mock_response metadata_response{200, R"({"name":"test/file.txt","contentType":"text/plain","etag":"\"abc123\"","size":"100"})"};
    mock_response copy_response{200, R"({"name":"dest/file.txt","etag":"\"copied123\""})"};
    mock_response compose_response{200, R"({"name":"composed.txt","etag":"\"composed123\""})"};

    auto get(
        const std::string& url,
        const std::map<std::string, std::string>& /*query*/,
        const std::map<std::string, std::string>& /*headers*/)
        -> result<gcs_http_response> override {
        gcs_http_response resp;

        if (url.find("/storage/v1/b/") != std::string::npos && url.find("/o/") != std::string::npos) {
            if (url.find("alt=media") != std::string::npos) {
                // Download request
                resp.status_code = download_response.status_code;
                resp.body = std::vector<uint8_t>(download_response.body.begin(), download_response.body.end());
            } else if (url.find("prefix=") != std::string::npos) {
                // List request (has prefix= query param)
                resp.status_code = list_response.status_code;
                resp.body = std::vector<uint8_t>(list_response.body.begin(), list_response.body.end());
            } else {
                // Metadata request (GET /storage/v1/b/{bucket}/o/{object})
                resp.status_code = metadata_response.status_code;
                resp.body = std::vector<uint8_t>(metadata_response.body.begin(), metadata_response.body.end());
            }
        } else if (url.find("/storage/v1/b/") != std::string::npos && url.find("/o?") != std::string::npos) {
            // List objects without prefix
            resp.status_code = list_response.status_code;
            resp.body = std::vector<uint8_t>(list_response.body.begin(), list_response.body.end());
        } else {
            resp.status_code = 200;
        }

        return resp;
    }

    auto post(
        const std::string& url,
        const std::vector<uint8_t>& /*body*/,
        const std::map<std::string, std::string>& /*headers*/)
        -> result<gcs_http_response> override {
        gcs_http_response resp;

        if (url.find("/upload/storage/v1/b/") != std::string::npos) {
            // Upload request
            resp.status_code = upload_response.status_code;
            resp.body = std::vector<uint8_t>(upload_response.body.begin(), upload_response.body.end());
        } else if (url.find("/rewriteTo/") != std::string::npos) {
            // Copy request
            resp.status_code = copy_response.status_code;
            resp.body = std::vector<uint8_t>(copy_response.body.begin(), copy_response.body.end());
        } else if (url.find("/compose") != std::string::npos) {
            // Compose request
            resp.status_code = compose_response.status_code;
            resp.body = std::vector<uint8_t>(compose_response.body.begin(), compose_response.body.end());
        } else {
            resp.status_code = 200;
        }

        return resp;
    }

    auto post(
        const std::string& url,
        const std::string& /*body*/,
        const std::map<std::string, std::string>& /*headers*/)
        -> result<gcs_http_response> override {
        gcs_http_response resp;

        if (url.find("oauth2.googleapis.com/token") != std::string::npos ||
            url.find("/token") != std::string::npos) {
            // Token request
            resp.status_code = token_response.status_code;
            resp.body = std::vector<uint8_t>(token_response.body.begin(), token_response.body.end());
        } else if (url.find("/copyTo/") != std::string::npos) {
            // Copy request
            resp.status_code = copy_response.status_code;
            resp.body = std::vector<uint8_t>(copy_response.body.begin(), copy_response.body.end());
        } else if (url.find("/compose") != std::string::npos) {
            // Compose request
            resp.status_code = compose_response.status_code;
            resp.body = std::vector<uint8_t>(compose_response.body.begin(), compose_response.body.end());
        } else {
            resp.status_code = 200;
        }

        return resp;
    }

    auto del(
        const std::string& /*url*/,
        const std::map<std::string, std::string>& /*headers*/)
        -> result<gcs_http_response> override {
        gcs_http_response resp;
        resp.status_code = delete_response.status_code;
        resp.body = std::vector<uint8_t>(delete_response.body.begin(), delete_response.body.end());
        return resp;
    }
};

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
        creds.service_account_json = create_test_service_account_json();
        provider_ = gcs_credential_provider::create(creds);

        auto config = cloud_config_builder::gcs()
            .with_project_id("my-project-id")
            .with_bucket("my-bucket")
            .build_gcs();

        // Use mock HTTP client for testing
        mock_http_client_ = std::make_shared<mock_gcs_http_client>();
        storage_ = gcs_storage::create(config, provider_, mock_http_client_);
        storage_->connect();
    }

    void TearDown() override {
        if (storage_ && storage_->is_connected()) {
            storage_->disconnect();
        }
    }

    std::shared_ptr<credential_provider> provider_;
    std::shared_ptr<mock_gcs_http_client> mock_http_client_;
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
        EXPECT_EQ(result.value().key, "test/hello.txt");
        EXPECT_EQ(result.value().bytes_uploaded, data.size());
        EXPECT_FALSE(result.value().etag.empty());
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

    (void)storage_->disconnect();
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
        EXPECT_EQ(result.value().key, "test/file.txt");
        EXPECT_EQ(result.value().content_type, "text/plain");
    }
}

TEST_F(GcsStorageOperationTest, DeleteObject) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    auto result = storage_->delete_object("test/to_delete.txt");
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_EQ(result.value().key, "test/to_delete.txt");
    }
}

TEST_F(GcsStorageOperationTest, DeleteMultipleObjects) {
    ASSERT_NE(storage_, nullptr);
    ASSERT_TRUE(storage_->is_connected());

    std::vector<std::string> keys = {"file1.txt", "file2.txt", "file3.txt"};

    auto result = storage_->delete_objects(keys);
    EXPECT_TRUE(result.has_value());

    if (result.has_value()) {
        EXPECT_EQ(result.value().size(), keys.size());
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
        EXPECT_EQ(result.value().key, "dest/file.txt");
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
        creds.service_account_json = create_test_service_account_json();
        provider_ = gcs_credential_provider::create(creds);

        auto config = cloud_config_builder::gcs()
            .with_project_id("my-project-id")
            .with_bucket("my-bucket")
            .build_gcs();

        // Use mock HTTP client for testing
        mock_http_client_ = std::make_shared<mock_gcs_http_client>();
        storage_ = gcs_storage::create(config, provider_, mock_http_client_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::shared_ptr<mock_gcs_http_client> mock_http_client_;
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
        creds.service_account_json = create_test_service_account_json();
        provider_ = gcs_credential_provider::create(creds);

        auto config = cloud_config_builder::gcs()
            .with_project_id("my-project-id")
            .with_bucket("my-bucket")
            .build_gcs();

        // Use mock HTTP client for testing
        mock_http_client_ = std::make_shared<mock_gcs_http_client>();
        storage_ = gcs_storage::create(config, provider_, mock_http_client_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::shared_ptr<mock_gcs_http_client> mock_http_client_;
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
        EXPECT_EQ(result.value().key, "test/streamed.txt");
        EXPECT_EQ(result.value().bytes_uploaded, 500);
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
        creds.service_account_json = create_test_service_account_json();
        provider_ = gcs_credential_provider::create(creds);

        auto config = cloud_config_builder::gcs()
            .with_project_id("my-project-id")
            .with_bucket("my-bucket")
            .build_gcs();

        // Use mock HTTP client for testing
        mock_http_client_ = std::make_shared<mock_gcs_http_client>();
        storage_ = gcs_storage::create(config, provider_, mock_http_client_);
        storage_->connect();
    }

    std::shared_ptr<credential_provider> provider_;
    std::shared_ptr<mock_gcs_http_client> mock_http_client_;
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
        EXPECT_EQ(result.value().key, "composed.txt");
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
        EXPECT_TRUE(result.value().find("https://") != std::string::npos ||
                    result.value().find("http://") != std::string::npos);
        EXPECT_TRUE(result.value().find("my-bucket") != std::string::npos);
    }
}
#endif

}  // namespace
}  // namespace kcenon::file_transfer
