#include <kcenon/file_transfer/client/file_transfer_client.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <kcenon/file_transfer/core/file_commit.h>
#include <kcenon/file_transfer/core/checksum.h>
#include <fstream>
#include <iostream>
#include <thread>
using namespace kcenon::file_transfer;
using namespace std::chrono_literals;
int main(int argc,char** argv) {
    if(argc!=7) return 2;
    const std::string mode=argv[1]; const std::filesystem::path root=argv[2];
    auto port=static_cast<uint16_t>(std::stoul(argv[3])); bool encrypted=std::string(argv[6])=="encrypted";
    std::vector<std::byte> key(32,std::byte{0x61});
    try {
        if(mode=="server") {
            auto builder=file_transfer_server::builder().with_storage_directory(root/"server")
                .with_checkpoint_directory(root/"server-state").with_chunk_size(65536);
            if(encrypted) builder.with_encryption_key(key);
            auto built=builder.build(); if(!built) throw std::runtime_error(built.error().message);
            auto server=std::move(built.value()); auto started=server.start({"127.0.0.1",port});
            if(!started) throw std::runtime_error(started.error().message);
            if(!durable_write(root/"server-ready","ready")) return 3;
            for(;;) std::this_thread::sleep_for(100ms);
        }
        auto builder=file_transfer_client::builder().with_checkpoint_directory(root/"client-state")
            .with_auto_reconnect(false).with_connect_timeout(2s).with_transfer_timeout(3s);
        if(encrypted) builder.with_encryption_key(key);
        auto built=builder.build(); if(!built) throw std::runtime_error(built.error().message);
        auto client=std::move(built.value()); auto connected=client.connect({"127.0.0.1",port});
        if(!connected) throw std::runtime_error(connected.error().message);
        uint64_t stop_after=std::stoull(argv[5]); auto started=std::chrono::steady_clock::now();
        int64_t first_progress_ms=-1;
        client.on_progress([&](const auto& progress) {
            if(first_progress_ms<0) first_progress_ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
            if(stop_after && progress.bytes_transferred>=stop_after*65536) {
                auto states=client.resumable_transfers(); if(states.size()!=1) std::_Exit(42);
                auto saved=durable_write(root/"interrupted-id",states[0].id.to_string());
                std::_Exit(saved ? 41 : 43); // Genuine crash: no client/destructor cleanup.
            }
        });
        std::optional<transfer_id> id;
        if(std::string(argv[4])!="new") { id=transfer_id::from_string(argv[4]); if(!id) return 4; }
        result<transfer_handle> transfer=unexpected{error{error_code::invalid_configuration,"Invalid mode"}};
        if(mode=="upload") {
            upload_options options; options.resumable=true; options.resume_id=id;
            transfer=client.upload_file(root/"source","data",options);
        } else if(mode=="download") {
            download_options options; options.resumable=true; options.resume_id=id;
            transfer=client.download_file("data",root/"output",options);
        }
        if(!transfer) throw std::runtime_error(transfer.error().message);
        auto result=transfer.value().wait_for(10s);
        if(!result || !result.value().success) throw std::runtime_error(result ? result.value().error_message.value_or("Transfer failed") : result.error().message);
        auto path=mode=="upload" ? root/"server/data" : root/"output";
        auto hash=checksum::sha256_file(path); if(!hash) throw std::runtime_error(hash.error().message);
        auto bytes=client.network_bytes();
        std::string report="{\"sha256\":\""+hash.value()+"\",\"bytes\":"+std::to_string(result.value().bytes_transferred)+
            ",\"first_progress_ms\":"+std::to_string(first_progress_ms)+",\"network_sent\":"+std::to_string(bytes.first)+
            ",\"network_received\":"+std::to_string(bytes.second)+"}\n";
        auto saved=durable_write(root/"result.json",report); if(!saved) return 5;
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
