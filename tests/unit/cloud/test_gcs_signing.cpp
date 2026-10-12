#include <gtest/gtest.h>
#include <kcenon/file_transfer/cloud/gcs_storage.h>
#include <kcenon/file_transfer/cloud/s3_storage.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <fstream>

namespace kcenon::file_transfer {
namespace {
auto fixture(const std::string& name) -> std::string {
    std::ifstream file(std::filesystem::path(FILE_TRANS_TEST_DATA_DIR) / "gcs_signing" / name, std::ios::binary);
    if (!file) throw std::runtime_error("Missing signing fixture: " + name);
    return {std::istreambuf_iterator<char>{file}, {}};
}
auto fixed_time() -> std::chrono::system_clock::time_point {
    using namespace std::chrono;
    return sys_days{year{2025}/1/2} + hours{3} + minutes{4} + seconds{5};
}
auto signer(std::string document = fixture("service-account.json")) -> std::unique_ptr<gcs_storage> {
    gcs_credentials credentials; credentials.service_account_json = std::move(document);
    auto config = cloud_config_builder::gcs().with_bucket("test-bucket").with_project_id("test-project").build_gcs();
    return gcs_storage::create(config, gcs_credential_provider::create(credentials));
}
}

TEST(GcsSigningTest, GetAndPutMatchIndependentPythonAndOpenSslVectors) {
    auto storage = signer(); ASSERT_TRUE(storage);
    for (auto method : {"GET", "PUT"}) {
        presigned_url_options options; options.method = method;
        if (options.method == "GET") {
            options.response_content_type = "text/plain";
            options.response_content_disposition = "attachment; filename=\"café.txt\"";
        } else {
            options.content_type = "  text/plain;   charset=UTF-8 ";
            options.content_md5 = "XUFAKrxLKna5cZ2REBfFkg==";
            options.custom_metadata = {{"Label", "  Test \tValue "}};
        }
        auto result = storage->generate_signed_url("dir/café +%?.txt", options, fixed_time()); ASSERT_TRUE(result) << result.error().message;
        EXPECT_EQ(result.value(), fixture(options.method == "GET" ? "get.url" : "put.url"));
        EXPECT_EQ(result.value().find("placeholder"), std::string::npos);
        EXPECT_NE(result.value().find("test-signer%40test-project.iam.gserviceaccount.com"), std::string::npos);
    }
}

TEST(GcsSigningTest, SignatureVerifiesLocallyAndTamperedMessageFails) {
    auto url = fixture("get.url"), message = fixture("get.string-to-sign");
    auto hex = url.substr(url.find("&X-Goog-Signature=") + 18);
    // The parameter prefix is 18 characters including the ampersand.
    std::vector<unsigned char> signature;
    for (std::size_t i = 0; i < hex.size(); i += 2) signature.push_back(static_cast<unsigned char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    auto pem = fixture("test-key.pem");
    auto bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())); ASSERT_NE(bio, nullptr);
    auto key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr); BIO_free(bio); ASSERT_NE(key, nullptr);
    auto verify = [&](const std::string& input) {
        auto ctx = EVP_MD_CTX_new();
        bool valid = EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, key) == 1 &&
            EVP_DigestVerify(ctx, signature.data(), signature.size(), reinterpret_cast<const unsigned char*>(input.data()), input.size()) == 1;
        EVP_MD_CTX_free(ctx); return valid;
    };
    EXPECT_TRUE(verify(message)); EXPECT_FALSE(verify(message + "tampered")); EVP_PKEY_free(key);
}

TEST(GcsSigningTest, ExpirationBoundariesAndUnsupportedInputsAreExplicitErrors) {
    auto storage = signer(); ASSERT_TRUE(storage);
    presigned_url_options options;
    for (auto seconds : {-1, 0, 604801}) { options.expiration = std::chrono::seconds{seconds}; EXPECT_FALSE(storage->generate_signed_url("key", options, fixed_time())); }
    for (auto seconds : {1, 604800}) { options.expiration = std::chrono::seconds{seconds}; EXPECT_TRUE(storage->generate_signed_url("key", options, fixed_time())); }
    for (auto method : {"POST", "DELETE", "HEAD", "get"}) { options.method = method; EXPECT_FALSE(storage->generate_signed_url("key", options, fixed_time())); }
    options = {}; EXPECT_FALSE(storage->generate_signed_url("", options, fixed_time()));
    options.content_type = "text/plain\r\nx-injected: yes"; EXPECT_FALSE(storage->generate_signed_url("key", options, fixed_time()));
    options = {}; options.custom_metadata = {{"Label", "a"}, {"label", "b"}};
    EXPECT_FALSE(storage->generate_signed_url("key", options, fixed_time()));
    options = {}; options.server_side_encryption = "AES256";
    EXPECT_FALSE(storage->generate_signed_url("key", options, fixed_time()));
}

TEST(GcsSigningTest, MalformedDocumentsAndMissingSigningCapabilityFail) {
    auto valid = fixture("service-account.json");
    auto duplicate = valid; duplicate.insert(1, "\"type\":\"service_account\",");
    for (auto document : {std::string("{}"), valid + "garbage", duplicate, valid.substr(0, valid.size() / 2),
            std::string("{\"type\":\"service_account\",\"project_id\":\"p\",\"client_email\":\"a@b\",\"private_key\":\"not a PEM\"}"),
            std::string("{\"type\":\"service_account\",\"type\":\"service_account\"}"),
            std::string("{\"private_key\":\"\\uD800x\"}")}) {
        auto storage = signer(document); ASSERT_TRUE(storage);
        auto result = storage->generate_signed_url("key", {}, fixed_time()); EXPECT_FALSE(result);
        EXPECT_EQ(result.error().code, error_code::invalid_configuration);
    }
    static_credentials values; values.access_key_id = "test-access"; values.secret_access_key = "test-secret";
    auto config = cloud_config_builder::gcs().with_bucket("test-bucket").with_project_id("test-project").build_gcs();
    auto storage = gcs_storage::create(config, s3_credential_provider::create(values)); ASSERT_TRUE(storage);
    EXPECT_FALSE(storage->generate_signed_url("key", {}, fixed_time()));
}

TEST(GcsSigningTest, FileBasedCredentialsUseTheSameActualSigner) {
    auto provider = gcs_credential_provider::create_from_service_account_file(
        (std::filesystem::path(FILE_TRANS_TEST_DATA_DIR) / "gcs_signing/service-account.json").string()); ASSERT_TRUE(provider);
    auto config = cloud_config_builder::gcs().with_bucket("test-bucket").with_project_id("test-project").build_gcs();
    auto storage = gcs_storage::create(config, std::move(provider)); ASSERT_TRUE(storage);
    presigned_url_options options; options.response_content_type = "text/plain";
    options.response_content_disposition = "attachment; filename=\"café.txt\"";
    auto result = storage->generate_signed_url("dir/café +%?.txt", options, fixed_time()); ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), fixture("get.url"));
}

TEST(GcsSigningTest, JsonUnicodeEscapesPreserveTheSigningIdentity) {
    auto document = fixture("service-account.json"); auto at = document.find('@'); ASSERT_NE(at, std::string::npos);
    document.replace(at, 1, "\\u0040"); auto storage = signer(document); ASSERT_TRUE(storage);
    presigned_url_options options; options.response_content_type = "text/plain";
    options.response_content_disposition = "attachment; filename=\"café.txt\"";
    auto result = storage->generate_signed_url("dir/café +%?.txt", options, fixed_time()); ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), fixture("get.url"));
}
} // namespace kcenon::file_transfer
