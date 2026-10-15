#include <gtest/gtest.h>
#include <kcenon/file_transfer/cloud/azure_blob_storage.h>
#include <kcenon/file_transfer/cloud/gcs_storage.h>
#include <kcenon/file_transfer/cloud/s3_storage.h>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>

namespace kcenon::file_transfer {
namespace {
class scoped_environment {
public:
    explicit scoped_environment(std::initializer_list<const char*> names) {
        for (auto name : names) {
            const char* previous = std::getenv(name);
            saved_.emplace(name, previous ? std::optional<std::string>(previous) : std::nullopt);
            set(name, std::nullopt);
        }
    }
    ~scoped_environment() { for (const auto& [name, value] : saved_) set(name, value); }
    void set(const std::string& name, std::optional<std::string> value) {
#ifdef _WIN32
        _putenv_s(name.c_str(), value.value_or("").c_str());
#else
        if (value) setenv(name.c_str(), value->c_str(), 1);
        else unsetenv(name.c_str());
#endif
    }
private:
    std::map<std::string, std::optional<std::string>> saved_;
};

TEST(CredentialSourceTest, AwsEnvironmentRefreshReplacesSnapshotAndKeepsLastCredentialsOnFailure) {
    scoped_environment env{"AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY", "AWS_SESSION_TOKEN", "AWS_REGION", "AWS_DEFAULT_REGION"};
    EXPECT_FALSE(s3_credential_provider::create_from_environment());
    env.set("AWS_ACCESS_KEY_ID", "fixture-access"); env.set("AWS_SECRET_ACCESS_KEY", "fixture-secret");
    env.set("AWS_SESSION_TOKEN", "fixture-session"); env.set("AWS_DEFAULT_REGION", "us-east-1");
    auto provider = s3_credential_provider::create_default(); ASSERT_TRUE(provider);
    auto snapshot = std::dynamic_pointer_cast<const static_credentials>(provider->get_credentials()); ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->region, "us-east-1"); EXPECT_EQ(snapshot->session_token, "fixture-session");
    int changes = 0; provider->on_state_changed([&](auto state) { EXPECT_EQ(state, credential_state::valid); ++changes; });
    env.set("AWS_REGION", "eu-west-1"); env.set("AWS_SECRET_ACCESS_KEY", "rotated-fixture");
    ASSERT_TRUE(provider->refresh()); EXPECT_EQ(changes, 1);
    auto updated = std::dynamic_pointer_cast<const static_credentials>(provider->get_credentials()); ASSERT_TRUE(updated);
    EXPECT_EQ(updated->region, "eu-west-1"); EXPECT_EQ(updated->secret_access_key, "rotated-fixture");
    EXPECT_EQ(snapshot->secret_access_key, "fixture-secret");
    env.set("AWS_SECRET_ACCESS_KEY", std::nullopt); EXPECT_FALSE(provider->refresh());
    EXPECT_EQ(provider->get_credentials(), updated); EXPECT_EQ(changes, 1);
}

TEST(CredentialSourceTest, AwsProfilesSelectOnlyTheNamedSectionAndRejectIncompleteCredentials) {
    auto file = std::filesystem::temp_directory_path() / ("fts-profiles-" + transfer_id::generate().to_string());
    struct cleanup { std::filesystem::path file; ~cleanup() { std::error_code ec; std::filesystem::remove(file, ec); } } cleanup{file};
    { std::ofstream out(file); out << "# fixture\n; comment\n\n[default]\naws_access_key_id = wrong\naws_secret_access_key = wrong\n"
        "[selected]\n aws_access_key_id = selected-access \r\naws_secret_access_key = selected-secret\n"
        "aws_session_token = selected-token\nignored line\n[partial]\naws_access_key_id = incomplete\n"; }
    auto selected = s3_credential_provider::create_from_profile("selected", file.string()); ASSERT_TRUE(selected);
    auto credentials = std::dynamic_pointer_cast<const static_credentials>(selected->get_credentials()); ASSERT_TRUE(credentials);
    EXPECT_EQ(credentials->access_key_id, "selected-access"); EXPECT_EQ(credentials->secret_access_key, "selected-secret");
    EXPECT_EQ(credentials->session_token, "selected-token");
    EXPECT_FALSE(s3_credential_provider::create_from_profile("missing", file.string()));
    EXPECT_FALSE(s3_credential_provider::create_from_profile("partial", file.string()));
    EXPECT_FALSE(s3_credential_provider::create_from_profile("selected", file.string() + ".missing"));
}

TEST(CredentialSourceTest, AzureEnvironmentPrecedenceAndRefreshPreserveCredentialIdentity) {
    scoped_environment env{"AZURE_STORAGE_CONNECTION_STRING", "AZURE_STORAGE_ACCOUNT", "AZURE_STORAGE_KEY", "AZURE_STORAGE_SAS_TOKEN"};
    EXPECT_FALSE(azure_blob_credential_provider::create_from_environment());
    EXPECT_FALSE(azure_blob_credential_provider::create_default("fixture"));
    env.set("AZURE_STORAGE_ACCOUNT", "fixture"); env.set("AZURE_STORAGE_KEY", "dGVzdA==");
    auto provider = azure_blob_credential_provider::create_default("ignored"); ASSERT_TRUE(provider);
    EXPECT_EQ(provider->auth_type(), "environment"); EXPECT_EQ(provider->account_name(), "fixture");
    int changes = 0; provider->on_state_changed([&](auto state) { EXPECT_EQ(state, credential_state::valid); ++changes; });
    env.set("AZURE_STORAGE_KEY", "bmV3LXRlc3Q="); ASSERT_TRUE(provider->refresh()); EXPECT_EQ(changes, 1);
    auto credentials = std::dynamic_pointer_cast<const azure_credentials>(provider->get_credentials()); ASSERT_TRUE(credentials);
    EXPECT_EQ(credentials->account_key, "bmV3LXRlc3Q=");
    env.set("AZURE_STORAGE_KEY", std::nullopt); EXPECT_FALSE(provider->refresh());
    EXPECT_EQ(provider->get_credentials(), credentials);
    env.set("AZURE_STORAGE_SAS_TOKEN", "sv=fixture&sig=test-only");
    auto sas = azure_blob_credential_provider::create_from_environment(); ASSERT_TRUE(sas); EXPECT_EQ(sas->auth_type(), "sas-token");
    env.set("AZURE_STORAGE_CONNECTION_STRING", "AccountName=connection-fixture;SharedAccessSignature=sv=fixture&sig=test-only");
    auto connection = azure_blob_credential_provider::create_from_environment(); ASSERT_TRUE(connection);
    EXPECT_EQ(connection->account_name(), "connection-fixture"); EXPECT_EQ(connection->auth_type(), "connection-string");
    env.set("AZURE_STORAGE_CONNECTION_STRING", "invalid"); EXPECT_FALSE(azure_blob_credential_provider::create_from_environment());
}

TEST(CredentialSourceTest, GcsExplicitFileWinsOverProjectFallbackAndProjectRefreshIsObservable) {
    scoped_environment env{"GOOGLE_APPLICATION_CREDENTIALS", "GOOGLE_CLOUD_PROJECT", "GCLOUD_PROJECT"};
    EXPECT_FALSE(gcs_credential_provider::create_from_environment());
    env.set("GCLOUD_PROJECT", "fallback-project");
    auto provider = gcs_credential_provider::create_from_environment(); ASSERT_TRUE(provider);
    EXPECT_EQ(provider->project_id(), "fallback-project"); EXPECT_EQ(provider->auth_type(), "environment-metadata");
    int changes = 0; provider->on_state_changed([&](auto state) { EXPECT_EQ(state, credential_state::valid); ++changes; });
    env.set("GOOGLE_CLOUD_PROJECT", "preferred-project"); ASSERT_TRUE(provider->refresh());
    EXPECT_EQ(changes, 1); EXPECT_EQ(provider->project_id(), "preferred-project");
    env.set("GOOGLE_CLOUD_PROJECT", std::nullopt); env.set("GCLOUD_PROJECT", std::nullopt);
    EXPECT_FALSE(provider->refresh()); EXPECT_EQ(provider->project_id(), "preferred-project");
    auto fixture = std::filesystem::path(FILE_TRANS_TEST_DATA_DIR) / "gcs_signing/service-account.json";
    env.set("GOOGLE_APPLICATION_CREDENTIALS", fixture.string()); env.set("GOOGLE_CLOUD_PROJECT", "ignored");
    auto file = gcs_credential_provider::create_default(); ASSERT_TRUE(file);
    EXPECT_EQ(file->project_id(), "test-project"); EXPECT_EQ(file->auth_type(), "environment");
    EXPECT_EQ(file->service_account_email(), "test-signer@test-project.iam.gserviceaccount.com");
    gcs_credentials by_path; by_path.service_account_file = fixture.string();
    EXPECT_TRUE(gcs_credential_provider::create(by_path));
    by_path.service_account_file = fixture.string() + ".missing"; EXPECT_FALSE(gcs_credential_provider::create(by_path));
}

TEST(CredentialSourceTest, ExpiringCredentialsRequireRefreshInsideTheSafetyWindow) {
    using namespace std::chrono;
    static_credentials aws; aws.access_key_id = "fixture"; aws.secret_access_key = "fixture";
    azure_credentials azure; azure.account_name = "fixture"; azure.account_key = "dGVzdA==";
    gcs_credentials gcs; gcs.service_account_json = "{}";
    for (auto lifetime : {seconds(-1), seconds(30), seconds(3600)}) {
        auto expires = system_clock::now() + lifetime;
        aws.expiration = expires; azure.expiration = expires; gcs.expiration = expires;
        EXPECT_EQ(s3_credential_provider::create(aws)->needs_refresh(seconds(60)), lifetime <= seconds(60));
        EXPECT_EQ(azure_blob_credential_provider::create(azure)->needs_refresh(seconds(60)), lifetime <= seconds(60));
        EXPECT_EQ(gcs_credential_provider::create(gcs)->needs_refresh(seconds(60)), lifetime <= seconds(60));
    }
}
} // namespace
} // namespace kcenon::file_transfer
