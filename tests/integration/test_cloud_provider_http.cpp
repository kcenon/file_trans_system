#include <gtest/gtest.h>
#include <kcenon/file_transfer/cloud/azure_blob_storage.h>
#include <kcenon/file_transfer/cloud/gcs_storage.h>
#include <kcenon/file_transfer/cloud/cloud_utils.h>
#include "scripted_http_peer.h"
#include <fstream>

namespace kcenon::file_transfer::test {
namespace {
auto azure_credentials_for_test() -> std::shared_ptr<credential_provider> {
    azure_credentials credentials; credentials.account_name = "fixture";
    credentials.account_key = cloud_utils::base64_encode(cloud_utils::generate_random_bytes(32));
    return azure_blob_credential_provider::create(credentials);
}

TEST(CloudProviderHttpTest, AzureAdapterSendsSignedOperationsAndPreservesBinaryData) {
    const std::string bytes("a\0b\xff", 4);
    scripted_http_peer peer({{201, "", {{"ETag", "etag"}}}, {200, bytes, {}},
        {200, "", {{"Content-Length", "4"}, {"Content-Type", "custom/type"}, {"ETag", "etag"},
                   {"Last-Modified", "Thu, 02 Jan 2025 03:04:05 GMT"}}}, {202, "", {}}});
    auto config = cloud_config_builder::azure_blob().with_account_name("fixture")
        .with_bucket("container").with_endpoint(peer.endpoint()).build_azure_blob();
    auto storage = azure_blob_storage::create(config, azure_credentials_for_test()); ASSERT_TRUE(storage);
    ASSERT_TRUE(storage->connect());
    auto uploaded = storage->upload("object", std::as_bytes(std::span(bytes))); ASSERT_TRUE(uploaded);
    EXPECT_EQ(uploaded.value().bytes_uploaded, bytes.size());
    auto downloaded = storage->download("object"); ASSERT_TRUE(downloaded) << downloaded.error().message;
    EXPECT_EQ(downloaded.value(), (std::vector<std::byte>{std::byte{'a'}, std::byte{0}, std::byte{'b'}, std::byte{255}}));
    auto metadata = storage->get_metadata("object"); ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata.value().size, 4); EXPECT_EQ(metadata.value().content_type, "custom/type");
    EXPECT_GT(metadata.value().last_modified.time_since_epoch().count(), 0);
    ASSERT_TRUE(storage->delete_object("object"));
    ASSERT_EQ(peer.received().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto requests = peer.received().get(); ASSERT_EQ(requests.size(), 4);
    EXPECT_EQ(requests[0].body, bytes);
    EXPECT_NE(requests[0].headers.find("x-ms-blob-type: BlockBlob\r\n"), std::string::npos);
    for (std::size_t i = 0; const auto* method : {"PUT", "GET", "HEAD", "DELETE"}) {
        EXPECT_TRUE(requests[i].headers.starts_with(std::string(method) + " /container/object "));
        EXPECT_NE(requests[i++].headers.find("Authorization: SharedKey fixture:"), std::string::npos);
    }
}

TEST(CloudProviderHttpTest, AzureSmallUploadsRequireServiceAcknowledgement) {
    for (const int status : {200, 403, 500}) {
        scripted_http_peer peer({{status, "denied", {}}});
        auto config = cloud_config_builder::azure_blob().with_account_name("fixture")
            .with_bucket("container").with_endpoint(peer.endpoint()).build_azure_blob();
        auto storage = azure_blob_storage::create(config, azure_credentials_for_test()); ASSERT_TRUE(storage->connect());
        const std::string data = "must persist";
        EXPECT_FALSE(storage->upload("object", std::as_bytes(std::span(data))));
        EXPECT_EQ(storage->get_statistics().bytes_uploaded, 0);
    }
}

TEST(CloudProviderHttpTest, AzureDownloadStreamUsesRangesAndAuthenticatesMetadata) {
    scripted_http_peer peer({{200, "", {{"Content-Length", "5"}, {"Content-Type", "application/octet-stream"},
        {"ETag", "etag"}, {"Last-Modified", "Thu, 02 Jan 2025 03:04:05 GMT"}}},
        {206, std::string("a\0b", 3), {}}, {206, "cd", {}}});
    auto config = cloud_config_builder::azure_blob().with_account_name("fixture")
        .with_bucket("container").with_endpoint(peer.endpoint()).build_azure_blob();
    auto storage = azure_blob_storage::create(config, azure_credentials_for_test()); ASSERT_TRUE(storage->connect());
    auto stream = storage->create_download_stream("dir/object"); ASSERT_TRUE(stream);
    EXPECT_EQ(stream->total_size(), 5); EXPECT_EQ(stream->metadata().etag, "etag");
    std::array<std::byte, 3> buffer{};
    auto first = stream->read(buffer); ASSERT_TRUE(first); EXPECT_EQ(first.value(), 3);
    EXPECT_EQ(buffer[1], std::byte{0}); EXPECT_TRUE(stream->has_more());
    auto second = stream->read(buffer); ASSERT_TRUE(second); EXPECT_EQ(second.value(), 2);
    EXPECT_EQ(buffer[0], std::byte{'c'}); EXPECT_EQ(buffer[1], std::byte{'d'});
    EXPECT_EQ(stream->bytes_read(), 5); EXPECT_FALSE(stream->has_more()); EXPECT_EQ(stream->read(buffer).value(), 0);
    ASSERT_EQ(peer.received().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto requests = peer.received().get(); ASSERT_EQ(requests.size(), 3);
    EXPECT_NE(requests[1].headers.find("Range: bytes=0-2\r\n"), std::string::npos);
    EXPECT_NE(requests[2].headers.find("Range: bytes=3-4\r\n"), std::string::npos);
    for (const auto& request : requests) EXPECT_NE(request.headers.find("Authorization: SharedKey fixture:"), std::string::npos);
}

TEST(CloudProviderHttpTest, AzureStreamDoesNotConvertHttpFailuresToSuccessfulReads) {
    for (const int status : {403, 404, 500}) {
        scripted_http_peer peer({{status, "", {}}});
        auto config = cloud_config_builder::azure_blob().with_account_name("fixture")
            .with_bucket("container").with_endpoint(peer.endpoint()).build_azure_blob();
        auto storage = azure_blob_storage::create(config, azure_credentials_for_test()); ASSERT_TRUE(storage->connect());
        auto stream = storage->create_download_stream("object"); ASSERT_TRUE(stream);
        std::array<std::byte, 1> output{};
        EXPECT_FALSE(stream->read(output)); EXPECT_FALSE(stream->has_more());
    }
    scripted_http_peer peer({{200, "", {{"Content-Length", "1"}}}, {503, "outage", {}}});
    auto config = cloud_config_builder::azure_blob().with_account_name("fixture")
        .with_bucket("container").with_endpoint(peer.endpoint()).build_azure_blob();
    auto storage = azure_blob_storage::create(config, azure_credentials_for_test()); ASSERT_TRUE(storage->connect());
    auto stream = storage->create_download_stream("object"); ASSERT_TRUE(stream);
    std::array<std::byte, 1> output{}; EXPECT_FALSE(stream->read(output)); EXPECT_EQ(stream->bytes_read(), 0);
}

TEST(CloudProviderHttpTest, GcsAdapterExchangesJwtOnceAndPreservesObjectsAndServiceErrors) {
    const std::string payload("a\0b\xff", 4);
    scripted_http_peer peer({{200, R"({"access_token":"fixture-token","expires_in":3600})", {}},
        {200, R"({"name":"object","etag":"etag","size":"4"})", {}}, {200, payload, {}},
        {200, R"({"name":"object","size":"4","updated":"2025-01-02T03:04:05Z","contentType":"custom/type"})", {}},
        {200, R"({"items":[{"name":"object","size":"4"}],"prefixes":["dir/","other/"],"nextPageToken":"next"})", {}},
        {403, R"({"message":"fixture denied"})", {}}, {204, "", {}}});
    std::ifstream input(std::filesystem::path(FILE_TRANS_TEST_DATA_DIR) / "gcs_signing/service-account.json");
    std::string document{std::istreambuf_iterator<char>(input), {}}; ASSERT_FALSE(document.empty());
    document.insert(1, "\"token_uri\":\"" + peer.endpoint() + "/token\",");
    auto credentials = gcs_credential_provider::create_from_service_account_json(document); ASSERT_TRUE(credentials);
    auto config = cloud_config_builder::gcs().with_bucket("bucket").with_project_id("test-project")
        .with_endpoint(peer.endpoint()).build_gcs();
    auto storage = gcs_storage::create(config, std::move(credentials)); ASSERT_TRUE(storage->connect());
    auto uploaded = storage->upload_async("object", std::as_bytes(std::span(payload))).get(); ASSERT_TRUE(uploaded);
    EXPECT_EQ(uploaded.value().bytes_uploaded, 4);
    auto downloaded = storage->download("object"); ASSERT_TRUE(downloaded); EXPECT_EQ(downloaded.value().size(), 4);
    EXPECT_EQ(downloaded.value()[1], std::byte{0}); EXPECT_EQ(downloaded.value()[3], std::byte{255});
    auto metadata = storage->get_metadata("object"); ASSERT_TRUE(metadata); EXPECT_EQ(metadata.value().content_type, "custom/type");
    EXPECT_GT(metadata.value().last_modified.time_since_epoch().count(), 0);
    auto listed = storage->list_objects(); ASSERT_TRUE(listed);
    EXPECT_EQ(listed.value().common_prefixes, (std::vector<std::string>{"dir/", "other/"}));
    EXPECT_EQ(listed.value().continuation_token, "next");
    auto denied = storage->download("denied"); ASSERT_FALSE(denied);
    EXPECT_NE(denied.error().message.find("fixture denied"), std::string::npos);
    ASSERT_TRUE(storage->delete_object("object"));
    ASSERT_EQ(peer.received().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto requests = peer.received().get(); ASSERT_EQ(requests.size(), 7);
    EXPECT_TRUE(requests[0].headers.starts_with("POST /token "));
    EXPECT_TRUE(requests[0].body.starts_with("grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion="));
    EXPECT_EQ(requests[1].body, payload);
    for (std::size_t i = 1; i < requests.size(); ++i)
        EXPECT_NE(requests[i].headers.find("Authorization: Bearer fixture-token\r\n"), std::string::npos);
}
} // namespace
} // namespace kcenon::file_transfer::test
