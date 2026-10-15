#include <kcenon/file_transfer/file_transfer.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace kcenon::file_transfer;
using namespace std::chrono_literals;

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: quick_start_client PORT SOURCE_FILE DOWNLOAD_PATH\n";
        return 2;
    }
    try {
        const auto port = std::stoul(argv[1]);
        if (!port || port > 65535) return 2;
        auto built = file_transfer_client::builder()
            .with_compression(compression_mode::adaptive)
            .with_auto_reconnect(true, reconnect_policy{})
            .with_connect_timeout(5s)
            .with_transfer_timeout(30s)
            .build();
        if (!built) throw std::runtime_error(built.error().message);
        auto client = std::move(built.value());
        auto connected = client.connect({"127.0.0.1", static_cast<uint16_t>(port)});
        if (!connected) throw std::runtime_error(connected.error().message);

        auto wait_verified = [](result<transfer_handle> pending) {
            if (!pending) throw std::runtime_error(pending.error().message);
            auto finished = pending.value().wait_for(120s);
            if (!finished) throw std::runtime_error(finished.error().message);
            if (!finished.value().success)
                throw std::runtime_error(finished.value().error_message.value_or("Transfer failed"));
            return finished.value().bytes_transferred;
        };
        const auto uploaded = wait_verified(client.upload_file(argv[2], "quick-start.bin"));
        const auto downloaded = wait_verified(client.download_file("quick-start.bin", argv[3]));
        auto disconnected = client.disconnect();
        if (!disconnected) throw std::runtime_error(disconnected.error().message);
        std::cout << "Verified upload: " << uploaded
                  << " bytes; verified download: " << downloaded << " bytes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
