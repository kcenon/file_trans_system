/**
 * @file azure_blob_storage.h
 * @brief Azure Blob Storage backend implementation
 * @version 0.1.0
 *
 * This file implements the Azure Blob Storage backend conforming to the
 * cloud storage abstraction interface.
 */

#ifndef KCENON_FILE_TRANSFER_CLOUD_AZURE_BLOB_STORAGE_H
#define KCENON_FILE_TRANSFER_CLOUD_AZURE_BLOB_STORAGE_H

#include <atomic>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "cloud_config.h"
#include "cloud_credentials.h"
#include "cloud_error.h"
#include "cloud_storage_interface.h"

namespace kcenon::file_transfer {

// ============================================================================
// Azure HTTP Client Interface (for dependency injection and testing)
// ============================================================================

/**
 * @brief HTTP response structure for Azure operations
 */
struct azure_http_response {
    int status_code = 0;
    std::map<std::string, std::string> headers;
    std::vector<uint8_t> body;

    [[nodiscard]] auto get_body_string() const -> std::string {
        return std::string(body.begin(), body.end());
    }

    [[nodiscard]] auto get_header(const std::string& name) const -> std::optional<std::string> {
        auto it = headers.find(name);
        if (it != headers.end()) {
            return it->second;
        }
        return std::nullopt;
    }
};

/**
 * @brief HTTP client interface for Azure operations
 *
 * This interface allows for dependency injection of HTTP clients,
 * enabling mock implementations for testing.
 */
class azure_http_client_interface {
public:
    virtual ~azure_http_client_interface() = default;

    /**
     * @brief Perform HTTP GET request
     */
    [[nodiscard]] virtual auto get(
        const std::string& url,
        const std::map<std::string, std::string>& query,
        const std::map<std::string, std::string>& headers)
        -> result<azure_http_response> = 0;

    /**
     * @brief Perform HTTP PUT request with string body
     */
    [[nodiscard]] virtual auto put(
        const std::string& url,
        const std::string& body,
        const std::map<std::string, std::string>& headers)
        -> result<azure_http_response> = 0;

    /**
     * @brief Perform HTTP PUT request with binary body
     */
    [[nodiscard]] virtual auto put(
        const std::string& url,
        const std::vector<uint8_t>& body,
        const std::map<std::string, std::string>& headers)
        -> result<azure_http_response> = 0;

    /**
     * @brief Perform HTTP DELETE request
     */
    [[nodiscard]] virtual auto del(
        const std::string& url,
        const std::map<std::string, std::string>& headers)
        -> result<azure_http_response> = 0;

    /**
     * @brief Perform HTTP HEAD request
     */
    [[nodiscard]] virtual auto head(
        const std::string& url,
        const std::map<std::string, std::string>& headers)
        -> result<azure_http_response> = 0;
};

/**
 * @brief Azure Blob upload stream implementation for block blobs
 *
 * Implements streaming upload using Azure Block Blob API.
 * Large files are uploaded as blocks and then committed.
 */
class azure_blob_upload_stream : public cloud_upload_stream {
public:
    ~azure_blob_upload_stream() override;

    azure_blob_upload_stream(const azure_blob_upload_stream&) = delete;
    auto operator=(const azure_blob_upload_stream&) -> azure_blob_upload_stream& = delete;
    azure_blob_upload_stream(azure_blob_upload_stream&&) noexcept;
    auto operator=(azure_blob_upload_stream&&) noexcept -> azure_blob_upload_stream&;

    [[nodiscard]] auto write(
        std::span<const std::byte> data) -> result<std::size_t> override;

    [[nodiscard]] auto finalize() -> result<upload_result> override;

    [[nodiscard]] auto abort() -> result<void> override;

    [[nodiscard]] auto bytes_written() const -> uint64_t override;

    [[nodiscard]] auto upload_id() const -> std::optional<std::string> override;

private:
    friend class azure_blob_storage;

    azure_blob_upload_stream(
        const std::string& blob_name,
        const azure_blob_config& config,
        std::shared_ptr<credential_provider> credentials,
        const cloud_transfer_options& options,
        std::shared_ptr<azure_http_client_interface> http_client = nullptr);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief Azure Blob download stream implementation
 *
 * Implements streaming download from Azure Blob Storage.
 */
class azure_blob_download_stream : public cloud_download_stream {
public:
    ~azure_blob_download_stream() override;

    azure_blob_download_stream(const azure_blob_download_stream&) = delete;
    auto operator=(const azure_blob_download_stream&) -> azure_blob_download_stream& = delete;
    azure_blob_download_stream(azure_blob_download_stream&&) noexcept;
    auto operator=(azure_blob_download_stream&&) noexcept -> azure_blob_download_stream&;

    [[nodiscard]] auto read(
        std::span<std::byte> buffer) -> result<std::size_t> override;

    [[nodiscard]] auto has_more() const -> bool override;

    [[nodiscard]] auto bytes_read() const -> uint64_t override;

    [[nodiscard]] auto total_size() const -> uint64_t override;

    [[nodiscard]] auto metadata() const -> const cloud_object_metadata& override;

private:
    friend class azure_blob_storage;

    azure_blob_download_stream(
        const std::string& blob_name,
        const azure_blob_config& config,
        std::shared_ptr<credential_provider> credentials);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief Azure Blob Storage backend
 *
 * Implements the cloud_storage_interface for Microsoft Azure Blob Storage.
 *
 * Features:
 * - Standard blob operations (PUT, GET, LIST, DELETE)
 * - Block blob uploads for large files
 * - SAS token generation for limited access
 * - Access tier support (Hot, Cool, Archive)
 * - Azure AD authentication
 * - Connection string authentication
 *
 * @code
 * // Create Azure Blob storage instance
 * auto config = cloud_config_builder::azure_blob()
 *     .with_account_name("mystorageaccount")
 *     .with_bucket("mycontainer")
 *     .build_azure_blob();
 *
 * azure_credentials creds;
 * creds.account_name = "mystorageaccount";
 * creds.account_key = "base64encodedkey...";
 *
 * auto provider = azure_blob_credential_provider::create(creds);
 * auto storage = azure_blob_storage::create(config, provider);
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
class azure_blob_storage : public cloud_storage_interface {
public:
    /**
     * @brief Create an Azure Blob storage instance
     * @param config Azure Blob configuration
     * @param credentials Credential provider
     * @return Unique pointer to storage instance, or nullptr on failure
     */
    [[nodiscard]] static auto create(
        const azure_blob_config& config,
        std::shared_ptr<credential_provider> credentials) -> std::unique_ptr<azure_blob_storage>;

    /**
     * @brief Create an Azure Blob storage instance with custom HTTP client
     * @param config Azure Blob configuration
     * @param credentials Credential provider
     * @param http_client Custom HTTP client (for testing)
     * @return Unique pointer to storage instance, or nullptr on failure
     */
    [[nodiscard]] static auto create(
        const azure_blob_config& config,
        std::shared_ptr<credential_provider> credentials,
        std::shared_ptr<azure_http_client_interface> http_client) -> std::unique_ptr<azure_blob_storage>;

    ~azure_blob_storage() override;

    azure_blob_storage(const azure_blob_storage&) = delete;
    auto operator=(const azure_blob_storage&) -> azure_blob_storage& = delete;
    azure_blob_storage(azure_blob_storage&&) noexcept;
    auto operator=(azure_blob_storage&&) noexcept -> azure_blob_storage&;

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
    // Presigned URLs (SAS Tokens)
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
    // Azure-specific Methods
    // ========================================================================

    /**
     * @brief Get the Azure Blob-specific configuration
     * @return Azure Blob configuration reference
     */
    [[nodiscard]] auto get_azure_config() const -> const azure_blob_config&;

    /**
     * @brief Get the container name
     * @return Container name
     */
    [[nodiscard]] auto container() const -> std::string_view;

    /**
     * @brief Get the storage account name
     * @return Storage account name
     */
    [[nodiscard]] auto account_name() const -> std::string_view;

    /**
     * @brief Get the effective endpoint URL
     * @return Endpoint URL (custom or default Azure endpoint)
     */
    [[nodiscard]] auto endpoint_url() const -> std::string;

    /**
     * @brief Set blob access tier
     * @param key Blob key
     * @param tier Access tier (Hot, Cool, Archive)
     * @return Result indicating success or error
     */
    [[nodiscard]] auto set_access_tier(
        const std::string& key,
        const std::string& tier) -> result<void>;

    /**
     * @brief Get blob access tier
     * @param key Blob key
     * @return Result containing access tier or error
     */
    [[nodiscard]] auto get_access_tier(
        const std::string& key) -> result<std::string>;

    /**
     * @brief Generate SAS token for the container
     * @param options SAS token options
     * @return Result containing SAS token or error
     */
    [[nodiscard]] auto generate_container_sas(
        const presigned_url_options& options = {}) -> result<std::string>;

    /**
     * @brief Generate SAS token for a specific blob
     * @param key Blob key
     * @param options SAS token options
     * @return Result containing SAS token or error
     */
    [[nodiscard]] auto generate_blob_sas(
        const std::string& key,
        const presigned_url_options& options = {}) -> result<std::string>;

private:
    explicit azure_blob_storage(
        const azure_blob_config& config,
        std::shared_ptr<credential_provider> credentials);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief Azure Blob credential provider implementation
 *
 * Provides credentials for Azure Blob Storage operations with support for:
 * - Account key authentication
 * - Connection string authentication
 * - SAS token authentication
 * - Azure AD authentication (client credentials)
 * - Managed identity (when running on Azure)
 */
class azure_blob_credential_provider : public credential_provider {
public:
    /**
     * @brief Create from Azure credentials
     * @param creds Azure credentials
     * @return Credential provider instance
     */
    [[nodiscard]] static auto create(
        const azure_credentials& creds) -> std::unique_ptr<azure_blob_credential_provider>;

    /**
     * @brief Create from connection string
     * @param connection_string Azure storage connection string
     * @return Credential provider instance or nullptr if invalid
     */
    [[nodiscard]] static auto create_from_connection_string(
        const std::string& connection_string) -> std::unique_ptr<azure_blob_credential_provider>;

    /**
     * @brief Create from environment variables
     *
     * Looks for AZURE_STORAGE_ACCOUNT, AZURE_STORAGE_KEY or
     * AZURE_STORAGE_CONNECTION_STRING
     *
     * @return Credential provider instance or nullptr if not found
     */
    [[nodiscard]] static auto create_from_environment()
        -> std::unique_ptr<azure_blob_credential_provider>;

    /**
     * @brief Create from SAS token
     * @param account_name Storage account name
     * @param sas_token SAS token (without leading '?')
     * @return Credential provider instance
     */
    [[nodiscard]] static auto create_from_sas_token(
        const std::string& account_name,
        const std::string& sas_token) -> std::unique_ptr<azure_blob_credential_provider>;

    /**
     * @brief Create from Azure AD client credentials
     * @param tenant_id Azure AD tenant ID
     * @param client_id Azure AD client (application) ID
     * @param client_secret Azure AD client secret
     * @param account_name Storage account name
     * @return Credential provider instance
     */
    [[nodiscard]] static auto create_from_client_credentials(
        const std::string& tenant_id,
        const std::string& client_id,
        const std::string& client_secret,
        const std::string& account_name) -> std::unique_ptr<azure_blob_credential_provider>;

    /**
     * @brief Create with automatic credential discovery
     *
     * Attempts to find credentials in the following order:
     * 1. Environment variables
     * 2. Managed identity (when running on Azure)
     * 3. Azure CLI credentials
     *
     * @param account_name Storage account name
     * @return Credential provider instance or nullptr if not found
     */
    [[nodiscard]] static auto create_default(
        const std::string& account_name) -> std::unique_ptr<azure_blob_credential_provider>;

    ~azure_blob_credential_provider() override;

    azure_blob_credential_provider(const azure_blob_credential_provider&) = delete;
    auto operator=(const azure_blob_credential_provider&) -> azure_blob_credential_provider& = delete;
    azure_blob_credential_provider(azure_blob_credential_provider&&) noexcept;
    auto operator=(azure_blob_credential_provider&&) noexcept -> azure_blob_credential_provider&;

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
    // Azure-specific Methods
    // ========================================================================

    /**
     * @brief Get the storage account name
     * @return Storage account name
     */
    [[nodiscard]] auto account_name() const -> std::string;

    /**
     * @brief Get the authentication type being used
     * @return Authentication type description
     */
    [[nodiscard]] auto auth_type() const -> std::string_view;

private:
    explicit azure_blob_credential_provider(const azure_credentials& creds);
    azure_blob_credential_provider(credential_type type, const std::string& account_name);

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CLOUD_AZURE_BLOB_STORAGE_H
