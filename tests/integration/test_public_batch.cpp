#include "protocol_peer.h"
#include <kcenon/file_transfer/client/file_transfer_client.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <fstream>
#include <future>

namespace kcenon::file_transfer::test {
class PublicBatchTest : public ::testing::Test {
protected:
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("fts-batch-" + transfer_id::generate().to_string());
    std::unique_ptr<file_transfer_server> server;
    std::unique_ptr<file_transfer_client> client;
    std::vector<upload_entry> uploads;
    std::vector<download_entry> downloads;
    std::atomic<unsigned> admissions{};
    std::atomic<std::size_t> maximum{};
    uint64_t bytes{};
    void SetUp() override {
        auto s = file_transfer_server::builder().with_storage_directory(root / "server")
            .with_chunk_size(65536).build();
        ASSERT_TRUE(s); server = std::make_unique<file_transfer_server>(std::move(s.value()));
        auto port = available_port(); ASSERT_TRUE(server->start({"127.0.0.1", port}));
        auto c = file_transfer_client::builder().with_transfer_timeout(3s).build();
        ASSERT_TRUE(c); client = std::make_unique<file_transfer_client>(std::move(c.value()));
        ASSERT_TRUE(client->connect({"127.0.0.1", port}));
        for (unsigned i = 0; i < 10; ++i) {
            auto name = "file-" + std::to_string(i);
            auto path = root / name;
            auto size = 180000 + 17 * i;
            std::ofstream out(path, std::ios::binary);
            for (unsigned j = 0; j < size; ++j) out.put(static_cast<char>((i + j) % 251));
            uploads.emplace_back(path, name);
            downloads.emplace_back(name, root / "downloads" / name);
            bytes += size;
        }
        server->on_upload_request([this](const auto&) { observe(); return true; });
        server->on_download_request([this](const auto&) { observe(); return true; });
    }
    void observe() {
        auto active = server->get_statistics().active_transfers + 1;
        auto old = maximum.load();
        while (old < active && !maximum.compare_exchange_weak(old, active)) {}
        ++admissions;
    }
    void seed_downloads() {
        for (const auto& entry : uploads)
            std::filesystem::copy_file(entry.local_path, root / "server" / entry.remote_name);
    }
    void TearDown() override {
        client.reset(); if (server && server->is_running()) (void)server->stop();
        server.reset(); std::error_code ec; std::filesystem::remove_all(root, ec);
    }
    void check_repeated(batch_transfer_handle& handle, const batch_result& first) {
        for (int i = 0; i < 3; ++i) {
            auto again = handle.wait_for(1s); ASSERT_TRUE(again);
            EXPECT_EQ(again.value().total_files, 10);
            EXPECT_EQ(again.value().succeeded, first.succeeded);
            EXPECT_EQ(again.value().failed, first.failed);
            EXPECT_EQ(again.value().total_bytes, first.total_bytes);
            EXPECT_EQ(again.value().elapsed, first.elapsed);
            ASSERT_EQ(again.value().file_results.size(), 10);
            for (unsigned j = 0; j < 10; ++j) {
                EXPECT_EQ(again.value().file_results[j].filename, "file-" + std::to_string(j));
                EXPECT_EQ(again.value().file_results[j].success, first.file_results[j].success);
                EXPECT_EQ(again.value().file_results[j].bytes_transferred, first.file_results[j].bytes_transferred);
                EXPECT_EQ(again.value().file_results[j].error_message, first.file_results[j].error_message);
            }
        }
        auto progress = handle.get_batch_progress();
        EXPECT_EQ(progress.completed_files, first.succeeded); EXPECT_EQ(progress.failed_files, first.failed);
        EXPECT_EQ(progress.transferred_bytes, first.total_bytes); EXPECT_EQ(progress.in_progress_files, 0);
        EXPECT_EQ(progress.pending_files(), 0);
    }
};

TEST_F(PublicBatchTest, TenUploadsAndDownloadsRefillThreeSlotsWithExactBytesAndStableResults) {
    batch_options options; options.max_concurrent = 3; options.compression = compression_mode::always;
    auto up = client->upload_files(uploads, options); ASSERT_TRUE(up);
    auto result = up.value().wait_for(10s); ASSERT_TRUE(result); ASSERT_TRUE(result.value().all_succeeded());
    EXPECT_EQ(result.value().total_bytes, bytes); EXPECT_EQ(admissions, 10); EXPECT_LE(maximum, 3);
    check_repeated(up.value(), result.value());
    maximum = 0; admissions = 0;
    auto down = client->download_files(downloads, options); ASSERT_TRUE(down);
    result = down.value().wait_for(10s); ASSERT_TRUE(result); ASSERT_TRUE(result.value().all_succeeded());
    EXPECT_EQ(result.value().total_bytes, bytes); EXPECT_EQ(admissions, 10); EXPECT_LE(maximum, 3);
    check_repeated(down.value(), result.value());
    for (unsigned i = 0; i < 10; ++i) {
        EXPECT_EQ(checksum::sha256_file(uploads[i].local_path).value(),
                  checksum::sha256_file(root / "server" / uploads[i].remote_name).value());
        EXPECT_EQ(checksum::sha256_file(uploads[i].local_path).value(),
                  checksum::sha256_file(downloads[i].local_path).value());
    }
}

TEST_F(PublicBatchTest, EarlyAndLateValidationAndAdmissionErrorsPreserveInputResults) {
    uploads[0].local_path = root / "missing";
    server->on_upload_request([this](const auto& request) { observe(); return request.filename != "file-7"; });
    batch_options options; options.max_concurrent = 3;
    auto batch = client->upload_files(uploads, options); ASSERT_TRUE(batch);
    auto result = batch.value().wait_for(10s); ASSERT_TRUE(result);
    EXPECT_EQ(result.value().succeeded, 8); EXPECT_EQ(result.value().failed, 2);
    uint64_t expected = 0;
    for (unsigned i = 0; i < 10; ++i) {
        EXPECT_EQ(result.value().file_results[i].success, i != 0 && i != 7);
        if (i != 0 && i != 7) expected += std::filesystem::file_size(uploads[i].local_path);
        else EXPECT_TRUE(result.value().file_results[i].error_message.has_value());
    }
    EXPECT_EQ(result.value().total_bytes, expected); EXPECT_LE(maximum, 3);
    check_repeated(batch.value(), result.value());
}

TEST_F(PublicBatchTest, DownloadFailuresAtBothEndsOfQueueContinueRemainingFiles) {
    seed_downloads();
    std::filesystem::remove(root / "server/file-0");
    std::filesystem::remove(root / "server/file-8");
    batch_options options; options.max_concurrent = 3;
    auto batch = client->download_files(downloads, options); ASSERT_TRUE(batch);
    auto result = batch.value().wait_for(10s); ASSERT_TRUE(result);
    EXPECT_EQ(result.value().succeeded, 8); EXPECT_EQ(result.value().failed, 2);
    for (unsigned i = 0; i < 10; ++i)
        EXPECT_EQ(result.value().file_results[i].success, i != 0 && i != 8);
    check_repeated(batch.value(), result.value()); EXPECT_LE(maximum, 3);
}

TEST_F(PublicBatchTest, FailFastAccountsForEveryUnstartedFile) {
    uploads[0].local_path = root / "missing";
    batch_options options; options.max_concurrent = 3; options.continue_on_error = false;
    auto batch = client->upload_files(uploads, options); ASSERT_TRUE(batch);
    auto result = batch.value().wait_for(5s); ASSERT_TRUE(result);
    EXPECT_EQ(result.value().succeeded, 0); EXPECT_EQ(result.value().failed, 10);
    EXPECT_EQ(admissions, 0); EXPECT_EQ(result.value().total_bytes, 0);
    for (unsigned i = 1; i < 10; ++i)
        EXPECT_EQ(result.value().file_results[i].error_message, "Skipped after batch error");
    check_repeated(batch.value(), result.value());
}

TEST_F(PublicBatchTest, LateRuntimeFailFastStopsPendingEntries) {
    server->on_upload_request([this](const auto& request) { observe(); return request.filename != "file-4"; });
    batch_options options; options.max_concurrent = 1; options.continue_on_error = false;
    auto batch = client->upload_files(uploads, options); ASSERT_TRUE(batch);
    auto result = batch.value().wait_for(5s); ASSERT_TRUE(result);
    EXPECT_EQ(result.value().succeeded, 4); EXPECT_EQ(result.value().failed, 6); EXPECT_EQ(admissions, 5);
    for (unsigned i = 0; i < 10; ++i) EXPECT_EQ(result.value().file_results[i].success, i < 4);
    check_repeated(batch.value(), result.value());
}

TEST_F(PublicBatchTest, InvalidConcurrencyAndEmptyInputsAreRejectedBeforeWork) {
    for (auto concurrency : {std::size_t{0}, std::size_t{257}, std::numeric_limits<std::size_t>::max()}) {
        batch_options options; options.max_concurrent = concurrency;
        EXPECT_FALSE(client->upload_files(uploads, options)); EXPECT_FALSE(client->download_files(downloads, options));
    }
    EXPECT_FALSE(client->upload_files({})); EXPECT_FALSE(client->download_files({})); EXPECT_EQ(admissions, 0);
}

TEST_F(PublicBatchTest, PauseBeforeSlotReleaseHoldsPendingEntriesAndWaitDoesNotConsumeResults) {
    std::atomic<uint64_t> id{}; std::atomic<bool> once{};
    std::promise<void> paused; auto signal = paused.get_future();
    client->on_progress([&](const auto&) {
        if (once.exchange(true)) return;
        while (!id.load()) std::this_thread::yield();
        EXPECT_TRUE(client->pause_batch(id)); paused.set_value();
    });
    batch_options options; options.max_concurrent = 1;
    auto batch = client->upload_files(uploads, options); ASSERT_TRUE(batch); id = batch.value().get_id();
    ASSERT_EQ(signal.wait_for(3s), std::future_status::ready);
    auto premature = batch.value().wait_for(75ms); EXPECT_FALSE(premature);
    EXPECT_EQ(admissions, 1); EXPECT_EQ(batch.value().get_batch_progress().pending_files(), 9);
    ASSERT_TRUE(batch.value().resume_all());
    auto result = batch.value().wait_for(10s); ASSERT_TRUE(result); EXPECT_TRUE(result.value().all_succeeded());
    check_repeated(batch.value(), result.value());
}

TEST_F(PublicBatchTest, CancellationIncludesPendingAndActiveEntries) {
    std::atomic<uint64_t> id{}; std::atomic<bool> once{};
    client->on_progress([&](const auto&) {
        if (once.exchange(true)) return;
        while (!id.load()) std::this_thread::yield();
        EXPECT_TRUE(client->cancel_batch(id));
    });
    batch_options options; options.max_concurrent = 1;
    auto batch = client->upload_files(uploads, options); ASSERT_TRUE(batch); id = batch.value().get_id();
    auto result = batch.value().wait_for(5s); ASSERT_TRUE(result);
    EXPECT_EQ(result.value().failed, 10); EXPECT_EQ(result.value().succeeded, 0); EXPECT_EQ(admissions, 1);
    for (const auto& file : result.value().file_results) EXPECT_TRUE(file.error_message.has_value());
    check_repeated(batch.value(), result.value());
}

TEST_F(PublicBatchTest, ClientDestructionJoinsSchedulerAndDynamicallyCreatedJobs) {
    for (unsigned i = 0; i < 5; ++i) {
        batch_options options; options.max_concurrent = 3; options.overwrite = true;
        ASSERT_TRUE(client->upload_files(uploads, options));
    }
    auto start = std::chrono::steady_clock::now(); client.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - start, 3s);
}
}  // namespace kcenon::file_transfer::test
