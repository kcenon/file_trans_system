/**
 * @file test_cloud_interface.cpp
 * @brief Unit tests for cloud storage abstraction layer
 */

#include <gtest/gtest.h>

#include "kcenon/file_transfer/cloud/cloud_config.h"
#include "kcenon/file_transfer/cloud/cloud_credentials.h"
#include "kcenon/file_transfer/cloud/cloud_error.h"
#include "kcenon/file_transfer/cloud/cloud_storage_interface.h"

namespace kcenon::file_transfer {
namespace {

// ============================================================================
// Cloud Error Code Tests
// ============================================================================

class CloudErrorCodeTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(CloudErrorCodeTest, AuthErrorRange) {
    EXPECT_TRUE(is_auth_error(static_cast<int32_t>(cloud_error_code::auth_failed)));
    EXPECT_TRUE(is_auth_error(static_cast<int32_t>(cloud_error_code::auth_expired)));
    EXPECT_TRUE(is_auth_error(static_cast<int32_t>(cloud_error_code::auth_invalid_credentials)));
    EXPECT_TRUE(is_auth_error(static_cast<int32_t>(cloud_error_code::auth_missing_credentials)));
    EXPECT_TRUE(is_auth_error(static_cast<int32_t>(cloud_error_code::auth_mfa_required)));
    EXPECT_FALSE(is_auth_error(static_cast<int32_t>(cloud_error_code::access_denied)));
}

TEST_F(CloudErrorCodeTest, AuthorizationErrorRange) {
    EXPECT_TRUE(is_authorization_error(static_cast<int32_t>(cloud_error_code::access_denied)));
    EXPECT_TRUE(is_authorization_error(static_cast<int32_t>(cloud_error_code::permission_denied)));
    EXPECT_TRUE(is_authorization_error(static_cast<int32_t>(cloud_error_code::resource_forbidden)));
    EXPECT_FALSE(is_authorization_error(static_cast<int32_t>(cloud_error_code::auth_failed)));
}

TEST_F(CloudErrorCodeTest, ConnectionErrorRange) {
    EXPECT_TRUE(is_cloud_connection_error(static_cast<int32_t>(cloud_error_code::connection_failed)));
    EXPECT_TRUE(is_cloud_connection_error(static_cast<int32_t>(cloud_error_code::connection_timeout)));
    EXPECT_TRUE(is_cloud_connection_error(static_cast<int32_t>(cloud_error_code::service_unavailable)));
    EXPECT_TRUE(is_cloud_connection_error(static_cast<int32_t>(cloud_error_code::rate_limited)));
    EXPECT_FALSE(is_cloud_connection_error(static_cast<int32_t>(cloud_error_code::bucket_not_found)));
}

TEST_F(CloudErrorCodeTest, BucketErrorRange) {
    EXPECT_TRUE(is_bucket_error(static_cast<int32_t>(cloud_error_code::bucket_not_found)));
    EXPECT_TRUE(is_bucket_error(static_cast<int32_t>(cloud_error_code::bucket_already_exists)));
    EXPECT_TRUE(is_bucket_error(static_cast<int32_t>(cloud_error_code::bucket_not_empty)));
    EXPECT_FALSE(is_bucket_error(static_cast<int32_t>(cloud_error_code::object_not_found)));
}

TEST_F(CloudErrorCodeTest, ObjectErrorRange) {
    EXPECT_TRUE(is_object_error(static_cast<int32_t>(cloud_error_code::object_not_found)));
    EXPECT_TRUE(is_object_error(static_cast<int32_t>(cloud_error_code::object_already_exists)));
    EXPECT_TRUE(is_object_error(static_cast<int32_t>(cloud_error_code::checksum_mismatch)));
    EXPECT_FALSE(is_object_error(static_cast<int32_t>(cloud_error_code::upload_failed)));
}

TEST_F(CloudErrorCodeTest, TransferErrorRange) {
    EXPECT_TRUE(is_cloud_transfer_error(static_cast<int32_t>(cloud_error_code::upload_failed)));
    EXPECT_TRUE(is_cloud_transfer_error(static_cast<int32_t>(cloud_error_code::download_failed)));
    EXPECT_TRUE(is_cloud_transfer_error(static_cast<int32_t>(cloud_error_code::transfer_cancelled)));
    EXPECT_FALSE(is_cloud_transfer_error(static_cast<int32_t>(cloud_error_code::storage_quota_exceeded)));
}

TEST_F(CloudErrorCodeTest, QuotaErrorRange) {
    EXPECT_TRUE(is_quota_error(static_cast<int32_t>(cloud_error_code::storage_quota_exceeded)));
    EXPECT_TRUE(is_quota_error(static_cast<int32_t>(cloud_error_code::bandwidth_limit_exceeded)));
    EXPECT_TRUE(is_quota_error(static_cast<int32_t>(cloud_error_code::file_size_limit_exceeded)));
    EXPECT_FALSE(is_quota_error(static_cast<int32_t>(cloud_error_code::provider_error)));
}

TEST_F(CloudErrorCodeTest, RetryableErrors) {
    EXPECT_TRUE(is_cloud_retryable(static_cast<int32_t>(cloud_error_code::connection_timeout)));
    EXPECT_TRUE(is_cloud_retryable(static_cast<int32_t>(cloud_error_code::rate_limited)));
    EXPECT_TRUE(is_cloud_retryable(static_cast<int32_t>(cloud_error_code::service_unavailable)));
    EXPECT_TRUE(is_cloud_retryable(static_cast<int32_t>(cloud_error_code::upload_failed)));
    EXPECT_FALSE(is_cloud_retryable(static_cast<int32_t>(cloud_error_code::access_denied)));
    EXPECT_FALSE(is_cloud_retryable(static_cast<int32_t>(cloud_error_code::bucket_not_found)));
}

TEST_F(CloudErrorCodeTest, ErrorCodeToString) {
    EXPECT_EQ(to_string(cloud_error_code::success), "success");
    EXPECT_EQ(to_string(cloud_error_code::auth_failed), "authentication failed");
    EXPECT_EQ(to_string(cloud_error_code::bucket_not_found), "bucket/container not found");
    EXPECT_EQ(to_string(cloud_error_code::object_not_found), "object/blob not found");
    EXPECT_EQ(to_string(cloud_error_code::rate_limited), "request rate limited");
}

// ============================================================================
// Cloud Provider Tests
// ============================================================================

class CloudProviderTest : public ::testing::Test {};

TEST_F(CloudProviderTest, ProviderToString) {
    EXPECT_STREQ(to_string(cloud_provider::aws_s3), "aws-s3");
    EXPECT_STREQ(to_string(cloud_provider::azure_blob), "azure-blob");
    EXPECT_STREQ(to_string(cloud_provider::google_cloud), "google-cloud");
    EXPECT_STREQ(to_string(cloud_provider::custom), "custom");
}

TEST_F(CloudProviderTest, CredentialTypeToString) {
    EXPECT_STREQ(to_string(credential_type::static_credentials), "static-credentials");
    EXPECT_STREQ(to_string(credential_type::iam_role), "iam-role");
    EXPECT_STREQ(to_string(credential_type::managed_identity), "managed-identity");
    EXPECT_STREQ(to_string(credential_type::service_account), "service-account");
    EXPECT_STREQ(to_string(credential_type::assume_role), "assume-role");
}

TEST_F(CloudProviderTest, CredentialStateToString) {
    EXPECT_STREQ(to_string(credential_state::uninitialized), "uninitialized");
    EXPECT_STREQ(to_string(credential_state::valid), "valid");
    EXPECT_STREQ(to_string(credential_state::expired), "expired");
    EXPECT_STREQ(to_string(credential_state::invalid), "invalid");
    EXPECT_STREQ(to_string(credential_state::refreshing), "refreshing");
}

// ============================================================================
// Cloud Credentials Tests
// ============================================================================

class CloudCredentialsTest : public ::testing::Test {};

TEST_F(CloudCredentialsTest, BaseCredentialsDefaults) {
    cloud_credentials creds;

    EXPECT_EQ(creds.type, credential_type::static_credentials);
    EXPECT_FALSE(creds.session_token.has_value());
    EXPECT_FALSE(creds.expiration.has_value());
    EXPECT_FALSE(creds.region.has_value());
}

TEST_F(CloudCredentialsTest, IsExpiredWithNoExpiration) {
    cloud_credentials creds;
    EXPECT_FALSE(creds.is_expired());
}

TEST_F(CloudCredentialsTest, IsExpiredWithFutureExpiration) {
    cloud_credentials creds;
    creds.expiration = std::chrono::system_clock::now() + std::chrono::hours{1};
    EXPECT_FALSE(creds.is_expired());
}

TEST_F(CloudCredentialsTest, IsExpiredWithPastExpiration) {
    cloud_credentials creds;
    creds.expiration = std::chrono::system_clock::now() - std::chrono::hours{1};
    EXPECT_TRUE(creds.is_expired());
}

TEST_F(CloudCredentialsTest, TimeUntilExpirationNoExpiration) {
    cloud_credentials creds;
    EXPECT_FALSE(creds.time_until_expiration().has_value());
}

TEST_F(CloudCredentialsTest, TimeUntilExpirationFuture) {
    cloud_credentials creds;
    creds.expiration = std::chrono::system_clock::now() + std::chrono::seconds{3600};
    auto time = creds.time_until_expiration();
    ASSERT_TRUE(time.has_value());
    EXPECT_GT(time.value().count(), 3500);
    EXPECT_LE(time.value().count(), 3600);
}

TEST_F(CloudCredentialsTest, TimeUntilExpirationPast) {
    cloud_credentials creds;
    creds.expiration = std::chrono::system_clock::now() - std::chrono::hours{1};
    auto time = creds.time_until_expiration();
    ASSERT_TRUE(time.has_value());
    EXPECT_EQ(time.value().count(), 0);
}

TEST_F(CloudCredentialsTest, StaticCredentials) {
    static_credentials creds;

    EXPECT_EQ(creds.type, credential_type::static_credentials);
    EXPECT_TRUE(creds.access_key_id.empty());
    EXPECT_TRUE(creds.secret_access_key.empty());

    creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
    creds.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";

    EXPECT_EQ(creds.access_key_id, "AKIAIOSFODNN7EXAMPLE");
    EXPECT_EQ(creds.secret_access_key, "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY");
}

TEST_F(CloudCredentialsTest, AzureCredentials) {
    azure_credentials creds;

    EXPECT_EQ(creds.type, credential_type::static_credentials);
    EXPECT_TRUE(creds.account_name.empty());
    EXPECT_FALSE(creds.account_key.has_value());
    EXPECT_FALSE(creds.connection_string.has_value());
    EXPECT_FALSE(creds.sas_token.has_value());
}

TEST_F(CloudCredentialsTest, GcsCredentials) {
    gcs_credentials creds;

    EXPECT_EQ(creds.type, credential_type::service_account);
    EXPECT_FALSE(creds.service_account_file.has_value());
    EXPECT_FALSE(creds.service_account_json.has_value());
    EXPECT_FALSE(creds.project_id.has_value());
}

TEST_F(CloudCredentialsTest, AssumeRoleCredentials) {
    assume_role_credentials creds;

    EXPECT_EQ(creds.type, credential_type::assume_role);
    EXPECT_TRUE(creds.role_arn.empty());
    EXPECT_TRUE(creds.role_session_name.empty());
    EXPECT_EQ(creds.duration, std::chrono::seconds{3600});
    EXPECT_FALSE(creds.external_id.has_value());
    EXPECT_FALSE(creds.mfa_serial.has_value());
}

TEST_F(CloudCredentialsTest, ProfileCredentials) {
    profile_credentials creds;

    EXPECT_EQ(creds.type, credential_type::profile);
    EXPECT_EQ(creds.profile_name, "default");
    EXPECT_FALSE(creds.credentials_file.has_value());
    EXPECT_FALSE(creds.config_file.has_value());
}

// ============================================================================
// Cloud Config Tests
// ============================================================================

class CloudConfigTest : public ::testing::Test {};

TEST_F(CloudConfigTest, RetryPolicyDefaults) {
    cloud_retry_policy policy;

    EXPECT_EQ(policy.max_attempts, 3);
    EXPECT_EQ(policy.initial_delay, std::chrono::milliseconds{1000});
    EXPECT_EQ(policy.max_delay, std::chrono::milliseconds{30000});
    EXPECT_DOUBLE_EQ(policy.backoff_multiplier, 2.0);
    EXPECT_TRUE(policy.use_jitter);
    EXPECT_TRUE(policy.retry_on_rate_limit);
    EXPECT_TRUE(policy.retry_on_connection_error);
    EXPECT_TRUE(policy.retry_on_server_error);
}

TEST_F(CloudConfigTest, MultipartConfigDefaults) {
    multipart_config config;

    EXPECT_TRUE(config.enabled);
    EXPECT_EQ(config.threshold, 100 * 1024 * 1024);
    EXPECT_EQ(config.part_size, 5 * 1024 * 1024);
    EXPECT_EQ(config.max_concurrent_parts, 4);
    EXPECT_EQ(config.max_part_retries, 3);
}

TEST_F(CloudConfigTest, TransferOptionsDefaults) {
    cloud_transfer_options options;

    EXPECT_EQ(options.timeout, std::chrono::milliseconds{0});
    EXPECT_TRUE(options.verify_checksum);
    EXPECT_EQ(options.checksum_algorithm, "md5");
    EXPECT_FALSE(options.content_type.has_value());
    EXPECT_TRUE(options.metadata.empty());
    EXPECT_FALSE(options.storage_class.has_value());
}

TEST_F(CloudConfigTest, BaseStorageConfigDefaults) {
    cloud_storage_config config;

    EXPECT_EQ(config.provider, cloud_provider::aws_s3);
    EXPECT_TRUE(config.bucket.empty());
    EXPECT_TRUE(config.region.empty());
    EXPECT_FALSE(config.endpoint.has_value());
    EXPECT_FALSE(config.use_path_style);
    EXPECT_TRUE(config.use_ssl);
    EXPECT_TRUE(config.verify_ssl);
    EXPECT_EQ(config.connect_timeout, std::chrono::milliseconds{30000});
    EXPECT_EQ(config.connection_pool_size, 25);
    EXPECT_TRUE(config.keep_alive);
}

TEST_F(CloudConfigTest, S3ConfigDefaults) {
    s3_config config;

    EXPECT_EQ(config.provider, cloud_provider::aws_s3);
    EXPECT_FALSE(config.use_transfer_acceleration);
    EXPECT_FALSE(config.use_dualstack);
    EXPECT_FALSE(config.use_express_one_zone);
    EXPECT_EQ(config.signature_version, "v4");
    EXPECT_TRUE(config.use_chunked_encoding);
    EXPECT_FALSE(config.request_payer.has_value());
}

TEST_F(CloudConfigTest, AzureBlobConfigDefaults) {
    azure_blob_config config;

    EXPECT_EQ(config.provider, cloud_provider::azure_blob);
    EXPECT_TRUE(config.container.empty());
    EXPECT_TRUE(config.account_name.empty());
    EXPECT_EQ(config.api_version, "2023-11-03");
    EXPECT_TRUE(config.validate_content_md5);
    EXPECT_FALSE(config.access_tier.has_value());
    EXPECT_FALSE(config.use_customer_encryption_key);
}

TEST_F(CloudConfigTest, GcsConfigDefaults) {
    gcs_config config;

    EXPECT_EQ(config.provider, cloud_provider::google_cloud);
    EXPECT_TRUE(config.project_id.empty());
    EXPECT_FALSE(config.predefined_acl.has_value());
    EXPECT_FALSE(config.use_csek);
    EXPECT_TRUE(config.uniform_bucket_level_access);
}

TEST_F(CloudConfigTest, S3ConfigBuilder) {
    auto config = cloud_config_builder::s3()
        .with_bucket("my-bucket")
        .with_region("us-east-1")
        .with_endpoint("http://localhost:9000")
        .with_path_style(true)
        .with_ssl(false, false)
        .with_connect_timeout(std::chrono::milliseconds{5000})
        .with_connection_pool_size(10)
        .with_transfer_acceleration(true)
        .with_dualstack(true)
        .build_s3();

    EXPECT_EQ(config.provider, cloud_provider::aws_s3);
    EXPECT_EQ(config.bucket, "my-bucket");
    EXPECT_EQ(config.region, "us-east-1");
    ASSERT_TRUE(config.endpoint.has_value());
    EXPECT_EQ(config.endpoint.value(), "http://localhost:9000");
    EXPECT_TRUE(config.use_path_style);
    EXPECT_FALSE(config.use_ssl);
    EXPECT_FALSE(config.verify_ssl);
    EXPECT_EQ(config.connect_timeout, std::chrono::milliseconds{5000});
    EXPECT_EQ(config.connection_pool_size, 10);
    EXPECT_TRUE(config.use_transfer_acceleration);
    EXPECT_TRUE(config.use_dualstack);
}

TEST_F(CloudConfigTest, AzureBlobConfigBuilder) {
    auto config = cloud_config_builder::azure_blob()
        .with_bucket("my-container")
        .with_region("eastus")
        .with_account_name("myaccount")
        .with_access_tier("Hot")
        .with_request_timeout(std::chrono::milliseconds{60000})
        .build_azure_blob();

    EXPECT_EQ(config.provider, cloud_provider::azure_blob);
    EXPECT_EQ(config.bucket, "my-container");
    EXPECT_EQ(config.container, "my-container");
    EXPECT_EQ(config.region, "eastus");
    EXPECT_EQ(config.account_name, "myaccount");
    ASSERT_TRUE(config.access_tier.has_value());
    EXPECT_EQ(config.access_tier.value(), "Hot");
}

TEST_F(CloudConfigTest, GcsConfigBuilder) {
    auto config = cloud_config_builder::gcs()
        .with_bucket("my-gcs-bucket")
        .with_region("us-central1")
        .with_project_id("my-project-123")
        .with_uniform_bucket_level_access(false)
        .build_gcs();

    EXPECT_EQ(config.provider, cloud_provider::google_cloud);
    EXPECT_EQ(config.bucket, "my-gcs-bucket");
    EXPECT_EQ(config.region, "us-central1");
    EXPECT_EQ(config.project_id, "my-project-123");
    EXPECT_FALSE(config.uniform_bucket_level_access);
}

TEST_F(CloudConfigTest, ConfigWithRetryPolicy) {
    cloud_retry_policy policy;
    policy.max_attempts = 5;
    policy.initial_delay = std::chrono::milliseconds{500};
    policy.backoff_multiplier = 1.5;

    auto config = cloud_config_builder::s3()
        .with_bucket("test-bucket")
        .with_retry_policy(policy)
        .build_s3();

    EXPECT_EQ(config.retry.max_attempts, 5);
    EXPECT_EQ(config.retry.initial_delay, std::chrono::milliseconds{500});
    EXPECT_DOUBLE_EQ(config.retry.backoff_multiplier, 1.5);
}

TEST_F(CloudConfigTest, ConfigWithMultipart) {
    multipart_config mp_config;
    mp_config.threshold = 50 * 1024 * 1024;
    mp_config.part_size = 10 * 1024 * 1024;
    mp_config.max_concurrent_parts = 8;

    auto config = cloud_config_builder::s3()
        .with_bucket("test-bucket")
        .with_multipart(mp_config)
        .build_s3();

    EXPECT_EQ(config.multipart.threshold, 50 * 1024 * 1024);
    EXPECT_EQ(config.multipart.part_size, 10 * 1024 * 1024);
    EXPECT_EQ(config.multipart.max_concurrent_parts, 8);
}

// ============================================================================
// Cloud Storage State Tests
// ============================================================================

class CloudStorageStateTest : public ::testing::Test {};

TEST_F(CloudStorageStateTest, StateToString) {
    EXPECT_STREQ(to_string(cloud_storage_state::disconnected), "disconnected");
    EXPECT_STREQ(to_string(cloud_storage_state::connecting), "connecting");
    EXPECT_STREQ(to_string(cloud_storage_state::connected), "connected");
    EXPECT_STREQ(to_string(cloud_storage_state::error), "error");
}

// ============================================================================
// Cloud Object Metadata Tests
// ============================================================================

class CloudObjectMetadataTest : public ::testing::Test {};

TEST_F(CloudObjectMetadataTest, DefaultValues) {
    cloud_object_metadata metadata;

    EXPECT_TRUE(metadata.key.empty());
    EXPECT_EQ(metadata.size, 0);
    EXPECT_TRUE(metadata.etag.empty());
    EXPECT_TRUE(metadata.content_type.empty());
    EXPECT_FALSE(metadata.content_encoding.has_value());
    EXPECT_FALSE(metadata.storage_class.has_value());
    EXPECT_FALSE(metadata.version_id.has_value());
    EXPECT_FALSE(metadata.md5.has_value());
    EXPECT_TRUE(metadata.custom_metadata.empty());
    EXPECT_FALSE(metadata.is_directory);
}

TEST_F(CloudObjectMetadataTest, WithValues) {
    cloud_object_metadata metadata;
    metadata.key = "path/to/file.txt";
    metadata.size = 1024;
    metadata.etag = "\"abc123\"";
    metadata.content_type = "text/plain";
    metadata.storage_class = "STANDARD";
    metadata.version_id = "v1";
    metadata.custom_metadata = {{"author", "test"}};

    EXPECT_EQ(metadata.key, "path/to/file.txt");
    EXPECT_EQ(metadata.size, 1024);
    EXPECT_EQ(metadata.etag, "\"abc123\"");
    EXPECT_EQ(metadata.content_type, "text/plain");
    ASSERT_TRUE(metadata.storage_class.has_value());
    EXPECT_EQ(metadata.storage_class.value(), "STANDARD");
    EXPECT_EQ(metadata.custom_metadata.size(), 1);
}

// ============================================================================
// List Objects Tests
// ============================================================================

class ListObjectsTest : public ::testing::Test {};

TEST_F(ListObjectsTest, OptionsDefaults) {
    list_objects_options options;

    EXPECT_FALSE(options.prefix.has_value());
    ASSERT_TRUE(options.delimiter.has_value());
    EXPECT_EQ(options.delimiter.value(), "/");
    EXPECT_EQ(options.max_keys, 1000);
    EXPECT_FALSE(options.continuation_token.has_value());
    EXPECT_FALSE(options.start_after.has_value());
    EXPECT_FALSE(options.fetch_owner);
}

TEST_F(ListObjectsTest, ResultDefaults) {
    list_objects_result result;

    EXPECT_TRUE(result.objects.empty());
    EXPECT_TRUE(result.common_prefixes.empty());
    EXPECT_FALSE(result.is_truncated);
    EXPECT_FALSE(result.continuation_token.has_value());
    EXPECT_FALSE(result.total_count.has_value());
}

// ============================================================================
// Progress Tests
// ============================================================================

class ProgressTest : public ::testing::Test {};

TEST_F(ProgressTest, UploadProgressPercentage) {
    upload_progress progress;
    progress.bytes_transferred = 50;
    progress.total_bytes = 100;

    EXPECT_DOUBLE_EQ(progress.percentage(), 50.0);
}

TEST_F(ProgressTest, UploadProgressZeroTotal) {
    upload_progress progress;
    progress.bytes_transferred = 0;
    progress.total_bytes = 0;

    EXPECT_DOUBLE_EQ(progress.percentage(), 0.0);
}

TEST_F(ProgressTest, DownloadProgressPercentage) {
    download_progress progress;
    progress.bytes_transferred = 750;
    progress.total_bytes = 1000;

    EXPECT_DOUBLE_EQ(progress.percentage(), 75.0);
}

TEST_F(ProgressTest, DownloadProgressComplete) {
    download_progress progress;
    progress.bytes_transferred = 1024;
    progress.total_bytes = 1024;

    EXPECT_DOUBLE_EQ(progress.percentage(), 100.0);
}

// ============================================================================
// Presigned URL Options Tests
// ============================================================================

class PresignedUrlOptionsTest : public ::testing::Test {};

TEST_F(PresignedUrlOptionsTest, Defaults) {
    presigned_url_options options;

    EXPECT_EQ(options.expiration, std::chrono::seconds{3600});
    EXPECT_EQ(options.method, "GET");
    EXPECT_FALSE(options.content_type.has_value());
    EXPECT_FALSE(options.content_md5.has_value());
}

// ============================================================================
// Cloud Storage Statistics Tests
// ============================================================================

class CloudStorageStatisticsTest : public ::testing::Test {};

TEST_F(CloudStorageStatisticsTest, DefaultValues) {
    cloud_storage_statistics stats;

    EXPECT_EQ(stats.bytes_uploaded, 0);
    EXPECT_EQ(stats.bytes_downloaded, 0);
    EXPECT_EQ(stats.upload_count, 0);
    EXPECT_EQ(stats.download_count, 0);
    EXPECT_EQ(stats.list_count, 0);
    EXPECT_EQ(stats.delete_count, 0);
    EXPECT_EQ(stats.errors, 0);
}

}  // namespace
}  // namespace kcenon::file_transfer
