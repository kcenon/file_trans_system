/**
 * @file s3_storage.h
 * @brief AWS S3 storage backend implementation
 * @version 0.1.0
 *
 * This file implements the AWS S3 storage backend conforming to the
 * cloud storage abstraction interface.
 */

#ifndef KCENON_FILE_TRANSFER_CLOUD_S3_STORAGE_H
#define KCENON_FILE_TRANSFER_CLOUD_S3_STORAGE_H

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

#include "cloud_config.h"
#include "cloud_credentials.h"
#include "cloud_error.h"
#include "cloud_storage_interface.h"

namespace kcenon::file_transfer {

/**
 * @brief S3 upload stream implementation for multipart uploads
 */
class s3_upload_stream : public cloud_upload_stream {
public:
    ~s3_upload_stream() override;

    s3_upload_stream(const s3_upload_stream&) = delete;
    auto operator=(const s3_upload_stream&) -> s3_upload_stream& = delete;
    s3_upload_stream(s3_upload_stream&&) noexcept;
    auto operator=(s3_upload_stream&&) noexcept -> s3_upload_stream&;

    [[nodiscard]] auto write(
        std::span<const std::byte> data) -> result<std::size_t> override;

    [[nodiscard]] auto finalize() -> result<upload_result> override;

    [[nodiscard]] auto abort() -> result<void> override;

    [[nodiscard]] auto bytes_written() const -> uint64_t override;

    [[nodiscard]] auto upload_id() const -> std::optional<std::string> override;

private:
    friend class s3_storage;

    s3_upload_stream(
        const std::string& key,
        const s3_config& config,
        std::shared_ptr<credential_provider> credentials,
        const cloud_transfer_options& options);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief S3 download stream implementation
 */
class s3_download_stream : public cloud_download_stream {
public:
    ~s3_download_stream() override;

    s3_download_stream(const s3_download_stream&) = delete;
    auto operator=(const s3_download_stream&) -> s3_download_stream& = delete;
    s3_download_stream(s3_download_stream&&) noexcept;
    auto operator=(s3_download_stream&&) noexcept -> s3_download_stream&;

    [[nodiscard]] auto read(
        std::span<std::byte> buffer) -> result<std::size_t> override;

    [[nodiscard]] auto has_more() const -> bool override;

    [[nodiscard]] auto bytes_read() const -> uint64_t override;

    [[nodiscard]] auto total_size() const -> uint64_t override;

    [[nodiscard]] auto metadata() const -> const cloud_object_metadata& override;

private:
    friend class s3_storage;

    s3_download_stream(
        const std::string& key,
        const s3_config& config,
        std::shared_ptr<credential_provider> credentials);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief AWS S3 storage backend
 *
 * Implements the cloud_storage_interface for AWS S3 and S3-compatible
 * storage providers (MinIO, DigitalOcean Spaces, etc.).
 *
 * Features:
 * - Standard S3 operations (PUT, GET, LIST, DELETE)
 * - Multipart uploads for large files
 * - Presigned URLs for direct access
 * - S3 Transfer Acceleration support
 * - Server-side encryption (SSE-S3, SSE-KMS)
 * - Custom S3-compatible endpoints
 *
 * @code
 * // Create S3 storage instance
 * auto config = cloud_config_builder::s3()
 *     .with_bucket("my-bucket")
 *     .with_region("us-east-1")
 *     .build_s3();
 *
 * static_credentials creds;
 * creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
 * creds.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
 *
 * auto provider = s3_credential_provider::create(creds);
 * auto storage = s3_storage::create(config, provider);
 *
 * if (storage) {
 *     auto result = storage->connect();
 *     if (result.has_value()) {
 *         // Upload a file
 *         auto upload = storage->upload_file("local/file.txt", "remote/file.txt");
 *     }
 * }
 * @endcode
 */
class s3_storage : public cloud_storage_interface {
public:
    /**
     * @brief Create an S3 storage instance
     * @param config S3 configuration
     * @param credentials Credential provider
     * @return Unique pointer to storage instance, or nullptr on failure
     */
    [[nodiscard]] static auto create(
        const s3_config& config,
        std::shared_ptr<credential_provider> credentials) -> std::unique_ptr<s3_storage>;

    ~s3_storage() override;

    s3_storage(const s3_storage&) = delete;
    auto operator=(const s3_storage&) -> s3_storage& = delete;
    s3_storage(s3_storage&&) noexcept;
    auto operator=(s3_storage&&) noexcept -> s3_storage&;

    // ========================================================================
    // Provider Info
    // ========================================================================

    [[nodiscard]] auto provider() const -> cloud_provider override;
    [[nodiscard]] auto provider_name() const -> std::string_view override;

    // ========================================================================
    // Connection Management
    // ========================================================================

    [[nodiscard]] auto connect() -> result<void> override;
    [[nodiscard]] auto disconnect() -> result<void> override;
    [[nodiscard]] auto is_connected() const -> bool override;
    [[nodiscard]] auto state() const -> cloud_storage_state override;

    // ========================================================================
    // Object Operations - Synchronous
    // ========================================================================

    [[nodiscard]] auto upload(
        const std::string& key,
        std::span<const std::byte> data,
        const cloud_transfer_options& options = {}) -> result<upload_result> override;

    [[nodiscard]] auto upload_file(
        const std::filesystem::path& local_path,
        const std::string& key,
        const cloud_transfer_options& options = {}) -> result<upload_result> override;

    [[nodiscard]] auto download(
        const std::string& key) -> result<std::vector<std::byte>> override;

    [[nodiscard]] auto download_file(
        const std::string& key,
        const std::filesystem::path& local_path) -> result<download_result> override;

    [[nodiscard]] auto delete_object(
        const std::string& key) -> result<delete_result> override;

    [[nodiscard]] auto delete_objects(
        const std::vector<std::string>& keys) -> result<std::vector<delete_result>> override;

    [[nodiscard]] auto exists(
        const std::string& key) -> result<bool> override;

    [[nodiscard]] auto get_metadata(
        const std::string& key) -> result<cloud_object_metadata> override;

    [[nodiscard]] auto list_objects(
        const list_objects_options& options = {}) -> result<list_objects_result> override;

    [[nodiscard]] auto copy_object(
        const std::string& source_key,
        const std::string& dest_key,
        const cloud_transfer_options& options = {}) -> result<cloud_object_metadata> override;

    // ========================================================================
    // Object Operations - Asynchronous
    // ========================================================================

    [[nodiscard]] auto upload_async(
        const std::string& key,
        std::span<const std::byte> data,
        const cloud_transfer_options& options = {}) -> std::future<result<upload_result>> override;

    [[nodiscard]] auto upload_file_async(
        const std::filesystem::path& local_path,
        const std::string& key,
        const cloud_transfer_options& options = {}) -> std::future<result<upload_result>> override;

    [[nodiscard]] auto download_async(
        const std::string& key) -> std::future<result<std::vector<std::byte>>> override;

    [[nodiscard]] auto download_file_async(
        const std::string& key,
        const std::filesystem::path& local_path) -> std::future<result<download_result>> override;

    // ========================================================================
    // Streaming Operations
    // ========================================================================

    [[nodiscard]] auto create_upload_stream(
        const std::string& key,
        const cloud_transfer_options& options = {}) -> std::unique_ptr<cloud_upload_stream> override;

    [[nodiscard]] auto create_download_stream(
        const std::string& key) -> std::unique_ptr<cloud_download_stream> override;

    // ========================================================================
    // Presigned URLs
    // ========================================================================

    [[nodiscard]] auto generate_presigned_url(
        const std::string& key,
        const presigned_url_options& options = {}) -> result<std::string> override;

    // ========================================================================
    // Progress Callbacks
    // ========================================================================

    void on_upload_progress(
        std::function<void(const upload_progress&)> callback) override;

    void on_download_progress(
        std::function<void(const download_progress&)> callback) override;

    void on_state_changed(
        std::function<void(cloud_storage_state)> callback) override;

    // ========================================================================
    // Statistics and Configuration
    // ========================================================================

    [[nodiscard]] auto get_statistics() const -> cloud_storage_statistics override;

    void reset_statistics() override;

    [[nodiscard]] auto config() const -> const cloud_storage_config& override;

    [[nodiscard]] auto bucket() const -> std::string_view override;

    [[nodiscard]] auto region() const -> std::string_view override;

    // ========================================================================
    // S3-specific Methods
    // ========================================================================

    /**
     * @brief Get the S3-specific configuration
     * @return S3 configuration reference
     */
    [[nodiscard]] auto get_s3_config() const -> const struct s3_config&;

    /**
     * @brief Get the effective endpoint URL
     * @return Endpoint URL (custom or default AWS endpoint)
     */
    [[nodiscard]] auto endpoint_url() const -> std::string;

    /**
     * @brief Check if Transfer Acceleration is enabled
     * @return true if enabled, false otherwise
     */
    [[nodiscard]] auto is_transfer_acceleration_enabled() const -> bool;

private:
    explicit s3_storage(
        const struct s3_config& config,
        std::shared_ptr<credential_provider> credentials);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief S3 credential provider implementation
 *
 * Provides credentials for AWS S3 operations with support for:
 * - Static credentials
 * - Environment variables
 * - AWS profile configuration
 * - IAM roles (when running on EC2/ECS/Lambda)
 */
class s3_credential_provider : public credential_provider {
public:
    /**
     * @brief Create from static credentials
     * @param creds Static credentials
     * @return Credential provider instance
     */
    [[nodiscard]] static auto create(
        const static_credentials& creds) -> std::unique_ptr<s3_credential_provider>;

    /**
     * @brief Create from environment variables
     *
     * Looks for AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY
     *
     * @return Credential provider instance or nullptr if not found
     */
    [[nodiscard]] static auto create_from_environment()
        -> std::unique_ptr<s3_credential_provider>;

    /**
     * @brief Create from AWS profile
     * @param profile_name Profile name (default: "default")
     * @param credentials_file Optional path to credentials file
     * @return Credential provider instance or nullptr if not found
     */
    [[nodiscard]] static auto create_from_profile(
        const std::string& profile_name = "default",
        const std::optional<std::string>& credentials_file = std::nullopt)
        -> std::unique_ptr<s3_credential_provider>;

    /**
     * @brief Create with automatic credential discovery
     *
     * Attempts to find credentials in the following order:
     * 1. Environment variables
     * 2. Shared credentials file (~/.aws/credentials)
     * 3. IAM role (EC2/ECS metadata service)
     *
     * @return Credential provider instance or nullptr if not found
     */
    [[nodiscard]] static auto create_default()
        -> std::unique_ptr<s3_credential_provider>;

    ~s3_credential_provider() override;

    s3_credential_provider(const s3_credential_provider&) = delete;
    auto operator=(const s3_credential_provider&) -> s3_credential_provider& = delete;
    s3_credential_provider(s3_credential_provider&&) noexcept;
    auto operator=(s3_credential_provider&&) noexcept -> s3_credential_provider&;

    [[nodiscard]] auto provider() const -> cloud_provider override;

    [[nodiscard]] auto get_credentials() const
        -> std::shared_ptr<const cloud_credentials> override;

    [[nodiscard]] auto refresh() -> bool override;

    [[nodiscard]] auto needs_refresh(
        std::chrono::seconds buffer = std::chrono::seconds{300}) const -> bool override;

    [[nodiscard]] auto state() const -> credential_state override;

    void on_state_changed(
        std::function<void(credential_state)> callback) override;

    void set_auto_refresh(
        bool enable,
        std::chrono::seconds check_interval = std::chrono::seconds{60}) override;

private:
    explicit s3_credential_provider(const static_credentials& creds);
    s3_credential_provider(credential_type type);

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CLOUD_S3_STORAGE_H
