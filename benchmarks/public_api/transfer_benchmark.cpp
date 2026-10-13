// Public TCP API benchmark. The Python driver controls fresh, separate processes.
#include <kcenon/file_transfer/file_transfer.h>
#include <kcenon/file_transfer/core/checksum.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <sys/resource.h>
#ifdef __APPLE__
#include <mach/mach.h>
#else
#include <unistd.h>
#endif
using namespace kcenon::file_transfer;
using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;
static double seconds(clock_type::duration duration) {
    return std::chrono::duration<double>(duration).count();
}
static uint64_t peak_rss() {
    rusage usage{}; if (getrusage(RUSAGE_SELF, &usage)) throw std::runtime_error("getrusage failed");
#ifdef __APPLE__
    return usage.ru_maxrss;
#else
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024;
#endif
}
static uint64_t current_rss() {
#ifdef __APPLE__
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        throw std::runtime_error("task_info failed");
    return info.resident_size;
#else
    std::ifstream input("/proc/self/statm"); uint64_t total{}, resident{};
    if (!(input >> total >> resident)) throw std::runtime_error("statm failed");
    return resident * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
#endif
}
static std::string quote(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 32) out += '?';
        else out += c;
    }
    return out + "\"";
}
template<class T> static T require(result<T>&& value) {
    if (!value) throw std::runtime_error(value.error().message);
    return std::move(value.value());
}
static void require(result<void>&& value) {
    if (!value) throw std::runtime_error(value.error().message);
}
struct sample {
    double elapsed{}, request_start{}, request_end{};
    uint64_t bytes{}, sent{}, received{};
    std::string sha256, error;
    std::vector<double> progress_intervals_ms;
};
int main(int argc, char** argv) {
    try {
        if (argc < 4) throw std::runtime_error("server ROOT PORT [HOST] | client ROOT PORT upload|download COUNT LIMIT none|adaptive|always [HOST]");
        std::string role = argv[1]; std::filesystem::path root = argv[2];
        unsigned long port_number = std::stoul(argv[3]);
        if (!port_number || port_number > 65535) throw std::runtime_error("Invalid port");
        auto port = static_cast<uint16_t>(port_number);
        if (role == "server") {
            auto server = require(file_transfer_server::builder()
                .with_storage_directory(root / "storage").with_max_connections(256)
                .with_max_file_size(2'000'000'000ULL).with_storage_quota(100'000'000'000ULL).build());
            require(server.start({argc > 4 ? argv[4] : "127.0.0.1", port}));
            const auto baseline = current_rss();
            { std::ofstream ready(root / "ready"); ready << baseline << '\n'; }
            std::string command; std::getline(std::cin, command);
            auto statistics = server.get_statistics();
            auto peak = peak_rss(); require(server.stop());
            std::ofstream output(root / "server.json");
            output << "{\"baseline_rss_bytes\":" << baseline << ",\"peak_rss_bytes\":" << peak
                   << ",\"files_uploaded\":" << statistics.total_files_uploaded
                   << ",\"files_downloaded\":" << statistics.total_files_downloaded << "}\n";
            return 0;
        }
        if (role != "client" || argc < 8) throw std::runtime_error("Invalid client arguments");
        std::string operation = argv[4], compression = argv[7];
        auto count = std::stoul(argv[5]); auto limit = std::stoull(argv[6]);
        if (!count || count > 256 || (operation != "upload" && operation != "download"))
            throw std::runtime_error("Invalid operation/client count");
        auto mode = compression == "none" ? compression_mode::none :
                    compression == "adaptive" ? compression_mode::adaptive : compression_mode::always;
        if (compression != "none" && compression != "adaptive" && compression != "always")
            throw std::runtime_error("Invalid compression");
        std::vector<std::unique_ptr<file_transfer_client>> clients;
        for (unsigned i = 0; i < count; ++i) {
            auto client = require(file_transfer_client::builder().with_compression(mode)
                .with_auto_reconnect(false).with_connect_timeout(10s).with_transfer_timeout(60s)
                .with_progress_interval(0ms).with_upload_bandwidth_limit(limit)
                .with_download_bandwidth_limit(limit).build());
            clients.push_back(std::make_unique<file_transfer_client>(std::move(client)));
            require(clients.back()->connect({argc > 8 ? argv[8] : "127.0.0.1", port}));
        }
        const auto baseline = current_rss(); // All clients connected, no transfer active.
        std::promise<void> start; auto gate = start.get_future().share();
        std::vector<std::future<sample>> jobs;
        for (unsigned i = 0; i < count; ++i) jobs.push_back(std::async(std::launch::async, [&, i] {
            sample result; auto& client = *clients[i]; std::mutex mutex;
            clock_type::time_point last{};
            client.on_progress([&](const auto&) {
                auto now = clock_type::now(); std::lock_guard lock(mutex);
                if (last != clock_type::time_point{}) result.progress_intervals_ms.push_back(1000 * seconds(now - last));
                last = now;
            });
            gate.wait(); const auto began = clock_type::now();
            result.request_start = seconds(began.time_since_epoch());
            try {
                auto pending = operation == "upload"
                    ? client.upload_file(root / "source", "object-" + std::to_string(i))
                    : client.download_file("source", root / ("output-" + std::to_string(i)));
                auto handle = require(std::move(pending));
                auto finished = require(handle.wait_for(600s));
                if (!finished.success) throw std::runtime_error(finished.error_message.value_or("Transfer failed"));
                result.request_end = seconds(clock_type::now().time_since_epoch());
                result.elapsed = result.request_end - result.request_start;
                result.bytes = finished.bytes_transferred;
                auto [sent, received] = client.network_bytes(); result.sent = sent; result.received = received;
                // Independent file digest is outside the API transfer timing.
                result.sha256 = require(checksum::sha256_file(operation == "upload"
                    ? root / "source" : root / ("output-" + std::to_string(i))));
            } catch (const std::exception& error) {
                result.request_end = seconds(clock_type::now().time_since_epoch());
                result.elapsed = result.request_end - result.request_start; result.error = error.what();
            }
            require(client.disconnect()); // Join callbacks before their captured state is destroyed.
            client.on_progress({});
            return result;
        }));
        const auto began = clock_type::now(); start.set_value();
        std::vector<sample> samples;
        for (auto& job : jobs) samples.push_back(job.get());
        const auto wall = seconds(clock_type::now() - began);
        std::ofstream out(root / "client.json"); out << std::setprecision(12);
        out << "{\"idle_rss_bytes\":" << baseline << ",\"peak_rss_bytes\":" << peak_rss()
            << ",\"driver_wall_seconds\":" << wall << ",\"transfers\":[";
        bool failed = false;
        for (unsigned i = 0; i < samples.size(); ++i) {
            const auto& s = samples[i]; if (i) out << ',';
            failed |= !s.error.empty();
            out << "{\"index\":" << i << ",\"elapsed_seconds\":" << s.elapsed
                << ",\"request_start_seconds\":" << s.request_start << ",\"request_end_seconds\":" << s.request_end
                << ",\"useful_payload_bytes\":" << s.bytes << ",\"network_sent_bytes\":" << s.sent
                << ",\"network_received_bytes\":" << s.received << ",\"sha256\":" << quote(s.sha256)
                << ",\"error\":" << quote(s.error) << ",\"progress_intervals_ms\":[";
            for (unsigned j = 0; j < s.progress_intervals_ms.size(); ++j) {
                if (j) out << ','; out << s.progress_intervals_ms[j];
            }
            out << "]}";
        }
        out << "]}\n";
        return failed ? 1 : 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
