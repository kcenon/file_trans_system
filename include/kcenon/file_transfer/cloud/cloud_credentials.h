/**
 * @file cloud_credentials.h
 * @brief Cloud storage credential management
 * @version 0.1.0
 *
 * This file defines credential structures and management interfaces
 * for different cloud storage providers.
 */

#ifndef KCENON_FILE_TRANSFER_CLOUD_CLOUD_CREDENTIALS_H
#define KCENON_FILE_TRANSFER_CLOUD_CLOUD_CREDENTIALS_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kcenon::file_transfer {

/**
 * @brief Cloud provider enumeration
 */
enum class cloud_provider {
    aws_s3,         ///< Amazon Web Services S3
    azure_blob,     ///< Microsoft Azure Blob Storage
    google_cloud,   ///< Google Cloud Storage
    custom          ///< Custom S3-compatible provider
};

/**
 * @brief Convert cloud_provider to string
 */
[[nodiscard]] constexpr auto to_string(cloud_provider provider) -> const char* {
    switch (provider) {
        case cloud_provider::aws_s3: return "aws-s3";
        case cloud_provider::azure_blob: return "azure-blob";
        case cloud_provider::google_cloud: return "google-cloud";
        case cloud_provider::custom: return "custom";
        default: return "unknown";
    }
}

/**
 * @brief Credential type enumeration
 */
enum class credential_type {
    static_credentials,     ///< Static access key and secret
    iam_role,              ///< IAM role (AWS)
    managed_identity,      ///< Managed identity (Azure)
    service_account,       ///< Service account (GCP)
    environment,           ///< Environment variables
    profile,               ///< Profile from config file
    assume_role,           ///< Assume role with STS
    web_identity,          ///< Web identity federation
    shared_credentials     ///< Shared credentials file
};

/**
 * @brief Convert credential_type to string
 */
[[nodiscard]] constexpr auto to_string(credential_type type) -> const char* {
    switch (type) {
        case credential_type::static_credentials: return "static-credentials";
        case credential_type::iam_role: return "iam-role";
        case credential_type::managed_identity: return "managed-identity";
        case credential_type::service_account: return "service-account";
        case credential_type::environment: return "environment";
        case credential_type::profile: return "profile";
        case credential_type::assume_role: return "assume-role";
        case credential_type::web_identity: return "web-identity";
        case credential_type::shared_credentials: return "shared-credentials";
        default: return "unknown";
    }
}

/**
 * @brief Credential state enumeration
 */
enum class credential_state {
    uninitialized,  ///< Not initialized
    valid,          ///< Credentials are valid
    expired,        ///< Credentials have expired
    invalid,        ///< Credentials are invalid
    refreshing      ///< Currently refreshing credentials
};

/**
 * @brief Convert credential_state to string
 */
[[nodiscard]] constexpr auto to_string(credential_state state) -> const char* {
    switch (state) {
        case credential_state::uninitialized: return "uninitialized";
        case credential_state::valid: return "valid";
        case credential_state::expired: return "expired";
        case credential_state::invalid: return "invalid";
        case credential_state::refreshing: return "refreshing";
        default: return "unknown";
    }
}

/**
 * @brief Base credential structure
 *
 * This is the base class for all credential types.
 */
struct cloud_credentials {
    credential_type type = credential_type::static_credentials;

    /// Optional session token (for temporary credentials)
    std::optional<std::string> session_token;

    /// Credential expiration time (for temporary credentials)
    std::optional<std::chrono::system_clock::time_point> expiration;

    /// Region for the credentials (if applicable)
    std::optional<std::string> region;

    virtual ~cloud_credentials() = default;

    /**
     * @brief Check if credentials have expired
     * @return true if expired, false otherwise
     */
    [[nodiscard]] auto is_expired() const -> bool {
        if (!expiration.has_value()) {
            return false;
        }
        return std::chrono::system_clock::now() >= expiration.value();
    }

    /**
     * @brief Get time until expiration
     * @return Duration until expiration, or nullopt if no expiration
     */
    [[nodiscard]] auto time_until_expiration() const
        -> std::optional<std::chrono::seconds> {
        if (!expiration.has_value()) {
            return std::nullopt;
        }
        auto now = std::chrono::system_clock::now();
        if (now >= expiration.value()) {
            return std::chrono::seconds{0};
        }
        return std::chrono::duration_cast<std::chrono::seconds>(
            expiration.value() - now);
    }
};

/**
 * @brief Static credentials (access key + secret)
 *
 * Used for AWS S3 and S3-compatible storage.
 */
struct static_credentials : cloud_credentials {
    static_credentials() {
        type = credential_type::static_credentials;
    }

    /// Access key ID
    std::string access_key_id;

    /// Secret access key
    std::string secret_access_key;
};

/**
 * @brief Azure Blob Storage credentials
 */
struct azure_credentials : cloud_credentials {
    azure_credentials() {
        type = credential_type::static_credentials;
    }

    /// Storage account name
    std::string account_name;

    /// Account access key (for access key auth)
    std::optional<std::string> account_key;

    /// Connection string (alternative to account name + key)
    std::optional<std::string> connection_string;

    /// SAS token (for shared access signature auth)
    std::optional<std::string> sas_token;

    /// Tenant ID (for AAD auth)
    std::optional<std::string> tenant_id;

    /// Client ID (for AAD auth)
    std::optional<std::string> client_id;

    /// Client secret (for AAD auth)
    std::optional<std::string> client_secret;
};

/**
 * @brief Google Cloud Storage credentials
 */
struct gcs_credentials : cloud_credentials {
    gcs_credentials() {
        type = credential_type::service_account;
    }

    /// Path to service account JSON file
    std::optional<std::string> service_account_file;

    /// Service account JSON content (alternative to file path)
    std::optional<std::string> service_account_json;

    /// Project ID
    std::optional<std::string> project_id;
};

/**
 * @brief Assume role credentials (AWS STS)
 */
struct assume_role_credentials : cloud_credentials {
    assume_role_credentials() {
        type = credential_type::assume_role;
    }

    /// Role ARN to assume
    std::string role_arn;

    /// Session name for the assumed role
    std::string role_session_name;

    /// Duration for the session (seconds)
    std::chrono::seconds duration{3600};

    /// External ID (if required by trust policy)
    std::optional<std::string> external_id;

    /// Serial number of MFA device
    std::optional<std::string> mfa_serial;

    /// Source credentials for assuming the role
    std::optional<static_credentials> source_credentials;
};

/**
 * @brief Web identity federation credentials
 */
struct web_identity_credentials : cloud_credentials {
    web_identity_credentials() {
        type = credential_type::web_identity;
    }

    /// Role ARN to assume
    std::string role_arn;

    /// Web identity token (JWT)
    std::string web_identity_token;

    /// Token file path (alternative to token string)
    std::optional<std::string> web_identity_token_file;

    /// Session name
    std::optional<std::string> role_session_name;

    /// Duration for the session
    std::chrono::seconds duration{3600};
};

/**
 * @brief Profile-based credentials
 *
 * Load credentials from a profile in configuration file.
 */
struct profile_credentials : cloud_credentials {
    profile_credentials() {
        type = credential_type::profile;
    }

    /// Profile name
    std::string profile_name = "default";

    /// Path to credentials file (optional, uses default if not set)
    std::optional<std::string> credentials_file;

    /// Path to config file (optional, uses default if not set)
    std::optional<std::string> config_file;
};

/**
 * @brief Credential provider interface
 *
 * Provides an abstraction for credential retrieval and refresh.
 * Implementations can support different credential sources.
 */
class credential_provider {
public:
    virtual ~credential_provider() = default;

    // Non-copyable
    credential_provider(const credential_provider&) = delete;
    auto operator=(const credential_provider&) -> credential_provider& = delete;

    // Movable
    credential_provider(credential_provider&&) noexcept = default;
    auto operator=(credential_provider&&) noexcept -> credential_provider& = default;

    /**
     * @brief Get the provider type
     * @return Cloud provider type
     */
    [[nodiscard]] virtual auto provider() const -> cloud_provider = 0;

    /**
     * @brief Get current credentials
     * @return Current credentials or nullptr if unavailable
     */
    [[nodiscard]] virtual auto get_credentials() const
        -> std::shared_ptr<const cloud_credentials> = 0;

    /**
     * @brief Refresh credentials
     * @return true if refresh succeeded, false otherwise
     */
    [[nodiscard]] virtual auto refresh() -> bool = 0;

    /**
     * @brief Check if credentials need refresh
     * @param buffer Time buffer before expiration to trigger refresh
     * @return true if refresh is needed, false otherwise
     */
    [[nodiscard]] virtual auto needs_refresh(
        std::chrono::seconds buffer = std::chrono::seconds{300}) const -> bool = 0;

    /**
     * @brief Get current credential state
     * @return Current state
     */
    [[nodiscard]] virtual auto state() const -> credential_state = 0;

    /**
     * @brief Set callback for credential state changes
     * @param callback Function called when state changes
     */
    virtual void on_state_changed(
        std::function<void(credential_state)> callback) = 0;

    /**
     * @brief Enable automatic refresh
     * @param enable true to enable, false to disable
     * @param check_interval How often to check for refresh need
     */
    virtual void set_auto_refresh(
        bool enable,
        std::chrono::seconds check_interval = std::chrono::seconds{60}) = 0;

protected:
    credential_provider() = default;
};

/**
 * @brief Credential provider factory
 *
 * Creates credential provider instances for different cloud providers.
 */
class credential_provider_factory {
public:
    virtual ~credential_provider_factory() = default;

    /**
     * @brief Create a static credential provider
     * @param creds Static credentials
     * @return Credential provider instance
     */
    [[nodiscard]] virtual auto create_static(
        const static_credentials& creds) -> std::unique_ptr<credential_provider> = 0;

    /**
     * @brief Create an Azure credential provider
     * @param creds Azure credentials
     * @return Credential provider instance
     */
    [[nodiscard]] virtual auto create_azure(
        const azure_credentials& creds) -> std::unique_ptr<credential_provider> = 0;

    /**
     * @brief Create a GCS credential provider
     * @param creds GCS credentials
     * @return Credential provider instance
     */
    [[nodiscard]] virtual auto create_gcs(
        const gcs_credentials& creds) -> std::unique_ptr<credential_provider> = 0;

    /**
     * @brief Create an assume role credential provider
     * @param creds Assume role credentials
     * @return Credential provider instance
     */
    [[nodiscard]] virtual auto create_assume_role(
        const assume_role_credentials& creds) -> std::unique_ptr<credential_provider> = 0;

    /**
     * @brief Create a profile-based credential provider
     * @param creds Profile credentials
     * @return Credential provider instance
     */
    [[nodiscard]] virtual auto create_from_profile(
        const profile_credentials& creds) -> std::unique_ptr<credential_provider> = 0;

    /**
     * @brief Create a credential provider from environment variables
     * @param provider Target cloud provider
     * @return Credential provider instance
     */
    [[nodiscard]] virtual auto create_from_environment(
        cloud_provider provider) -> std::unique_ptr<credential_provider> = 0;

    /**
     * @brief Create a credential provider with automatic detection
     *
     * Attempts to find credentials in the following order:
     * 1. Environment variables
     * 2. Shared credentials file
     * 3. IAM role / managed identity / service account
     *
     * @param provider Target cloud provider
     * @return Credential provider instance or nullptr if not found
     */
    [[nodiscard]] virtual auto create_default(
        cloud_provider provider) -> std::unique_ptr<credential_provider> = 0;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CLOUD_CLOUD_CREDENTIALS_H
