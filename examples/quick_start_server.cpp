#include <kcenon/file_transfer/file_transfer.h>
#include <iostream>

using namespace kcenon::file_transfer;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: quick_start_server STORAGE_DIRECTORY PORT\n";
        return 2;
    }
    try {
        const auto port = std::stoul(argv[2]);
        if (!port || port > 65535) return 2;
        auto built = file_transfer_server::builder()
            .with_storage_directory(argv[1])
            .with_max_connections(100)
            .build();
        if (!built) {
            std::cerr << built.error().message << '\n';
            return 1;
        }
        auto server = std::move(built.value());
        server.on_transfer_complete([](const transfer_result& result) {
            if (result.success)
                std::cout << "Verified transfer: " << result.filename << '\n';
            else
                std::cerr << "Transfer failed: " << result.error_message << '\n';
        });
        auto started = server.start({"127.0.0.1", static_cast<uint16_t>(port)});
        if (!started) {
            std::cerr << started.error().message << '\n';
            return 1;
        }
        std::cout << "READY; press Enter to stop." << std::endl;
        std::string line;
        std::getline(std::cin, line);
        auto stopped = server.stop();
        if (!stopped) std::cerr << stopped.error().message << '\n';
        return stopped ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
