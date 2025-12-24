/**
 * @file cloud_config.h
 * @brief Cloud storage configuration types
 * @version 0.1.0
 *
 * This file defines configuration structures for cloud storage implementations.
 */

#ifndef KCENON_FILE_TRANSFER_CLOUD_CLOUD_CONFIG_H
#define KCENON_FILE_TRANSFER_CLOUD_CLOUD_CONFIG_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cloud_credentials.h"

namespace kcenon::file_transfer {

/**
 * @brief Retry policy for cloud operations
 */
struct cloud_retry_policy {
    /// Maximum number of retry attempts
    std::size_t max_attempts = 3;

    /// Initial delay between retries
    std::chrono::milliseconds initial_delay{1000};

    /// Maximum delay between retries
    std::chrono::milliseconds max_delay{30000};

    /// Multiplier for exponential backoff
    double backoff_multiplier = 2.0;

    /// Add jitter to retry delays
    bool use_jitter = true;

    /// Retry on rate limiting
    bool retry_on_rate_limit = true;

    /// Retry on connection errors
    bool retry_on_connection_error = true;

    /// Retry on server errors (5xx)
    bool retry_on_server_error = true;
};

/**
 * @brief Multipart upload configuration
 */
struct multipart_config {
    /// Enable multipart upload
    bool enabled = true;

    /// Minimum file size to use multipart upload (default: 100MB)
    uint64_t threshold = 100 * 1024 * 1024;

    /// Part size for multipart upload (default: 5MB)
    uint64_t part_size = 5 * 1024 * 1024;

    /// Maximum concurrent upload parts
    std::size_t max_concurrent_parts = 4;

    /// Timeout for individual part upload
    std::chrono::milliseconds part_timeout{300000};

    /// Maximum retries for failed parts
    std::size_t max_part_retries = 3;
};

/**
 * @brief Transfer options for upload/download operations
 */
struct cloud_transfer_options {
    /// Operation timeout
    std::chrono::milliseconds timeout{0};  ///< 0 = no timeout

    /// Verify checksum after transfer
    bool verify_checksum = true;

    /// Checksum algorithm (md5, sha256, crc32c)
    std::string checksum_algorithm = "md5";

    /// Override content type
    std::optional<std::string> content_type;

    /// Custom metadata
    std::vector<std::pair<std::string, std::string>> metadata;

    /// Storage class (e.g., STANDARD, GLACIER, ARCHIVE)
    std::optional<std::string> storage_class;

    /// Server-side encryption
    std::optional<std::string> server_side_encryption;

    /// KMS key ID for encryption
    std::optional<std::string> kms_key_id;

    /// ACL (e.g., private, public-read)
    std::optional<std::string> acl;

    /// Cache-Control header
    std::optional<std::string> cache_control;

    /// Content-Disposition header
    std::optional<std::string> content_disposition;

    /// Content-Encoding header
    std::optional<std::string> content_encoding;
};

/**
 * @brief Base cloud storage configuration
 */
struct cloud_storage_config {
    cloud_provider provider = cloud_provider::aws_s3;

    /// Bucket/container name
    std::string bucket;

    /// Region
    std::string region;

    /// Custom endpoint URL (for S3-compatible storage)
    std::optional<std::string> endpoint;

    /// Use path-style URLs (vs virtual-hosted style)
    bool use_path_style = false;

    /// Enable SSL/TLS
    bool use_ssl = true;

    /// Verify SSL certificates
    bool verify_ssl = true;

    /// Custom CA certificate path
    std::optional<std::string> ca_cert_path;

    /// Connection timeout
    std::chrono::milliseconds connect_timeout{30000};

    /// Request timeout
    std::chrono::milliseconds request_timeout{0};  ///< 0 = no timeout

    /// Connection pool size
    std::size_t connection_pool_size = 25;

    /// Enable connection keep-alive
    bool keep_alive = true;

    /// Retry policy
    cloud_retry_policy retry;

    /// Multipart upload configuration
    multipart_config multipart;

    /// Default transfer options
    cloud_transfer_options default_transfer_options;

    /// User-Agent string
    std::optional<std::string> user_agent;

    virtual ~cloud_storage_config() = default;
};

/**
 * @brief AWS S3 specific configuration
 */
struct s3_config : cloud_storage_config {
    s3_config() {
        provider = cloud_provider::aws_s3;
    }

    /// Enable S3 Transfer Acceleration
    bool use_transfer_acceleration = false;

    /// Enable dualstack endpoints (IPv4 + IPv6)
    bool use_dualstack = false;

    /// Enable S3 Express One Zone
    bool use_express_one_zone = false;

    /// Signature version (v2, v4)
    std::string signature_version = "v4";

    /// Enable chunked encoding
    bool use_chunked_encoding = true;

    /// S3 request payer (requester, bucket-owner)
    std::optional<std::string> request_payer;
};

/**
 * @brief Azure Blob Storage specific configuration
 */
struct azure_blob_config : cloud_storage_config {
    azure_blob_config() {
        provider = cloud_provider::azure_blob;
    }

    /// Container name (Azure uses "container" instead of "bucket")
    std::string container;

    /// Azure storage account name
    std::string account_name;

    /// Blob service version
    std::string api_version = "2023-11-03";

    /// Enable automatic MD5 validation
    bool validate_content_md5 = true;

    /// Block blob tier (Hot, Cool, Archive)
    std::optional<std::string> access_tier;

    /// Enable customer-provided encryption keys
    bool use_customer_encryption_key = false;

    /// Customer-provided encryption key (base64)
    std::optional<std::string> customer_encryption_key;

    /// Enable immutability policy
    bool enable_immutability = false;
};

/**
 * @brief Google Cloud Storage specific configuration
 */
struct gcs_config : cloud_storage_config {
    gcs_config() {
        provider = cloud_provider::google_cloud;
    }

    /// Project ID
    std::string project_id;

    /// Predefined ACL (e.g., private, publicRead)
    std::optional<std::string> predefined_acl;

    /// Default object ACL
    std::optional<std::string> default_object_acl;

    /// Enable customer-supplied encryption keys
    bool use_csek = false;

    /// Customer-supplied encryption key (base64)
    std::optional<std::string> csek_key;

    /// Customer-supplied encryption key SHA256 (base64)
    std::optional<std::string> csek_key_sha256;

    /// Cloud KMS key name for encryption
    std::optional<std::string> kms_key_name;

    /// Enable uniform bucket-level access
    bool uniform_bucket_level_access = true;
};

/**
 * @brief Cloud storage configuration builder
 */
class cloud_config_builder {
public:
    /**
     * @brief Start building S3 configuration
     */
    static auto s3() -> cloud_config_builder {
        cloud_config_builder builder;
        builder.s3_config_ = s3_config{};
        return builder;
    }

    /**
     * @brief Start building Azure Blob configuration
     */
    static auto azure_blob() -> cloud_config_builder {
        cloud_config_builder builder;
        builder.azure_config_ = azure_blob_config{};
        return builder;
    }

    /**
     * @brief Start building GCS configuration
     */
    static auto gcs() -> cloud_config_builder {
        cloud_config_builder builder;
        builder.gcs_config_ = gcs_config{};
        return builder;
    }

    // Common options
    auto with_bucket(const std::string& bucket) -> cloud_config_builder& {
        if (s3_config_.has_value()) {
            s3_config_->bucket = bucket;
        } else if (azure_config_.has_value()) {
            azure_config_->bucket = bucket;
            azure_config_->container = bucket;
        } else if (gcs_config_.has_value()) {
            gcs_config_->bucket = bucket;
        }
        return *this;
    }

    auto with_region(const std::string& region) -> cloud_config_builder& {
        if (s3_config_.has_value()) {
            s3_config_->region = region;
        } else if (azure_config_.has_value()) {
            azure_config_->region = region;
        } else if (gcs_config_.has_value()) {
            gcs_config_->region = region;
        }
        return *this;
    }

    auto with_endpoint(const std::string& endpoint) -> cloud_config_builder& {
        if (s3_config_.has_value()) {
            s3_config_->endpoint = endpoint;
        } else if (azure_config_.has_value()) {
            azure_config_->endpoint = endpoint;
        } else if (gcs_config_.has_value()) {
            gcs_config_->endpoint = endpoint;
        }
        return *this;
    }

    auto with_path_style(bool enable) -> cloud_config_builder& {
        if (s3_config_.has_value()) {
            s3_config_->use_path_style = enable;
        } else if (azure_config_.has_value()) {
            azure_config_->use_path_style = enable;
        } else if (gcs_config_.has_value()) {
            gcs_config_->use_path_style = enable;
        }
        return *this;
    }

    auto with_ssl(bool enable, bool verify = true) -> cloud_config_builder& {
        if (s3_config_.has_value()) {
            s3_config_->use_ssl = enable;
            s3_config_->verify_ssl = verify;
        } else if (azure_config_.has_value()) {
            azure_config_->use_ssl = enable;
            azure_config_->verify_ssl = verify;
        } else if (gcs_config_.has_value()) {
            gcs_config_->use_ssl = enable;
            gcs_config_->verify_ssl = verify;
        }
        return *this;
    }

    auto with_connect_timeout(std::chrono::milliseconds timeout) -> cloud_config_builder& {
        if (s3_config_.has_value()) {
            s3_config_->connect_timeout = timeout;
        } else if (azure_config_.has_value()) {
            azure_config_->connect_timeout = timeout;
        } else if (gcs_config_.has_value()) {
            gcs_config_->connect_timeout = timeout;
        }
        return *this;
    }

    auto with_request_timeout(std::chrono::milliseconds timeout) -> cloud_config_builder& {
        if (s3_config_.has_value()) {
            s3_config_->request_timeout = timeout;
        } else if (azure_config_.has_value()) {
            azure_config_->request_timeout = timeout;
        } else if (gcs_config_.has_value()) {
            gcs_config_->request_timeout = timeout;
        }
        return *this;
    }

    auto with_connection_pool_size(std::size_t size) -> cloud_config_builder& {
        if (s3_config_.has_value()) {
            s3_config_->connection_pool_size = size;
        } else if (azure_config_.has_value()) {
            azure_config_->connection_pool_size = size;
        } else if (gcs_config_.has_value()) {
            gcs_config_->connection_pool_size = size;
        }
        return *this;
    }

    auto with_retry_policy(const cloud_retry_policy& policy) -> cloud_config_builder& {
        if (s3_config_.has_value()) {
            s3_config_->retry = policy;
        } else if (azure_config_.has_value()) {
            azure_config_->retry = policy;
        } else if (gcs_config_.has_value()) {
            gcs_config_->retry = policy;
        }
        return *this;
    }

    auto with_multipart(const multipart_config& config) -> cloud_config_builder& {
        if (s3_config_.has_value()) {
            s3_config_->multipart = config;
        } else if (azure_config_.has_value()) {
            azure_config_->multipart = config;
        } else if (gcs_config_.has_value()) {
            gcs_config_->multipart = config;
        }
        return *this;
    }

    // S3-specific options
    auto with_transfer_acceleration(bool enable) -> cloud_config_builder& {
        if (s3_config_.has_value()) {
            s3_config_->use_transfer_acceleration = enable;
        }
        return *this;
    }

    auto with_dualstack(bool enable) -> cloud_config_builder& {
        if (s3_config_.has_value()) {
            s3_config_->use_dualstack = enable;
        }
        return *this;
    }

    // Azure-specific options
    auto with_account_name(const std::string& name) -> cloud_config_builder& {
        if (azure_config_.has_value()) {
            azure_config_->account_name = name;
        }
        return *this;
    }

    auto with_access_tier(const std::string& tier) -> cloud_config_builder& {
        if (azure_config_.has_value()) {
            azure_config_->access_tier = tier;
        }
        return *this;
    }

    // GCS-specific options
    auto with_project_id(const std::string& project_id) -> cloud_config_builder& {
        if (gcs_config_.has_value()) {
            gcs_config_->project_id = project_id;
        }
        return *this;
    }

    auto with_uniform_bucket_level_access(bool enable) -> cloud_config_builder& {
        if (gcs_config_.has_value()) {
            gcs_config_->uniform_bucket_level_access = enable;
        }
        return *this;
    }

    /**
     * @brief Build S3 configuration
     * @return S3 configuration
     */
    [[nodiscard]] auto build_s3() const -> s3_config {
        if (s3_config_.has_value()) {
            return s3_config_.value();
        }
        return s3_config{};
    }

    /**
     * @brief Build Azure Blob configuration
     * @return Azure Blob configuration
     */
    [[nodiscard]] auto build_azure_blob() const -> azure_blob_config {
        if (azure_config_.has_value()) {
            return azure_config_.value();
        }
        return azure_blob_config{};
    }

    /**
     * @brief Build GCS configuration
     * @return GCS configuration
     */
    [[nodiscard]] auto build_gcs() const -> gcs_config {
        if (gcs_config_.has_value()) {
            return gcs_config_.value();
        }
        return gcs_config{};
    }

private:
    std::optional<s3_config> s3_config_;
    std::optional<azure_blob_config> azure_config_;
    std::optional<gcs_config> gcs_config_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CLOUD_CLOUD_CONFIG_H
