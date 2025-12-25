/**
 * @file test_session_resumption.cpp
 * @brief Unit tests for 0-RTT session resumption
 */

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
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
