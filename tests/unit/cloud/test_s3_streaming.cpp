#include <gtest/gtest.h>
#include <kcenon/file_transfer/cloud/s3_storage.h>
#include <fstream>
#include <thread>

namespace kcenon::file_transfer {
namespace {
auto stream_credentials() -> std::shared_ptr<credential_provider> {
    static_credentials credentials; credentials.access_key_id = "test-access"; credentials.secret_access_key = "test-secret";
    return s3_credential_provider::create(credentials);
}
auto stream_config() -> s3_config {
    s3_config config; config.bucket = "bucket"; config.region = "us-east-1"; config.download_range_size = 5;
    config.retry.max_attempts = 1; return config;
}
auto response(int status, std::string body = {}) -> cloud_http_response {
    cloud_http_response r; r.status_code = status; r.body.assign(body.begin(), body.end()); return r;
}
auto range_transport(std::string contents, std::string failure = {}) -> cloud_http_transport {
    return [contents, failure, ranges = 0u](const cloud_http_request& request) mutable -> result<cloud_http_response> {
        if (request.method == "HEAD") {
            auto r = response(failure == "missing" && !request.url.ends_with('/') ? 404 : 200);
            r.headers["Content-Length"] = failure == "bad-size" ? "12garbage" : std::to_string(contents.size());
            r.headers["ETag"] = "\"identity\""; r.headers["Content-Type"] = "text/plain"; return r;
        }
        ++ranges;
        if (failure == "after-first" && ranges > 1) return unexpected{error{error_code::connection_lost, "range interrupted"}};
        EXPECT_EQ(request.headers.at("If-Match"), "\"identity\"");
        auto range = request.headers.at("Range"); auto dash = range.find('-');
        auto start = std::stoull(range.substr(6, dash - 6)), end = std::stoull(range.substr(dash + 1));
        EXPECT_LE(end - start + 1, 5); EXPECT_EQ(request.max_response_bytes, end - start + 1);
        auto r = response(failure == "ignored" ? 200 : failure == "changed" ? 412 : 206, contents.substr(start, end - start + 1));
        r.headers["ETag"] = failure == "identity" ? "\"replaced\"" : "\"identity\"";
        r.headers["Content-Range"] = "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(contents.size());
        if (failure == "range") r.headers["Content-Range"] = "bytes 1-5/12";
        if (failure == "short") r.body.pop_back();
        return r;
    };
}
}

TEST(S3StreamingTest, SmallCallerBuffersReadExactBytesAcrossRangeBoundaries) {
    auto storage = s3_storage::create(stream_config(), stream_credentials(), range_transport("abcdefghijkl"));
    ASSERT_TRUE(storage->connect()); auto stream = storage->create_download_stream("object"); ASSERT_TRUE(stream);
    EXPECT_EQ(stream->total_size(), 12); EXPECT_EQ(stream->metadata().etag, "\"identity\"");
    EXPECT_EQ(stream->metadata().content_type, "text/plain");
    std::array<std::byte, 3> buffer; std::string actual;
    while (stream->has_more()) {
        auto read = stream->read(buffer); ASSERT_TRUE(read); ASSERT_GT(read.value(), 0);
        actual.append(reinterpret_cast<const char*>(buffer.data()), read.value());
        EXPECT_EQ(stream->bytes_read(), actual.size());
    }
    EXPECT_EQ(actual, "abcdefghijkl"); EXPECT_EQ(stream->read(buffer).value(), 0);
    EXPECT_FALSE(stream->has_more());
}

TEST(S3StreamingTest, EmptyObjectIsEofButInitializationAndRangeFailuresAreErrors) {
    for (auto failure : {"", "missing", "bad-size", "range", "short", "ignored", "changed", "identity", "after-first"}) {
        auto storage = s3_storage::create(stream_config(), stream_credentials(), range_transport(std::string(failure).empty() ? "" : "abcdefghijkl", failure));
        ASSERT_TRUE(storage->connect()); auto stream = storage->create_download_stream("object"); ASSERT_TRUE(stream);
        std::array<std::byte, 5> buffer;
        if (std::string(failure).empty()) { EXPECT_FALSE(stream->has_more()); EXPECT_EQ(stream->read(buffer).value(), 0); continue; }
        if (std::string(failure) == "after-first") { auto read = stream->read(buffer); ASSERT_TRUE(read); ASSERT_EQ(read.value(), 5); }
        auto before = stream->bytes_read(); EXPECT_TRUE(stream->has_more());
        auto read = stream->read(buffer); EXPECT_FALSE(read) << failure;
        EXPECT_EQ(stream->bytes_read(), before); EXPECT_FALSE(stream->read(buffer));
    }
}

TEST(S3StreamingTest, FileUploadsBoundPartBuffersAndConcurrentRequests) {
    auto config = stream_config(); config.multipart.threshold = 1; config.multipart.part_size = 65536;
    config.multipart.max_concurrent_parts = 2; config.file_io_buffer_size = 8192;
    auto path = std::filesystem::temp_directory_path() / ("s3-stream-budget-" + transfer_id::generate().to_string());
    constexpr std::size_t file_size = 16 * 1024 * 1024 + 7;
    { std::ofstream file(path, std::ios::binary); std::array<char, 8192> bytes; bytes.fill('x');
      for (std::size_t offset = 0; offset < file_size; offset += bytes.size()) file.write(bytes.data(), std::min(bytes.size(), file_size - offset)); }
    std::atomic<unsigned> active = 0, peak = 0, parts = 0; std::atomic<uint64_t> sent_bytes = 0;
    auto storage = s3_storage::create(config, stream_credentials(), [&](const auto& request) -> result<cloud_http_response> {
        if (request.method == "HEAD") return response(200);
        if (request.url.find("uploads=") != std::string::npos) return response(200, "<InitiateMultipartUploadResult><UploadId>id</UploadId></InitiateMultipartUploadResult>");
        if (request.method == "POST") return response(200, "<CompleteMultipartUploadResult><ETag>persisted</ETag></CompleteMultipartUploadResult>");
        if (request.method == "DELETE") return response(204);
        auto count = ++active; auto observed = peak.load(); while (count > observed && !peak.compare_exchange_weak(observed, count)) {}
        EXPECT_LE(request.body.size(), config.multipart.part_size); EXPECT_FALSE(request.source);
        EXPECT_TRUE(std::all_of(request.body.begin(), request.body.end(), [](auto b) { return b == std::byte{'x'}; }));
        sent_bytes += request.body.size(); ++parts; std::this_thread::sleep_for(std::chrono::milliseconds(1)); --active;
        auto r = response(200); r.headers["ETag"] = "part-etag"; return r;
    });
    ASSERT_TRUE(storage->connect()); auto uploaded = storage->upload_file(path, "object"); ASSERT_TRUE(uploaded) << uploaded.error().message;
    EXPECT_EQ(uploaded.value().bytes_uploaded, file_size); EXPECT_EQ(sent_bytes, file_size);
    EXPECT_EQ(parts, file_size / config.multipart.part_size + 1); EXPECT_LE(peak, 2);
    EXPECT_EQ(storage->get_statistics().bytes_uploaded, file_size);
    std::error_code ec; std::filesystem::remove(path, ec);
}

TEST(S3StreamingTest, SinglePutUsesReplayableSourceInsteadOfWholeFileBuffer) {
    auto config = stream_config(); config.multipart.enabled = false;
    auto path = std::filesystem::temp_directory_path() / ("s3-single-stream-" + transfer_id::generate().to_string());
    { std::ofstream file(path); file << "streamed bytes"; }
    auto storage = s3_storage::create(config, stream_credentials(), [&](const auto& request) -> result<cloud_http_response> {
        if (request.method == "HEAD") return response(200);
        EXPECT_TRUE(request.body.empty()); EXPECT_TRUE(request.source); if (!request.source) return response(500);
        EXPECT_EQ(request.source->size, 14); EXPECT_EQ(request.source->sha256.size(), 64);
        std::array<std::byte, 4> part;
        EXPECT_EQ(request.source->read(0, part).value(), 4);
        EXPECT_EQ(std::string(reinterpret_cast<char*>(part.data()), part.size()), "stre");
        EXPECT_EQ(request.source->read(0, part).value(), 4); // Retry starts at zero.
        auto r = response(200); r.headers["ETag"] = "remote-etag"; return r;
    });
    ASSERT_TRUE(storage->connect()); EXPECT_TRUE(storage->upload_file(path, "object"));
    std::error_code ec; std::filesystem::remove(path, ec);
}
TEST(S3StreamingTest, LocalReadFailureAbortsMultipartAndPreservesFailureCounters) {
    auto config = stream_config(); config.multipart.threshold = 1; config.multipart.part_size = 65536;
    config.multipart.max_concurrent_parts = 1; config.file_io_buffer_size = 8192;
    auto path = std::filesystem::temp_directory_path() / ("s3-truncate-" + transfer_id::generate().to_string());
    { std::ofstream file(path); file.seekp(4 * 65536); file.put('x'); }
    unsigned aborts = 0;
    auto storage = s3_storage::create(config, stream_credentials(), [&](const auto& request) -> result<cloud_http_response> {
        if (request.method == "HEAD") return response(200);
        if (request.url.find("uploads=") != std::string::npos) return response(200, "<InitiateMultipartUploadResult><UploadId>id</UploadId></InitiateMultipartUploadResult>");
        if (request.method == "DELETE") { ++aborts; return response(204); }
        if (request.method == "PUT") {
            std::filesystem::resize_file(path, 0);
            auto r = response(200); r.headers["ETag"] = "part"; return r;
        }
        ADD_FAILURE() << "An incomplete source must not reach multipart completion"; return response(500);
    });
    ASSERT_TRUE(storage->connect()); auto uploaded = storage->upload_file(path, "object");
    EXPECT_FALSE(uploaded); EXPECT_EQ(uploaded.error().code, error_code::file_read_error);
    EXPECT_EQ(aborts, 1); EXPECT_EQ(storage->get_statistics().bytes_uploaded, 0);
    std::error_code ec; std::filesystem::remove(path, ec);
}
} // namespace kcenon::file_transfer
