#include "protocol_peer.h"
#include <kcenon/file_transfer/client/file_transfer_client.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <kcenon/file_transfer/core/transfer_checkpoint.h>
#include <fstream>
namespace kcenon::file_transfer::test {
class PublicResumeTest : public ::testing::Test {
protected:
    std::filesystem::path root=std::filesystem::temp_directory_path()/("fts-resume-"+transfer_id::generate().to_string());
    std::unique_ptr<file_transfer_server> server;
    std::unique_ptr<file_transfer_client> client;
    uint16_t port{};
    std::vector<std::byte> key=std::vector<std::byte>(32,std::byte{0x71});
    void start(bool encrypted=false) {
        auto b=file_transfer_server::builder().with_storage_directory(root/"server").with_checkpoint_directory(root/"server-state").with_chunk_size(65536);
        if(encrypted) b.with_encryption_key(key);
        auto s=b.build(); ASSERT_TRUE(s); server=std::make_unique<file_transfer_server>(std::move(s.value()));
        if(!port) port=available_port(); ASSERT_TRUE(server->start({"127.0.0.1",port}));
    }
    void connect(bool encrypted=false,bool reconnect=false) {
        reconnect_policy policy; policy.initial_delay=20ms; policy.max_delay=50ms;
        auto b=file_transfer_client::builder().with_checkpoint_directory(root/"client-state").with_auto_reconnect(reconnect,policy).with_transfer_timeout(2s);
        if(encrypted) b.with_encryption_key(key);
        auto c=b.build(); ASSERT_TRUE(c); client=std::make_unique<file_transfer_client>(std::move(c.value()));
        ASSERT_TRUE(client->connect({"127.0.0.1",port}));
    }
    auto source(std::string name="source",size_t size=300000) -> std::filesystem::path {
        std::filesystem::create_directories(root); auto path=root/name;
        std::ofstream out(path,std::ios::binary); for(size_t i=0;i<size;++i) out.put(static_cast<char>(i%251)); return path;
    }
    auto interrupt_upload() -> transfer_state {
        auto path=source(); client->on_progress([this](auto){ (void)client->disconnect(); });
        upload_options options; options.resumable=true;
        auto h=client->upload_file(path,"data",options); if(!h) throw std::runtime_error(h.error().message);
        auto result=h.value().wait_for(5s); EXPECT_TRUE(result); if(result) EXPECT_FALSE(result.value().success);
        auto states=client->resumable_transfers(); EXPECT_EQ(states.size(),1);
        if(states.empty()) throw std::runtime_error("Missing upload checkpoint");
        EXPECT_EQ(states[0].received_chunk_count(),1); return states[0];
    }
    void TearDown() override { client.reset(); if(server&&server->is_running()) (void)server->stop(); server.reset(); std::error_code ec; std::filesystem::remove_all(root,ec); }
};
TEST_F(PublicResumeTest, FreshInstancesResumeUploadAndDownloadWithCompressionAndEncryption) {
    std::vector<bool> modes{false};
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    modes.push_back(true);
#endif
    for(bool encrypted:modes) {
        start(encrypted); connect(encrypted); auto state=interrupt_upload();
        client.reset(); ASSERT_TRUE(server->stop()); server.reset(); start(encrypted); connect(encrypted);
        std::vector<uint64_t> progress; client->on_progress([&](const auto& p){progress.push_back(p.bytes_transferred);});
        upload_options up; up.resume_id=state.id;
        auto h=client->upload_file(root/"source","data",up); ASSERT_TRUE(h) << h.error().message;
        auto result=h.value().wait_for(5s); ASSERT_TRUE(result); ASSERT_TRUE(result.value().success) << result.value().error_message.value_or("");
        ASSERT_FALSE(progress.empty()); EXPECT_EQ(progress.front(),2*65536);
        EXPECT_EQ(checksum::sha256_file(root/"source").value(),checksum::sha256_file(root/"server/data").value());
        EXPECT_TRUE(client->resumable_transfers().empty());
        client->on_progress([this](auto){ (void)client->disconnect(); });
        download_options down; down.resumable=true;
        auto d=client->download_file("data",root/"output",down); ASSERT_TRUE(d);
        auto interrupted=d.value().wait_for(5s); ASSERT_TRUE(interrupted); EXPECT_FALSE(interrupted.value().success);
        auto states=client->resumable_transfers(); ASSERT_EQ(states.size(),1); EXPECT_EQ(states[0].received_chunk_count(),1);
        auto id=states[0].id; client.reset(); ASSERT_TRUE(server->stop()); server.reset(); start(encrypted); connect(encrypted);
        progress.clear(); client->on_progress([&](const auto& p){ progress.push_back(p.bytes_transferred); });
        down.resume_id=id; auto resumed=client->download_file("data",root/"output",down); ASSERT_TRUE(resumed) << resumed.error().message;
        auto finished=resumed.value().wait_for(5s); ASSERT_TRUE(finished); ASSERT_TRUE(finished.value().success) << finished.value().error_message.value_or("");
        ASSERT_FALSE(progress.empty()); EXPECT_EQ(progress.front(),2*65536);
        EXPECT_EQ(checksum::sha256_file(root/"source").value(),checksum::sha256_file(root/"output").value());
        EXPECT_TRUE(client->resumable_transfers().empty());
        client.reset(); ASSERT_TRUE(server->stop()); server.reset(); std::filesystem::remove_all(root);
    }
}
TEST_F(PublicResumeTest, ChangedUploadSourceMissingOrCorruptCheckpointAndMissingPartialAreExplicit) {
    start(); connect(); auto state=interrupt_upload(); client.reset(); connect();
    upload_options options; options.resume_id=state.id;
    { std::ofstream f(root/"source",std::ios::binary|std::ios::app); f.put('x'); }
    EXPECT_FALSE(client->upload_file(root/"source","data",options)); source();
    auto record=root/"client-state"/(state.id.to_string()+".json");
    auto original=std::filesystem::file_size(record); EXPECT_LT(original,1024*1024);
    { std::fstream f(record,std::ios::binary|std::ios::in|std::ios::out); f.seekp(30); f.put('x'); }
    EXPECT_FALSE(client->upload_file(root/"source","data",options));
    std::filesystem::remove(record); EXPECT_FALSE(client->upload_file(root/"source","data",options));
    resume_handler handler(resume_handler_config{root/"client-state"}); ASSERT_TRUE(handler.save_state(state));
    std::filesystem::remove(root/"server-state"/(state.id.to_string()+".part"));
    auto h=client->upload_file(root/"source","data",options); ASSERT_TRUE(h);
    auto result=h.value().wait_for(5s); ASSERT_TRUE(result); EXPECT_FALSE(result.value().success);
    EXPECT_FALSE(std::filesystem::exists(root/"server/data"));
}
TEST_F(PublicResumeTest, ServerMayRejectResumeThroughTheSameAdmissionCallback) {
    start(); connect(); auto state=interrupt_upload(); client.reset(); connect();
    server->on_upload_request([](auto){return false;}); upload_options options; options.resume_id=state.id;
    auto h=client->upload_file(root/"source","data",options); ASSERT_TRUE(h); auto result=h.value().wait_for(5s);
    ASSERT_TRUE(result); EXPECT_FALSE(result.value().success); EXPECT_FALSE(std::filesystem::exists(root/"server/data"));
}
TEST_F(PublicResumeTest, NetworkOnlyReconnectRecoversTheExistingHandle) {
    start(); connect(false,true); auto path=source(); std::atomic<bool> interrupted{false};
    std::mutex mutex; std::condition_variable cv; bool stopped=false;
    client->on_progress([&](const auto&) {
        if(!interrupted.exchange(true)) { EXPECT_TRUE(server->stop()); std::lock_guard lock(mutex); stopped=true; cv.notify_all(); }
    });
    upload_options options; options.resumable=true; auto h=client->upload_file(path,"data",options); ASSERT_TRUE(h);
    { std::unique_lock lock(mutex); ASSERT_TRUE(cv.wait_for(lock,3s,[&]{return stopped;})); }
    ASSERT_TRUE(server->start({"127.0.0.1",port}));
    auto result=h.value().wait_for(5s); ASSERT_TRUE(result); ASSERT_TRUE(result.value().success) << result.value().error_message.value_or("");
    EXPECT_EQ(checksum::sha256_file(path).value(),checksum::sha256_file(root/"server/data").value());
}

TEST_F(PublicResumeTest, DownloadSourceChangeAndMissingPartialFailBeforeCommit) {
    start(); auto path=source(); std::filesystem::copy_file(path,root/"server/data"); connect();
    client->on_progress([this](auto){ (void)client->disconnect(); });
    download_options options; options.resumable=true;
    auto d=client->download_file("data",root/"output",options); ASSERT_TRUE(d);
    ASSERT_TRUE(d.value().wait_for(5s)); auto states=client->resumable_transfers(); ASSERT_EQ(states.size(),1);
    client.reset(); connect(); options.resume_id=states[0].id;
    { std::fstream f(root/"server/data",std::ios::in|std::ios::out|std::ios::binary); f.put('X'); }
    auto changed=client->download_file("data",root/"output",options); ASSERT_TRUE(changed);
    auto result=changed.value().wait_for(5s); ASSERT_TRUE(result); EXPECT_FALSE(result.value().success);
    EXPECT_FALSE(std::filesystem::exists(root/"output"));
    auto partial=root/("output."+states[0].id.to_string()+".part"); std::filesystem::remove(partial);
    EXPECT_FALSE(client->download_file("data",root/"output",options));
}
TEST_F(PublicResumeTest, NetworkOnlyDownloadRecoveryKeepsTheExistingHandle) {
    start(); auto path=source(); std::filesystem::copy_file(path,root/"server/data"); connect(false,true);
    std::atomic<bool> interrupted{false}; std::mutex mutex; std::condition_variable cv; bool stopped=false;
    client->on_progress([&](const auto&) {
        if(!interrupted.exchange(true)) { EXPECT_TRUE(server->stop()); std::lock_guard lock(mutex); stopped=true; cv.notify_all(); }
    });
    download_options options; options.resumable=true; auto h=client->download_file("data",root/"output",options); ASSERT_TRUE(h);
    { std::unique_lock lock(mutex); ASSERT_TRUE(cv.wait_for(lock,3s,[&]{return stopped;})); }
    ASSERT_TRUE(server->start({"127.0.0.1",port}));
    auto result=h.value().wait_for(5s); ASSERT_TRUE(result); ASSERT_TRUE(result.value().success) << result.value().error_message.value_or("");
    EXPECT_EQ(checksum::sha256_file(path).value(),checksum::sha256_file(root/"output").value());
}
TEST_F(PublicResumeTest, LostUploadCompletionAckReconcilesAlreadyPersistedOutput) {
    start(); connect(); auto state=interrupt_upload(); client.reset(); connect();
    upload_options options; options.resume_id=state.id; auto h=client->upload_file(root/"source","data",options); ASSERT_TRUE(h);
    auto result=h.value().wait_for(5s); ASSERT_TRUE(result); ASSERT_TRUE(result.value().success);
    client.reset();
    // Recreate the last durable client record as if the final server ACK was lost.
    detail::mark_prefix(state,state.total_chunks);
    resume_handler handler(resume_handler_config{root/"client-state"}); ASSERT_TRUE(handler.save_state(state));
    connect(); options.resume_id=state.id; auto again=client->upload_file(root/"source","data",options); ASSERT_TRUE(again);
    auto done=again.value().wait_for(5s); ASSERT_TRUE(done); ASSERT_TRUE(done.value().success) << done.value().error_message.value_or("");
    EXPECT_EQ(server->get_statistics().total_files_uploaded,1);
    EXPECT_TRUE(client->resumable_transfers().empty());
}
TEST_F(PublicResumeTest, ExplicitUploadCancellationDeletesTheDurableClientRecord) {
    start(); connect(); auto path=source(); std::atomic<uint64_t> id{};
    client->on_progress([&](const auto&){ if(auto handle=id.load()) (void)client->cancel_transfer(handle); });
    upload_options options; options.resumable=true; auto h=client->upload_file(path,"cancelled",options); ASSERT_TRUE(h); id=h.value().get_id();
    auto result=h.value().wait_for(5s); ASSERT_TRUE(result); EXPECT_FALSE(result.value().success);
    EXPECT_EQ(h.value().get_status(),transfer_status::cancelled); EXPECT_TRUE(client->resumable_transfers().empty());
    EXPECT_FALSE(std::filesystem::exists(root/"server/cancelled"));
}
} // namespace kcenon::file_transfer::test
