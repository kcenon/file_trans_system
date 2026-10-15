#include "protocol_peer.h"
#include <kcenon/file_transfer/client/file_transfer_client.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <kcenon/file_transfer/adapters/monitorable_adapter.h>
#include <kcenon/file_transfer/adapters/monitoring_adapter.h>
#include <kcenon/file_transfer/adapters/logger_adapter.h>
#include <fstream>

namespace kcenon::file_transfer::test {
using namespace adapters;
using health = common::interfaces::health_status;
static auto metric(const common::interfaces::metrics_snapshot& snapshot, std::string name) -> double {
    for (const auto& entry : snapshot.metrics) if (entry.name == name) return entry.value;
    ADD_FAILURE() << "Missing metric: " << name; return -1;
}
TEST(ObservabilityAdapterTest, TransferMetricsHealthQuotaAndExpiredOwnersReflectRealService) {
    auto root = std::filesystem::temp_directory_path() / ("fts-monitor-" + transfer_id::generate().to_string());
    struct cleanup { std::filesystem::path path; ~cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    auto s = file_transfer_server::builder().with_storage_directory(root / "server").with_storage_quota(10000).build();
    auto c = file_transfer_client::builder().build(); ASSERT_TRUE(s); ASSERT_TRUE(c);
    auto server = std::make_shared<file_transfer_server>(std::move(s.value()));
    auto client = std::make_shared<file_transfer_client>(std::move(c.value()));
    auto sm = file_transfer_server_monitorable::create(server, "server");
    auto cm = file_transfer_client_monitorable::create(client, "client");
    auto monitor = file_transfer_monitor_adapter::create(server, "custom");
    EXPECT_EQ(sm->health_check().value().status, health::unhealthy);
    EXPECT_EQ(cm->health_check().value().status, health::unhealthy);
    EXPECT_EQ(monitor->check_health().value().status, health::unhealthy);
    sm->set_component_name("server-renamed"); cm->set_component_name("client-renamed");
    EXPECT_EQ(sm->get_component_name(), "server-renamed"); EXPECT_EQ(cm->get_component_name(), "client-renamed");
    EXPECT_EQ(monitor->get_source_id(), "custom");
    auto port = available_port(); ASSERT_TRUE(server->start({"127.0.0.1", port})); ASSERT_TRUE(client->connect({"127.0.0.1", port}));
    EXPECT_EQ(sm->health_check().value().status, health::healthy);
    EXPECT_EQ(cm->health_check().value().status, health::healthy);
    for (auto size : {9000, 800}) {
        auto path = root / std::to_string(size); { std::ofstream out(path); out << std::string(size, 'x'); }
        auto up = client->upload_file(path, std::to_string(size)); ASSERT_TRUE(up);
        auto done = up.value().wait_for(5s); ASSERT_TRUE(done); ASSERT_TRUE(done.value().success);
        auto expected = size == 9000 ? health::degraded : health::unhealthy;
        EXPECT_EQ(sm->health_check().value().status, expected); EXPECT_EQ(monitor->check_health().value().status, expected);
    }
    ASSERT_TRUE(monitor->record_metric("queue", 3).is_ok());
    ASSERT_TRUE(monitor->record_metric("tagged", 7, {{"stage", "write"}}).is_ok());
    auto snapshot = monitor->get_metrics(); ASSERT_TRUE(snapshot.is_ok());
    EXPECT_EQ(metric(snapshot.value(), "queue"), 3); EXPECT_EQ(metric(snapshot.value(), "tagged"), 7);
    EXPECT_EQ(metric(snapshot.value(), "file_transfer.bytes_received"), 9800);
    EXPECT_EQ(metric(sm->get_monitoring_data().value(), "file_transfer.completed_uploads"), 2);
    EXPECT_EQ(metric(cm->get_monitoring_data().value(), "file_transfer.bytes_uploaded"), 9800);
    EXPECT_EQ(metric(cm->get_monitoring_data().value(), "file_transfer.connected"), 1);
    EXPECT_TRUE(monitor->reset().is_ok());
    auto reset_snapshot = monitor->get_metrics(); ASSERT_TRUE(reset_snapshot.is_ok());
    for (const auto& entry : reset_snapshot.value().metrics) EXPECT_NE(entry.name, "queue");
    auto quota = server->get_quota_usage();
    EXPECT_EQ(quota.used_bytes, 9800); EXPECT_EQ(quota.available_bytes, 200); EXPECT_EQ(quota.file_count, 2);
    EXPECT_TRUE(server->check_upload_allowed(200)); EXPECT_FALSE(server->check_upload_allowed(201));
    EXPECT_TRUE(client->disconnect()); EXPECT_TRUE(server->stop());
    EXPECT_TRUE(sm->is_server_available()); EXPECT_TRUE(cm->is_client_available()); EXPECT_TRUE(monitor->is_server_available());
    client.reset(); server.reset();
    EXPECT_FALSE(sm->is_server_available()); EXPECT_FALSE(cm->is_client_available()); EXPECT_FALSE(monitor->is_server_available());
    EXPECT_TRUE(sm->get_monitoring_data().is_err()); EXPECT_TRUE(cm->get_monitoring_data().is_err());
    EXPECT_EQ(sm->health_check().value().status, health::unhealthy);
    EXPECT_EQ(cm->health_check().value().status, health::unhealthy);
    EXPECT_EQ(monitor->check_health().value().status, health::unhealthy);
    EXPECT_TRUE(monitor->get_metrics().value().metrics.empty());
}

TEST(LoggerAdapterTest, FilteringContextMoveAndShutdownPreserveLoggingContract) {
    using level = common::interfaces::log_level;
    auto logger = file_transfer_logger_adapter::create(); ASSERT_TRUE(logger); EXPECT_TRUE(logger->initialize());
    EXPECT_TRUE(logger->set_level(level::warning).is_ok()); EXPECT_EQ(logger->get_level(), level::warning);
    EXPECT_FALSE(logger->is_enabled(level::debug)); EXPECT_TRUE(logger->is_enabled(level::error));
    transfer_context context; context.transfer_id = "test-transfer"; context.filename = "fixture";
    context.file_size = 42; context.client_id = "client"; context.server_address = "localhost";
    logger->set_transfer_context(context); EXPECT_TRUE(logger->has_transfer_context());
    EXPECT_EQ(logger->get_transfer_context().filename, "fixture");
    EXPECT_TRUE(logger->log(level::debug, std::string("filtered")).is_ok());
    EXPECT_TRUE(logger->log(level::debug, std::string_view("filtered location")).is_ok());
    common::interfaces::log_entry entry; entry.level = level::debug; entry.message = "filtered entry";
    EXPECT_TRUE(logger->log(entry).is_ok());
    EXPECT_TRUE(logger->log(level::warning, std::string("adapter contract fixture")).is_ok());
    EXPECT_TRUE(logger->log(level::warning, std::string_view("location fixture")).is_ok());
    entry.level = level::warning; entry.file = "fixture.cpp"; entry.function = "fixture"; entry.line = 12;
    EXPECT_TRUE(logger->log(entry).is_ok());
    file_transfer_logger_adapter moved(std::move(*logger)); EXPECT_TRUE(moved.has_transfer_context());
    file_transfer_logger_adapter assigned; assigned = std::move(moved); EXPECT_TRUE(assigned.is_initialized());
    assigned.clear_transfer_context(); EXPECT_FALSE(assigned.has_transfer_context());
    EXPECT_TRUE(assigned.flush().is_ok()); assigned.shutdown(); EXPECT_FALSE(assigned.is_initialized());

    // Distinct writer mutexes must still protect their shared console sink.
    auto first = file_transfer_logger_adapter::create();
    auto second = file_transfer_logger_adapter::create();
    ASSERT_TRUE(first->initialize()); ASSERT_TRUE(second->initialize());
    std::promise<void> release; auto ready = release.get_future().share();
    std::atomic<int> errors{0};
    auto write = [&](auto& writer) {
        ready.wait();
        for (int i = 0; i < 100; ++i) {
            if (!writer->log(level::warning, std::string("concurrent writer fixture")).is_ok()) ++errors;
            if (!writer->flush().is_ok()) ++errors;
        }
    };
    std::thread a([&] { write(first); }), b([&] { write(second); });
    release.set_value(); a.join(); b.join(); EXPECT_EQ(errors.load(), 0);
    first->shutdown(); second->shutdown();
}
} // namespace kcenon::file_transfer::test
