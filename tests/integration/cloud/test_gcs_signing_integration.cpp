#include <gtest/gtest.h>
#include <kcenon/file_transfer/cloud/gcs_storage.h>
#include <kcenon/file_transfer/cloud/http_transport.h>
#include <cstdlib>

namespace kcenon::file_transfer {
TEST(GcsSignedUrlIntegrationTest, AuthorizedGetSucceedsAndTamperedSignatureFails) {
    auto file = std::getenv("GCS_SIGNING_TEST_SERVICE_ACCOUNT_FILE");
    auto bucket = std::getenv("GCS_SIGNING_TEST_BUCKET");
    auto object = std::getenv("GCS_SIGNING_TEST_OBJECT");
    if (!file || !bucket || !object) GTEST_SKIP() << "Explicit GCS signing test credentials, bucket, and existing object are required";
    auto provider = gcs_credential_provider::create_from_service_account_file(file); ASSERT_TRUE(provider);
    auto config = cloud_config_builder::gcs().with_bucket(bucket).build_gcs();
    auto storage = gcs_storage::create(config, std::move(provider)); ASSERT_TRUE(storage);
    auto url = storage->generate_signed_url(object); ASSERT_TRUE(url) << url.error().message;
    auto http = make_default_cloud_http_transport();
    cloud_http_request request; request.method = "GET"; request.url = url.value();
    request.headers["Range"] = "bytes=0-1023"; request.max_response_bytes = 1024 * 1024;
    auto response = http(request); ASSERT_TRUE(response) << response.error().message;
    EXPECT_TRUE(response.value().status_code == 200 || response.value().status_code == 206);
    request.url.back() = request.url.back() == '0' ? '1' : '0';
    auto tampered = http(request); ASSERT_TRUE(tampered); EXPECT_EQ(tampered.value().status_code, 403);
}
} // namespace kcenon::file_transfer
