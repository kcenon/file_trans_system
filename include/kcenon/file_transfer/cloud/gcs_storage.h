/**
 * @file gcs_storage.h
 * @brief Google Cloud Storage backend implementation
 * @version 0.1.0
 *
 * This file implements the Google Cloud Storage backend conforming to the
 * cloud storage abstraction interface.
 */

#ifndef KCENON_FILE_TRANSFER_CLOUD_GCS_STORAGE_H
#define KCENON_FILE_TRANSFER_CLOUD_GCS_STORAGE_H

#include <atomic>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cloud_config.h"
#include "cloud_credentials.h"
#include "cloud_error.h"
#include "cloud_storage_interface.h"

namespace kcenon::file_transfer {

/**
 * @brief HTTP response structure for GCS HTTP client interface
 */
struct gcs_http_response {
    int status_code = 0;
    std::map<std::string, std::string> headers;
    std::vector<uint8_t> body;

    [[nodiscard]] auto get_body_string() const -> std::string {
        return std::string(body.begin(), body.end());
    }
};

/**
 * @brief HTTP client interface for GCS operations
 *
 * This interface allows for dependency injection of HTTP clients,
 * enabling mock implementations for testing.
 */
class gcs_http_client_interface {
public:
    virtual ~gcs_http_client_interface() = default;

    /**
     * @brief Perform HTTP GET request
     */
    [[nodiscard]] virtual auto get(
        const std::string& url,
        const std::map<std::string, std::string>& query,
        const std::map<std::string, std::string>& headers)
        -> result<gcs_http_response> = 0;

    /**
     * @brief Perform HTTP POST request with binary body
     */
    [[nodiscard]] virtual auto post(
        const std::string& url,
        const std::vector<uint8_t>& body,
        const std::map<std::string, std::string>& headers)
        -> result<gcs_http_response> = 0;

    /**
     * @brief Perform HTTP POST request with string body
     */
    [[nodiscard]] virtual auto post(
        const std::string& url,
        const std::string& body,
        const std::map<std::string, std::string>& headers)
        -> result<gcs_http_response> = 0;

    /**
     * @brief Perform HTTP DELETE request
     */
    [[nodiscard]] virtual auto del(
        const std::string& url,
        const std::map<std::string, std::string>& headers)
        -> result<gcs_http_response> = 0;
};

/**
 * @brief GCS upload stream implementation for resumable uploads
 *
 * Implements streaming upload using GCS Resumable Upload API.
 * Large files are uploaded in chunks with resume capability.
 */
class gcs_upload_stream : public cloud_upload_stream {
public:
    ~gcs_upload_stream() override;

    gcs_upload_stream(const gcs_upload_stream&) = delete;
    auto operator=(const gcs_upload_stream&) -> gcs_upload_stream& = delete;
    gcs_upload_stream(gcs_upload_stream&&) noexcept;
    auto operator=(gcs_upload_stream&&) noexcept -> gcs_upload_stream&;

    [[nodiscard]] auto write(
        std::span<const std::byte> data) -> result<std::size_t> override;

    [[nodiscard]] auto finalize() -> result<upload_result> override;

    [[nodiscard]] auto abort() -> result<void> override;

    [[nodiscard]] auto bytes_written() const -> uint64_t override;

    [[nodiscard]] auto upload_id() const -> std::optional<std::string> override;

private:
    friend class gcs_storage;

    gcs_upload_stream(
        const std::string& object_name,
        const gcs_config& config,
        std::shared_ptr<credential_provider> credentials,
        const cloud_transfer_options& options);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief GCS download stream implementation
 *
 * Implements streaming download from Google Cloud Storage.
 */
class gcs_download_stream : public cloud_download_stream {
public:
    ~gcs_download_stream() override;

    gcs_download_stream(const gcs_download_stream&) = delete;
    auto operator=(const gcs_download_stream&) -> gcs_download_stream& = delete;
    gcs_download_stream(gcs_download_stream&&) noexcept;
    auto operator=(gcs_download_stream&&) noexcept -> gcs_download_stream&;

    [[nodiscard]] auto read(
        std::span<std::byte> buffer) -> result<std::size_t> override;

    [[nodiscard]] auto has_more() const -> bool override;

    [[nodiscard]] auto bytes_read() const -> uint64_t override;

    [[nodiscard]] auto total_size() const -> uint64_t override;

    [[nodiscard]] auto metadata() const -> const cloud_object_metadata& override;

private:
    friend class gcs_storage;

    gcs_download_stream(
        const std::string& object_name,
        const gcs_config& config,
        std::shared_ptr<credential_provider> credentials);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief Google Cloud Storage backend
 *
 * Implements the cloud_storage_interface for Google Cloud Storage.
 *
 * Features:
 * - Standard object operations (PUT, GET, LIST, DELETE)
 * - Resumable uploads for large files
 * - Signed URLs for limited access
 * - Storage class support (Standard, Nearline, Coldline, Archive)
 * - Service account authentication
 * - Application default credentials
 *
 * @code
 * // Create GCS storage instance
 * auto config = cloud_config_builder::gcs()
 *     .with_project_id("my-project")
 *     .with_bucket("my-bucket")
 *     .build_gcs();
 *
 * gcs_credentials creds;
 * creds.service_account_file = "/path/to/service-account.json";
 *
 * auto provider = gcs_credential_provider::create(creds);
 * auto storage = gcs_storage::create(config, provider);
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
class gcs_storage : public cloud_storage_interface {
public:
    /**
     * @brief Create a GCS storage instance
     * @param config GCS configuration
     * @param credentials Credential provider
     * @return Unique pointer to storage instance, or nullptr on failure
     */
    [[nodiscard]] static auto create(
        const gcs_config& config,
        std::shared_ptr<credential_provider> credentials) -> std::unique_ptr<gcs_storage>;

    /**
     * @brief Create a GCS storage instance with custom HTTP client
     * @param config GCS configuration
     * @param credentials Credential provider
     * @param http_client Custom HTTP client (for testing)
     * @return Unique pointer to storage instance, or nullptr on failure
     */
    [[nodiscard]] static auto create(
        const gcs_config& config,
        std::shared_ptr<credential_provider> credentials,
        std::shared_ptr<gcs_http_client_interface> http_client) -> std::unique_ptr<gcs_storage>;

    ~gcs_storage() override;

    gcs_storage(const gcs_storage&) = delete;
    auto operator=(const gcs_storage&) -> gcs_storage& = delete;
    gcs_storage(gcs_storage&&) noexcept;
    auto operator=(gcs_storage&&) noexcept -> gcs_storage&;

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
    // Presigned URLs (Signed URLs)
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
    // GCS-specific Methods
    // ========================================================================

    /**
     * @brief Get the GCS-specific configuration
     * @return GCS configuration reference
     */
    [[nodiscard]] auto get_gcs_config() const -> const gcs_config&;

    /**
     * @brief Get the project ID
     * @return Project ID
     */
    [[nodiscard]] auto project_id() const -> std::string_view;

    /**
     * @brief Get the effective endpoint URL
     * @return Endpoint URL (custom or default GCS endpoint)
     */
    [[nodiscard]] auto endpoint_url() const -> std::string;

    /**
     * @brief Set object storage class
     * @param key Object key
     * @param storage_class Storage class (STANDARD, NEARLINE, COLDLINE, ARCHIVE)
     * @return Result indicating success or error
     */
    [[nodiscard]] auto set_storage_class(
        const std::string& key,
        const std::string& storage_class) -> result<void>;

    /**
     * @brief Get object storage class
     * @param key Object key
     * @return Result containing storage class or error
     */
    [[nodiscard]] auto get_storage_class(
        const std::string& key) -> result<std::string>;

    /**
     * @brief Generate signed URL for an object
     * @param key Object key
     * @param options Signed URL options
     * @return Result containing signed URL or error
     */
    [[nodiscard]] auto generate_signed_url(
        const std::string& key,
        const presigned_url_options& options = {}) -> result<std::string>;

    /**
     * @brief Compose multiple objects into one
     * @param source_keys Source object keys to compose
     * @param dest_key Destination object key
     * @param options Transfer options
     * @return Result containing metadata of composed object or error
     */
    [[nodiscard]] auto compose_objects(
        const std::vector<std::string>& source_keys,
        const std::string& dest_key,
        const cloud_transfer_options& options = {}) -> result<cloud_object_metadata>;

private:
    explicit gcs_storage(
        const gcs_config& config,
        std::shared_ptr<credential_provider> credentials);

    gcs_storage(
        const gcs_config& config,
        std::shared_ptr<credential_provider> credentials,
        std::shared_ptr<gcs_http_client_interface> http_client);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief GCS credential provider implementation
 *
 * Provides credentials for Google Cloud Storage operations with support for:
 * - Service account JSON file authentication
 * - Service account JSON content authentication
 * - Application Default Credentials (ADC)
 * - Environment variable credentials
 */
class gcs_credential_provider : public credential_provider {
public:
    /**
     * @brief Create from GCS credentials
     * @param creds GCS credentials
     * @return Credential provider instance
     */
    [[nodiscard]] static auto create(
        const gcs_credentials& creds) -> std::unique_ptr<gcs_credential_provider>;

    /**
     * @brief Create from service account JSON file
     * @param json_file_path Path to service account JSON file
     * @return Credential provider instance or nullptr if invalid
     */
    [[nodiscard]] static auto create_from_service_account_file(
        const std::string& json_file_path) -> std::unique_ptr<gcs_credential_provider>;

    /**
     * @brief Create from service account JSON content
     * @param json_content Service account JSON content
     * @return Credential provider instance or nullptr if invalid
     */
    [[nodiscard]] static auto create_from_service_account_json(
        const std::string& json_content) -> std::unique_ptr<gcs_credential_provider>;

    /**
     * @brief Create from environment variables
     *
     * Looks for GOOGLE_APPLICATION_CREDENTIALS or
     * GOOGLE_CLOUD_PROJECT / GCLOUD_PROJECT
     *
     * @return Credential provider instance or nullptr if not found
     */
    [[nodiscard]] static auto create_from_environment()
        -> std::unique_ptr<gcs_credential_provider>;

    /**
     * @brief Create with automatic credential discovery (Application Default Credentials)
     *
     * Attempts to find credentials in the following order:
     * 1. GOOGLE_APPLICATION_CREDENTIALS environment variable
     * 2. User credentials from gcloud CLI
     * 3. Compute Engine / GKE metadata server
     *
     * @param project_id Project ID (optional, auto-detected if not provided)
     * @return Credential provider instance or nullptr if not found
     */
    [[nodiscard]] static auto create_default(
        const std::string& project_id = "") -> std::unique_ptr<gcs_credential_provider>;

    ~gcs_credential_provider() override;

    gcs_credential_provider(const gcs_credential_provider&) = delete;
    auto operator=(const gcs_credential_provider&) -> gcs_credential_provider& = delete;
    gcs_credential_provider(gcs_credential_provider&&) noexcept;
    auto operator=(gcs_credential_provider&&) noexcept -> gcs_credential_provider&;

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

    // ========================================================================
    // GCS-specific Methods
    // ========================================================================

    /**
     * @brief Get the project ID
     * @return Project ID
     */
    [[nodiscard]] auto project_id() const -> std::string;

    /**
     * @brief Get the service account email
     * @return Service account email or empty if not using service account
     */
    [[nodiscard]] auto service_account_email() const -> std::string;

    /**
     * @brief Get the authentication type being used
     * @return Authentication type description
     */
    [[nodiscard]] auto auth_type() const -> std::string_view;

    /**
     * @brief Get OAuth2 access token
     * @return Access token or empty if not available
     */
    [[nodiscard]] auto access_token() const -> std::string;

private:
    explicit gcs_credential_provider(const gcs_credentials& creds);
    gcs_credential_provider(credential_type type, const std::string& project_id);

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CLOUD_GCS_STORAGE_H
