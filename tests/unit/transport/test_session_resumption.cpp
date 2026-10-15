/**
 * @file test_session_resumption.cpp
 * @brief Unit tests for 0-RTT session resumption
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <limits>
#include <filesystem>
#include <fstream>
#include <thread>

#include "kcenon/file_transfer/transport/session_resumption.h"

namespace kcenon::file_transfer {
namespace {

// ============================================================================
// Session Ticket Tests
// ============================================================================

class SessionTicketTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(SessionTicketTest, DefaultTicketIsInvalid) {
    session_ticket ticket;
    // Default ticket has expires_at in the past (epoch)
    EXPECT_FALSE(ticket.is_valid());
}

TEST_F(SessionTicketTest, ValidTicket) {
    session_ticket ticket;
    ticket.server_id = "example.com:443";
    ticket.ticket_data = {0x01, 0x02, 0x03, 0x04};
    ticket.issued_at = std::chrono::system_clock::now();
    ticket.expires_at = std::chrono::system_clock::now() + std::chrono::hours{24};
    ticket.max_early_data_size = 16384;

    EXPECT_TRUE(ticket.is_valid());
    EXPECT_TRUE(ticket.allows_early_data());
    EXPECT_GT(ticket.time_until_expiry().count(), 0);
}

TEST_F(SessionTicketTest, ExpiredTicket) {
    session_ticket ticket;
    ticket.server_id = "example.com:443";
    ticket.issued_at = std::chrono::system_clock::now() - std::chrono::hours{48};
    ticket.expires_at = std::chrono::system_clock::now() - std::chrono::hours{24};

    EXPECT_FALSE(ticket.is_valid());
    EXPECT_FALSE(ticket.allows_early_data());
    EXPECT_LT(ticket.time_until_expiry().count(), 0);
}

TEST_F(SessionTicketTest, TicketWithoutEarlyData) {
    session_ticket ticket;
    ticket.server_id = "example.com:443";
    ticket.expires_at = std::chrono::system_clock::now() + std::chrono::hours{24};
    ticket.max_early_data_size = 0;  // No early data

    EXPECT_TRUE(ticket.is_valid());
    EXPECT_FALSE(ticket.allows_early_data());
}

// ============================================================================
// Memory Session Store Tests
// ============================================================================

class MemorySessionStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        session_store_config config;
        config.max_tickets = 10;
        store_ = memory_session_store::create(config);
    }

    void TearDown() override {
        store_.reset();
    }

    session_ticket create_valid_ticket(const std::string& server_id) {
        session_ticket ticket;
        ticket.server_id = server_id;
        ticket.ticket_data = {0x01, 0x02, 0x03, 0x04};
        ticket.issued_at = std::chrono::system_clock::now();
        ticket.expires_at = std::chrono::system_clock::now() + std::chrono::hours{24};
        ticket.max_early_data_size = 16384;
        ticket.alpn_protocol = "file-transfer/1";
        return ticket;
    }

    std::unique_ptr<memory_session_store> store_;
};

TEST_F(MemorySessionStoreTest, StoreAndRetrieve) {
    auto ticket = create_valid_ticket("example.com:443");
    auto result = store_->store(ticket);
    EXPECT_TRUE(result.has_value());

    auto retrieved = store_->retrieve("example.com:443");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->server_id, "example.com:443");
    EXPECT_EQ(retrieved->ticket_data.size(), 4);
}

TEST_F(MemorySessionStoreTest, RetrieveNonExistent) {
    auto retrieved = store_->retrieve("nonexistent.com:443");
    EXPECT_FALSE(retrieved.has_value());
}

TEST_F(MemorySessionStoreTest, RemoveTicket) {
    auto ticket = create_valid_ticket("example.com:443");
    store_->store(ticket);

    EXPECT_TRUE(store_->has_ticket("example.com:443"));
    EXPECT_TRUE(store_->remove("example.com:443"));
    EXPECT_FALSE(store_->has_ticket("example.com:443"));
}

TEST_F(MemorySessionStoreTest, ClearAll) {
    store_->store(create_valid_ticket("server1.com:443"));
    store_->store(create_valid_ticket("server2.com:443"));
    store_->store(create_valid_ticket("server3.com:443"));

    EXPECT_EQ(store_->size(), 3);
    store_->clear();
    EXPECT_EQ(store_->size(), 0);
}

TEST_F(MemorySessionStoreTest, CleanupExpired) {
    // Store a valid ticket
    store_->store(create_valid_ticket("valid.com:443"));

    // Store an expired ticket
    session_ticket expired;
    expired.server_id = "expired.com:443";
    expired.expires_at = std::chrono::system_clock::now() - std::chrono::hours{1};
    store_->store(expired);

    EXPECT_EQ(store_->size(), 2);

    auto removed = store_->cleanup_expired();
    EXPECT_EQ(removed, 1);
    EXPECT_EQ(store_->size(), 1);
    EXPECT_TRUE(store_->has_ticket("valid.com:443"));
}

TEST_F(MemorySessionStoreTest, MaxTicketsEnforced) {
    session_store_config config;
    config.max_tickets = 3;
    auto limited_store = memory_session_store::create(config);

    limited_store->store(create_valid_ticket("server1.com:443"));
    limited_store->store(create_valid_ticket("server2.com:443"));
    limited_store->store(create_valid_ticket("server3.com:443"));
    limited_store->store(create_valid_ticket("server4.com:443"));

    // Should still have max 3 tickets
    EXPECT_EQ(limited_store->size(), 3);
    // server4 should be present (most recent)
    EXPECT_TRUE(limited_store->has_ticket("server4.com:443"));
}

TEST_F(MemorySessionStoreTest, UpdateExistingTicket) {
    auto ticket1 = create_valid_ticket("example.com:443");
    ticket1.ticket_data = {0x01};
    store_->store(ticket1);

    auto ticket2 = create_valid_ticket("example.com:443");
    ticket2.ticket_data = {0x02, 0x03};
    store_->store(ticket2);

    EXPECT_EQ(store_->size(), 1);

    auto retrieved = store_->retrieve("example.com:443");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->ticket_data.size(), 2);
}

// ============================================================================
// Session Resumption Manager Tests
// ============================================================================

class SessionResumptionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        session_resumption_config config;
        config.enable_0rtt = true;
        manager_ = session_resumption_manager::create(config);
    }

    void TearDown() override {
        manager_.reset();
    }

    std::unique_ptr<session_resumption_manager> manager_;
};

TEST_F(SessionResumptionManagerTest, StoreAndRetrieveTicket) {
    std::vector<uint8_t> ticket_data = {0x01, 0x02, 0x03, 0x04};
    auto result = manager_->store_ticket(
        "example.com", 443, ticket_data,
        std::chrono::hours{24}, 16384, "file-transfer/1");

    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(manager_->can_use_0rtt("example.com", 443));

    auto ticket = manager_->get_ticket_for_server("example.com", 443);
    ASSERT_TRUE(ticket.has_value());
    EXPECT_EQ(ticket->size(), 4);
}

TEST_F(SessionResumptionManagerTest, RemoveTicket) {
    std::vector<uint8_t> ticket_data = {0x01, 0x02};
    manager_->store_ticket("example.com", 443, ticket_data,
        std::chrono::hours{24}, 16384);

    EXPECT_TRUE(manager_->can_use_0rtt("example.com", 443));
    manager_->remove_ticket("example.com", 443);
    EXPECT_FALSE(manager_->can_use_0rtt("example.com", 443));
}

TEST_F(SessionResumptionManagerTest, On0RttRejected) {
    std::vector<uint8_t> ticket_data = {0x01, 0x02};
    manager_->store_ticket("example.com", 443, ticket_data,
        std::chrono::hours{24}, 16384);

    EXPECT_TRUE(manager_->can_use_0rtt("example.com", 443));

    // Simulate 0-RTT rejection
    manager_->on_0rtt_rejected("example.com", 443);

    // Ticket should be invalidated
    EXPECT_FALSE(manager_->can_use_0rtt("example.com", 443));
}

TEST_F(SessionResumptionManagerTest, CallbacksInvoked) {
    bool rejected_called = false;
    bool accepted_called = false;

    session_resumption_config config;
    config.enable_0rtt = true;
    config.on_0rtt_rejected = [&](const std::string&) { rejected_called = true; };
    config.on_0rtt_accepted = [&](const std::string&) { accepted_called = true; };

    auto manager = session_resumption_manager::create(config);

    std::vector<uint8_t> ticket_data = {0x01, 0x02};
    manager->store_ticket("example.com", 443, ticket_data,
        std::chrono::hours{24}, 16384);

    manager->on_0rtt_accepted("example.com", 443);
    EXPECT_TRUE(accepted_called);

    manager->on_0rtt_rejected("example.com", 443);
    EXPECT_TRUE(rejected_called);
}

TEST_F(SessionResumptionManagerTest, DisabledReturnsNoTicket) {
    session_resumption_config config;
    config.enable_0rtt = false;
    auto disabled_manager = session_resumption_manager::create(config);

    std::vector<uint8_t> ticket_data = {0x01, 0x02};
    disabled_manager->store_ticket("example.com", 443, ticket_data,
        std::chrono::hours{24}, 16384);

    // Even with stored ticket, should not be available when disabled
    auto ticket = disabled_manager->get_ticket_for_server("example.com", 443);
    EXPECT_FALSE(ticket.has_value());
}

TEST_F(SessionResumptionManagerTest, ClearAllTickets) {
    std::vector<uint8_t> ticket_data = {0x01, 0x02};
    manager_->store_ticket("server1.com", 443, ticket_data,
        std::chrono::hours{24}, 16384);
    manager_->store_ticket("server2.com", 443, ticket_data,
        std::chrono::hours{24}, 16384);

    EXPECT_TRUE(manager_->can_use_0rtt("server1.com", 443));
    EXPECT_TRUE(manager_->can_use_0rtt("server2.com", 443));

    manager_->clear_all_tickets();

    EXPECT_FALSE(manager_->can_use_0rtt("server1.com", 443));
    EXPECT_FALSE(manager_->can_use_0rtt("server2.com", 443));
}

// ============================================================================
// File Session Store Tests
// ============================================================================

class FileSessionStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_file_ = std::filesystem::temp_directory_path() /
                     ("test_sessions_" + std::to_string(
                         std::chrono::system_clock::now().time_since_epoch().count()) +
                      ".dat");
    }

    void TearDown() override {
        if (std::filesystem::exists(test_file_)) {
            std::filesystem::remove(test_file_);
        }
    }

    session_ticket create_valid_ticket(const std::string& server_id) {
        session_ticket ticket;
        ticket.server_id = server_id;
        ticket.ticket_data = {0x01, 0x02, 0x03, 0x04};
        ticket.issued_at = std::chrono::system_clock::now();
        ticket.expires_at = std::chrono::system_clock::now() + std::chrono::hours{24};
        ticket.max_early_data_size = 16384;
        ticket.alpn_protocol = "file-transfer/1";
        ticket.server_name = server_id.substr(0, server_id.find(':'));
        return ticket;
    }

    std::filesystem::path test_file_;
};

TEST_F(FileSessionStoreTest, CreateWithValidPath) {
    session_store_config config;
    config.storage_path = test_file_;

    auto store = file_session_store::create(config);
    ASSERT_NE(store, nullptr);
}

TEST_F(FileSessionStoreTest, CreateWithEmptyPath) {
    session_store_config config;
    // Empty path

    auto store = file_session_store::create(config);
    EXPECT_EQ(store, nullptr);
}

TEST_F(FileSessionStoreTest, StoreAndLoad) {
    session_store_config config;
    config.storage_path = test_file_;

    // Create store and save ticket
    {
        auto store = file_session_store::create(config);
        ASSERT_NE(store, nullptr);

        auto ticket = create_valid_ticket("example.com:443");
        store->store(ticket);
        store->save();
    }

    // Create new store and load
    {
        auto store = file_session_store::create(config);
        ASSERT_NE(store, nullptr);

        auto retrieved = store->retrieve("example.com:443");
        ASSERT_TRUE(retrieved.has_value());
        EXPECT_EQ(retrieved->server_id, "example.com:443");
    }
}

TEST_F(FileSessionStoreTest, PersistMultipleTickets) {
    session_store_config config;
    config.storage_path = test_file_;

    // Store multiple tickets
    {
        auto store = file_session_store::create(config);
        store->store(create_valid_ticket("server1.com:443"));
        store->store(create_valid_ticket("server2.com:8443"));
        store->store(create_valid_ticket("server3.com:443"));
    }

    // Reload and verify
    {
        auto store = file_session_store::create(config);
        EXPECT_EQ(store->size(), 3);
        EXPECT_TRUE(store->has_ticket("server1.com:443"));
        EXPECT_TRUE(store->has_ticket("server2.com:8443"));
        EXPECT_TRUE(store->has_ticket("server3.com:443"));
    }
}

TEST_F(MemorySessionStoreTest, RefreshAtCapacityPreservesOtherServers) {
    session_store_config config;
    config.max_tickets = 2;
    auto store = memory_session_store::create(config);
    auto oldest = create_valid_ticket("oldest:443");
    oldest.issued_at -= std::chrono::hours(1);
    ASSERT_TRUE(store->store(oldest));
    auto refreshed = create_valid_ticket("refresh:443");
    ASSERT_TRUE(store->store(refreshed));
    refreshed.ticket_data = {0, 255, 7};
    ASSERT_TRUE(store->store(refreshed));
    EXPECT_EQ(store->size(), 2);
    EXPECT_TRUE(store->has_ticket("oldest:443"));
    ASSERT_TRUE(store->retrieve("refresh:443"));
    EXPECT_EQ(store->retrieve("refresh:443")->ticket_data, refreshed.ticket_data);
}

TEST_F(FileSessionStoreTest, CapacityRefreshAndRemovalSurviveReloadWithExactTicketFields) {
    session_store_config config;
    config.storage_path = test_file_;
    config.max_tickets = 2;
    auto store = file_session_store::create(config);
    auto oldest = create_valid_ticket("oldest:443");
    oldest.issued_at -= std::chrono::hours(1);
    ASSERT_TRUE(store->store(oldest));
    auto refreshed = create_valid_ticket("refresh:8443");
    ASSERT_TRUE(store->store(refreshed));
    refreshed.ticket_data = {0, 255, 7};
    ASSERT_TRUE(store->store(refreshed));
    EXPECT_EQ(store->size(), 2);
    EXPECT_TRUE(store->has_ticket("oldest:443"));
    store.reset();
    store = file_session_store::create(config);
    auto ticket = store->retrieve("refresh:8443");
    ASSERT_TRUE(ticket);
    EXPECT_EQ(ticket->ticket_data, refreshed.ticket_data);
    EXPECT_EQ(ticket->alpn_protocol, refreshed.alpn_protocol);
    EXPECT_EQ(ticket->server_name, refreshed.server_name);
    EXPECT_EQ(ticket->max_early_data_size, refreshed.max_early_data_size);
    EXPECT_EQ(std::chrono::time_point_cast<std::chrono::milliseconds>(ticket->expires_at),
              std::chrono::time_point_cast<std::chrono::milliseconds>(refreshed.expires_at));
    EXPECT_TRUE(store->remove("oldest:443"));
    EXPECT_FALSE(store->remove("missing:443"));
    ASSERT_TRUE(store->save());
    store.reset();
    store = file_session_store::create(config);
    EXPECT_EQ(store->size(), 1);
    EXPECT_FALSE(store->has_ticket("oldest:443"));
    store->clear();
    store.reset();
    store = file_session_store::create(config);
    EXPECT_EQ(store->size(), 0);
}

TEST_F(FileSessionStoreTest, TruncatedLoadFailsWithoutReplacingValidInMemoryTickets) {
    session_store_config config;
    config.storage_path = test_file_;
    auto store = file_session_store::create(config);
    auto ticket = create_valid_ticket("kept:443");
    ASSERT_TRUE(store->store(ticket));
    std::ifstream input(test_file_, std::ios::binary);
    const std::string valid((std::istreambuf_iterator<char>(input)), {});
    input.close();
    ASSERT_GT(valid.size(), 12);
    for (std::size_t length : {std::size_t{0}, std::size_t{8}, std::size_t{12}, valid.size() - 1}) {
        SCOPED_TRACE(length);
        std::ofstream output(test_file_, std::ios::binary | std::ios::trunc);
        output.write(valid.data(), length);
        output.close();
        EXPECT_FALSE(store->load());
        auto kept = store->retrieve("kept:443");
        ASSERT_TRUE(kept);
        EXPECT_EQ(kept->ticket_data, ticket.ticket_data);
    }
}

TEST_F(FileSessionStoreTest, CorruptLengthsCountsVersionsAndTimestampsFailWithoutAllocation) {
    session_store_config config;
    config.storage_path = test_file_;
    auto store = file_session_store::create(config);
    auto ticket = create_valid_ticket("kept:443");
    ASSERT_TRUE(store->store(ticket));
    std::ifstream input(test_file_, std::ios::binary);
    const std::string valid((std::istreambuf_iterator<char>(input)), {});
    input.close();
    std::vector<std::string> corrupt;
    auto replace = [&](std::size_t offset, auto value) {
        auto bytes = valid;
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
        corrupt.push_back(std::move(bytes));
    };
    replace(0, uint32_t{0}); // Magic
    replace(4, uint32_t{99}); // Unsupported version
    replace(8, std::numeric_limits<uint32_t>::max()); // Ticket count
    replace(12, std::numeric_limits<uint32_t>::max()); // Server identifier size
    replace(16 + ticket.server_id.size(), std::numeric_limits<uint32_t>::max()); // Ticket size
    if (std::chrono::system_clock::period::den > 1000) {
        replace(20 + ticket.server_id.size() + ticket.ticket_data.size(),
                std::numeric_limits<int64_t>::max()); // Milliseconds must fit the native clock.
    }
    for (const auto& bytes : corrupt) {
        std::ofstream output(test_file_, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), bytes.size());
        output.close();
        EXPECT_FALSE(store->load());
        auto kept = store->retrieve("kept:443");
        ASSERT_TRUE(kept);
        EXPECT_EQ(kept->ticket_data, ticket.ticket_data);
    }
}

TEST_F(FileSessionStoreTest, ExpiredAndNearlyExpiredTicketsAreRemovedAndPersisted) {
    session_store_config config;
    config.storage_path = test_file_;
    config.max_tickets = 2;
    config.min_remaining_lifetime = std::chrono::seconds(60);
    auto store = file_session_store::create(config);
    auto expired = create_valid_ticket("expired:443");
    expired.expires_at = std::chrono::system_clock::now() - std::chrono::seconds(1);
    ASSERT_TRUE(store->store(expired));
    EXPECT_FALSE(store->retrieve("expired:443"));
    EXPECT_EQ(store->size(), 0);
    auto near = create_valid_ticket("near:443");
    near.expires_at = std::chrono::system_clock::now() + std::chrono::seconds(30);
    ASSERT_TRUE(store->store(near));
    EXPECT_FALSE(store->retrieve("near:443"));
    ASSERT_TRUE(store->store(expired));
    ASSERT_TRUE(store->store(create_valid_ticket("valid:443")));
    EXPECT_EQ(store->cleanup_expired(), 1);
    EXPECT_EQ(store->cleanup_expired(), 0);
    ASSERT_TRUE(store->save());
    ASSERT_TRUE(store->load());
    EXPECT_EQ(store->size(), 1);
    EXPECT_TRUE(store->has_ticket("valid:443"));
    EXPECT_FALSE(store->retrieve("missing:443"));
    // At capacity a new ticket may evict an expired entry, but never the live one.
    ASSERT_TRUE(store->store(expired));
    ASSERT_TRUE(store->store(create_valid_ticket("new:443")));
    EXPECT_EQ(store->size(), 2);
    EXPECT_TRUE(store->has_ticket("valid:443"));
    EXPECT_TRUE(store->has_ticket("new:443"));
}

TEST_F(FileSessionStoreTest, PersistenceFailuresAreReportedToTheCaller) {
    // A regular file used as the parent is a portable failure, even when running as root.
    std::ofstream(test_file_) << "parent is not a directory";
    session_store_config config;
    config.storage_path = test_file_ / "sessions.dat";
    auto store = file_session_store::create(config);
    ASSERT_NE(store, nullptr);
    auto stored = store->store(create_valid_ticket("server:443"));
    EXPECT_FALSE(stored);
    EXPECT_FALSE(store->save());
}

// ============================================================================
// Helper Function Tests
// ============================================================================

TEST(HelperFunctionTest, MakeServerId) {
    EXPECT_EQ(make_server_id("example.com", 443), "example.com:443");
    EXPECT_EQ(make_server_id("localhost", 8080), "localhost:8080");
    EXPECT_EQ(make_server_id("192.168.1.1", 443), "192.168.1.1:443");
}

}  // namespace
}  // namespace kcenon::file_transfer
