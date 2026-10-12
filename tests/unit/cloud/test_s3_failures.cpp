#include <gtest/gtest.h>
#include <kcenon/file_transfer/cloud/s3_storage.h>
#include <asio.hpp>

namespace kcenon::file_transfer {
namespace {
auto credentials() -> std::shared_ptr<credential_provider> {
    static_credentials value; value.access_key_id = "test-access"; value.secret_access_key = "test-secret";
    return s3_credential_provider::create(value);
}
auto test_config() -> s3_config {
    s3_config config; config.bucket = "bucket"; config.region = "us-east-1";
    config.retry.initial_delay = std::chrono::milliseconds(1); config.retry.max_delay = std::chrono::milliseconds(1);
    config.retry.use_jitter = false; config.request_timeout = std::chrono::milliseconds(100);
    return config;
}
auto ok(int status = 200, std::string body = {}, std::string etag = "\"remote-etag\"") -> cloud_http_response {
    cloud_http_response response; response.status_code = status; response.body.assign(body.begin(), body.end());
    if (!etag.empty()) response.headers["ETag"] = etag;
    return response;
}
}

TEST(S3FailureTest, DefaultAwsEndpointUsesSignedTransport) {
    std::vector<std::string> urls;
    auto storage = s3_storage::create(test_config(), credentials(), [&](const cloud_http_request& request) -> result<cloud_http_response> {
        urls.push_back(request.url); EXPECT_TRUE(request.headers.contains("Authorization"));
        EXPECT_EQ(request.headers.at("Host"), "bucket.s3.us-east-1.amazonaws.com");
        EXPECT_TRUE(request.verify_ssl); return ok();
    });
    ASSERT_TRUE(storage->connect()); std::vector<std::byte> data(4); ASSERT_TRUE(storage->upload("space name", data));
    EXPECT_EQ(urls, (std::vector<std::string>{"https://bucket.s3.us-east-1.amazonaws.com/", "https://bucket.s3.us-east-1.amazonaws.com/space%20name"}));
    EXPECT_EQ(storage->get_statistics().upload_count, 1); EXPECT_EQ(storage->get_statistics().bytes_uploaded, 4);
}

TEST(S3FailureTest, UnreachableEndpointNeverConnectsOrCountsUploadedBytes) {
    asio::io_context io; asio::ip::tcp::acceptor probe(io, {asio::ip::tcp::v4(), 0});
    auto port = probe.local_endpoint().port(); probe.close();
    auto config = test_config(); config.endpoint = "http://127.0.0.1:" + std::to_string(port); config.use_path_style = true;
    config.retry.max_attempts = 1;
    auto storage = s3_storage::create(config, credentials());
    auto connected = storage->connect(); ASSERT_FALSE(connected); EXPECT_FALSE(storage->is_connected());
    EXPECT_EQ(connected.error().code, error_code::connection_failed);
    EXPECT_FALSE(storage->upload("key", std::vector<std::byte>(4)));
    EXPECT_EQ(storage->get_statistics().upload_count, 0); EXPECT_EQ(storage->get_statistics().bytes_uploaded, 0);
}

TEST(S3FailureTest, HeadHttpFailuresAndExhaustedTransportRetriesRemainErrors) {
    for (auto status : {0, 403, 404, 503}) {
        unsigned requests = 0;
        auto storage = s3_storage::create(test_config(), credentials(), [&](const auto&) -> result<cloud_http_response> {
            ++requests;
            if (!status) return unexpected{error{error_code::connection_failed, "injected transport failure"}};
            return ok(status);
        });
        EXPECT_FALSE(storage->connect()) << status;
        EXPECT_EQ(requests, status == 0 || status == 503 ? 3 : 1);
        EXPECT_EQ(storage->get_statistics().bytes_uploaded, 0);
    }
}

TEST(S3FailureTest, FailedPutAndMalformedSuccessNeverIncrementSuccessCounters) {
    for (auto status : {0, 403, 404, 503, 200}) {
        unsigned puts = 0;
        auto storage = s3_storage::create(test_config(), credentials(), [&](const auto& request) -> result<cloud_http_response> {
            if (request.method == "HEAD") return ok();
            ++puts;
            if (!status) return unexpected{error{error_code::connection_failed, "injected PUT failure"}};
            return ok(status, "", "");
        });
        ASSERT_TRUE(storage->connect()); EXPECT_FALSE(storage->upload("key", std::vector<std::byte>(32))) << status;
        EXPECT_EQ(puts, status == 0 || status == 503 ? 3 : 1);
        EXPECT_EQ(storage->get_statistics().bytes_uploaded, 0); EXPECT_EQ(storage->get_statistics().upload_count, 0);
        EXPECT_EQ(storage->get_statistics().errors, 1);
    }
}

TEST(S3FailureTest, EachMultipartPhasePropagatesTransportAndResponseFailures) {
    for (auto phase : {"initiate", "part", "complete"}) for (auto failure : {"transport", "service", "malformed", "embedded"}) {
        auto config = test_config(); config.multipart.threshold = 1;
        std::atomic<unsigned> aborts = 0, failures = 0;
        auto storage = s3_storage::create(config, credentials(), [&, phase = std::string(phase), failure = std::string(failure)](const auto& request) -> result<cloud_http_response> {
            if (request.method == "HEAD") return ok();
            if (request.method == "DELETE") { ++aborts; return ok(204); }
            auto current = request.url.find("uploads=") != std::string::npos ? "initiate" : request.method == "PUT" ? "part" : "complete";
            if (phase == current) {
                ++failures;
                if (failure == "transport") return unexpected{error{error_code::connection_failed, "injected multipart failure"}};
                if (failure == "service") return ok(503);
                if (failure == "embedded") return ok(200, "<Error><Code>InternalError</Code></Error>", "");
                return ok(200, "malformed", "");
            }
            if (std::string(current) == "initiate") return ok(200, "<InitiateMultipartUploadResult><UploadId>upload-id</UploadId></InitiateMultipartUploadResult>");
            if (std::string(current) == "complete") return ok(200, "<CompleteMultipartUploadResult><ETag>persisted</ETag></CompleteMultipartUploadResult>");
            return ok();
        });
        ASSERT_TRUE(storage->connect());
        auto result = storage->upload("key", std::vector<std::byte>(128)); EXPECT_FALSE(result) << phase << "/" << failure;
        EXPECT_GT(failures, 0); EXPECT_EQ(aborts, std::string(phase) == "initiate" ? 0 : 1);
        EXPECT_EQ(storage->get_statistics().bytes_uploaded, 0); EXPECT_EQ(storage->get_statistics().upload_count, 0);
    }
}

TEST(S3FailureTest, AbortFailureIsExplicitAndFailedStreamCannotFinalizeSuccessfully) {
    std::atomic<unsigned> aborts = 0;
    auto storage = s3_storage::create(test_config(), credentials(), [&](const auto& request) -> result<cloud_http_response> {
        if (request.method == "HEAD") return ok();
        if (request.method == "DELETE") { ++aborts; return unexpected{error{error_code::connection_failed, "abort failed"}}; }
        if (request.url.find("uploads=") != std::string::npos) return ok(200, "<InitiateMultipartUploadResult><UploadId>id</UploadId></InitiateMultipartUploadResult>");
        return unexpected{error{error_code::connection_failed, "part failed"}};
    });
    ASSERT_TRUE(storage->connect()); auto stream = storage->create_upload_stream("key"); ASSERT_TRUE(stream);
    ASSERT_TRUE(stream->write(std::vector<std::byte>(10))); EXPECT_FALSE(stream->finalize());
    EXPECT_FALSE(stream->finalize()); EXPECT_FALSE(stream->write(std::vector<std::byte>(10)));
    EXPECT_FALSE(stream->abort()); EXPECT_GE(aborts, 6);
}
} // namespace kcenon::file_transfer
