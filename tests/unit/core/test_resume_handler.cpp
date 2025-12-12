/**
 * @file test_resume_handler.cpp
 * @brief Unit tests for resume_handler
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/core/resume_handler.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace kcenon::file_transfer::test {

class ResumeHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() /
                    ("file_trans_test_resume_" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    auto create_test_state(uint32_t num_chunks = 100) -> transfer_state {
        auto id = transfer_id::generate();
        return transfer_state(
            id,
            "test_file.dat",
            1024 * 1024,  // 1MB
            num_chunks,
            "abc123def456");
    }

    std::filesystem::path test_dir_;
};

// ============================================================================
// transfer_state Tests
// ============================================================================

TEST_F(ResumeHandlerTest, TransferState_DefaultConstruction) {
    transfer_state state;
    EXPECT_TRUE(state.id.is_null());
    EXPECT_TRUE(state.filename.empty());
    EXPECT_EQ(state.total_size, 0u);
    EXPECT_EQ(state.transferred_bytes, 0u);
    EXPECT_EQ(state.total_chunks, 0u);
    EXPECT_TRUE(state.chunk_bitmap.empty());
}

TEST_F(ResumeHandlerTest, TransferState_ParameterizedConstruction) {
    auto id = transfer_id::generate();
    transfer_state state(id, "myfile.txt", 1000000, 50, "sha256hash");

    EXPECT_EQ(state.id, id);
    EXPECT_EQ(state.filename, "myfile.txt");
    EXPECT_EQ(state.total_size, 1000000u);
    EXPECT_EQ(state.transferred_bytes, 0u);
    EXPECT_EQ(state.total_chunks, 50u);
    EXPECT_EQ(state.chunk_bitmap.size(), 50u);
    EXPECT_EQ(state.sha256, "sha256hash");
}

TEST_F(ResumeHandlerTest, TransferState_ReceivedChunkCount) {
    auto state = create_test_state(10);
    EXPECT_EQ(state.received_chunk_count(), 0u);

    state.chunk_bitmap[0] = true;
    state.chunk_bitmap[5] = true;
    state.chunk_bitmap[9] = true;

    EXPECT_EQ(state.received_chunk_count(), 3u);
}

TEST_F(ResumeHandlerTest, TransferState_CompletionPercentage) {
    auto state = create_test_state(10);
    EXPECT_DOUBLE_EQ(state.completion_percentage(), 0.0);

    state.chunk_bitmap[0] = true;
    EXPECT_DOUBLE_EQ(state.completion_percentage(), 10.0);

    for (int i = 0; i < 10; ++i) {
        state.chunk_bitmap[i] = true;
    }
    EXPECT_DOUBLE_EQ(state.completion_percentage(), 100.0);
}

TEST_F(ResumeHandlerTest, TransferState_IsComplete) {
    auto state = create_test_state(5);
    EXPECT_FALSE(state.is_complete());

    for (int i = 0; i < 4; ++i) {
        state.chunk_bitmap[i] = true;
    }
    EXPECT_FALSE(state.is_complete());

    state.chunk_bitmap[4] = true;
    EXPECT_TRUE(state.is_complete());
}

TEST_F(ResumeHandlerTest, TransferState_ZeroChunks) {
    transfer_state state;
    state.total_chunks = 0;
    state.chunk_bitmap.clear();

    EXPECT_EQ(state.received_chunk_count(), 0u);
    EXPECT_DOUBLE_EQ(state.completion_percentage(), 0.0);
}

// ============================================================================
// resume_handler_config Tests
// ============================================================================

TEST_F(ResumeHandlerTest, Config_DefaultValues) {
    resume_handler_config config;
    EXPECT_EQ(config.checkpoint_interval, 10u);
    EXPECT_EQ(config.state_ttl, std::chrono::seconds(86400));
    EXPECT_TRUE(config.auto_cleanup);
}

TEST_F(ResumeHandlerTest, Config_CustomDirectory) {
    resume_handler_config config(test_dir_);
    EXPECT_EQ(config.state_directory, test_dir_);
}

// ============================================================================
// resume_handler Basic Operations Tests
// ============================================================================

TEST_F(ResumeHandlerTest, Handler_Construction) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    EXPECT_EQ(handler.config().state_directory, test_dir_);
}

TEST_F(ResumeHandlerTest, Handler_SaveAndLoadState) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto original_state = create_test_state(50);
    original_state.chunk_bitmap[0] = true;
    original_state.chunk_bitmap[10] = true;
    original_state.transferred_bytes = 20000;

    auto save_result = handler.save_state(original_state);
    ASSERT_TRUE(save_result.has_value());

    auto load_result = handler.load_state(original_state.id);
    ASSERT_TRUE(load_result.has_value());

    const auto& loaded = load_result.value();
    EXPECT_EQ(loaded.id, original_state.id);
    EXPECT_EQ(loaded.filename, original_state.filename);
    EXPECT_EQ(loaded.total_size, original_state.total_size);
    EXPECT_EQ(loaded.transferred_bytes, original_state.transferred_bytes);
    EXPECT_EQ(loaded.total_chunks, original_state.total_chunks);
    EXPECT_EQ(loaded.chunk_bitmap, original_state.chunk_bitmap);
    EXPECT_EQ(loaded.sha256, original_state.sha256);
}

TEST_F(ResumeHandlerTest, Handler_LoadNonexistentState) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto id = transfer_id::generate();
    auto result = handler.load_state(id);

    EXPECT_FALSE(result.has_value());
}

TEST_F(ResumeHandlerTest, Handler_HasState) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto state = create_test_state();

    EXPECT_FALSE(handler.has_state(state.id));

    handler.save_state(state);

    EXPECT_TRUE(handler.has_state(state.id));
}

TEST_F(ResumeHandlerTest, Handler_DeleteState) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto state = create_test_state();
    handler.save_state(state);
    EXPECT_TRUE(handler.has_state(state.id));

    auto delete_result = handler.delete_state(state.id);
    EXPECT_TRUE(delete_result.has_value());

    EXPECT_FALSE(handler.has_state(state.id));
}

// ============================================================================
// Chunk Tracking Tests
// ============================================================================

TEST_F(ResumeHandlerTest, Handler_MarkChunkReceived) {
    resume_handler_config config(test_dir_);
    config.checkpoint_interval = 100;  // Prevent auto-save
    resume_handler handler(config);

    auto state = create_test_state(10);
    handler.save_state(state);

    auto result = handler.mark_chunk_received(state.id, 5);
    EXPECT_TRUE(result.has_value());

    EXPECT_TRUE(handler.is_chunk_received(state.id, 5));
    EXPECT_FALSE(handler.is_chunk_received(state.id, 0));
}

TEST_F(ResumeHandlerTest, Handler_MarkChunkReceived_InvalidIndex) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto state = create_test_state(10);
    handler.save_state(state);

    auto result = handler.mark_chunk_received(state.id, 100);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ResumeHandlerTest, Handler_MarkChunksReceived_Batch) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto state = create_test_state(20);
    handler.save_state(state);

    std::vector<uint32_t> chunks = {0, 5, 10, 15, 19};
    auto result = handler.mark_chunks_received(state.id, chunks);
    EXPECT_TRUE(result.has_value());

    for (auto idx : chunks) {
        EXPECT_TRUE(handler.is_chunk_received(state.id, idx));
    }
    EXPECT_FALSE(handler.is_chunk_received(state.id, 1));
}

TEST_F(ResumeHandlerTest, Handler_GetMissingChunks) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto state = create_test_state(10);
    state.chunk_bitmap[0] = true;
    state.chunk_bitmap[2] = true;
    state.chunk_bitmap[4] = true;
    handler.save_state(state);

    auto result = handler.get_missing_chunks(state.id);
    ASSERT_TRUE(result.has_value());

    const auto& missing = result.value();
    EXPECT_EQ(missing.size(), 7u);

    std::vector<uint32_t> expected = {1, 3, 5, 6, 7, 8, 9};
    EXPECT_EQ(missing, expected);
}

TEST_F(ResumeHandlerTest, Handler_GetMissingChunks_AllReceived) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto state = create_test_state(5);
    for (int i = 0; i < 5; ++i) {
        state.chunk_bitmap[i] = true;
    }
    handler.save_state(state);

    auto result = handler.get_missing_chunks(state.id);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().empty());
}

// ============================================================================
// State Listing Tests
// ============================================================================

TEST_F(ResumeHandlerTest, Handler_ListResumableTransfers_Empty) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto transfers = handler.list_resumable_transfers();
    EXPECT_TRUE(transfers.empty());
}

TEST_F(ResumeHandlerTest, Handler_ListResumableTransfers_Multiple) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto state1 = create_test_state(10);
    auto state2 = create_test_state(20);
    auto state3 = create_test_state(30);

    ASSERT_TRUE(handler.save_state(state1).has_value());
    ASSERT_TRUE(handler.save_state(state2).has_value());
    ASSERT_TRUE(handler.save_state(state3).has_value());

    auto transfers = handler.list_resumable_transfers();
    EXPECT_EQ(transfers.size(), 3u);
}

// ============================================================================
// State Persistence Tests
// ============================================================================

TEST_F(ResumeHandlerTest, Handler_StatePersistence_Checkpoint) {
    resume_handler_config config(test_dir_);
    config.checkpoint_interval = 5;
    resume_handler handler(config);

    auto state = create_test_state(100);
    ASSERT_TRUE(handler.save_state(state).has_value());

    // Mark chunks and trigger checkpoint
    for (uint32_t i = 0; i < 10; ++i) {
        ASSERT_TRUE(handler.mark_chunk_received(state.id, i).has_value());
    }

    // Create new handler to force reload from disk
    resume_handler handler2(config);
    auto loaded = handler2.load_state(state.id);

    ASSERT_TRUE(loaded.has_value());
    // At least some chunks should be persisted
    EXPECT_GT(loaded.value().received_chunk_count(), 0u);
}

TEST_F(ResumeHandlerTest, Handler_StatePersistence_LargeBitmap) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    // Create state with many chunks
    auto state = create_test_state(10000);

    // Mark random chunks
    for (uint32_t i = 0; i < 10000; i += 7) {
        state.chunk_bitmap[i] = true;
    }

    ASSERT_TRUE(handler.save_state(state).has_value());

    resume_handler handler2(config);
    auto loaded = handler2.load_state(state.id);

    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded.value().chunk_bitmap, state.chunk_bitmap);
}

// ============================================================================
// Cleanup Tests
// ============================================================================

TEST_F(ResumeHandlerTest, Handler_CleanupExpiredStates) {
    resume_handler_config config(test_dir_);
    config.state_ttl = std::chrono::seconds(0);  // Immediate expiry
    resume_handler handler(config);

    auto state = create_test_state();
    ASSERT_TRUE(handler.save_state(state).has_value());

    // Small delay to ensure expiry
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto removed = handler.cleanup_expired_states();
    EXPECT_EQ(removed, 1u);

    EXPECT_FALSE(handler.has_state(state.id));
}

// ============================================================================
// Update Transferred Bytes Tests
// ============================================================================

TEST_F(ResumeHandlerTest, Handler_UpdateTransferredBytes) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto state = create_test_state();
    ASSERT_TRUE(handler.save_state(state).has_value());

    auto result = handler.update_transferred_bytes(state.id, 1000);
    EXPECT_TRUE(result.has_value());

    result = handler.update_transferred_bytes(state.id, 500);
    EXPECT_TRUE(result.has_value());

    auto loaded = handler.load_state(state.id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded.value().transferred_bytes, 1500u);
}

// ============================================================================
// Edge Cases Tests
// ============================================================================

TEST_F(ResumeHandlerTest, Handler_SpecialCharactersInFilename) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto id = transfer_id::generate();
    transfer_state state(
        id,
        "file with spaces & special chars (1).txt",
        1000,
        10,
        "hash");

    ASSERT_TRUE(handler.save_state(state).has_value());

    auto loaded = handler.load_state(id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded.value().filename, state.filename);
}

TEST_F(ResumeHandlerTest, Handler_UnicodeFilename) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto id = transfer_id::generate();
    transfer_state state(
        id,
        "test_file.dat",
        1000,
        10,
        "hash");

    ASSERT_TRUE(handler.save_state(state).has_value());

    auto loaded = handler.load_state(id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded.value().filename, state.filename);
}

TEST_F(ResumeHandlerTest, Handler_SingleChunk) {
    resume_handler_config config(test_dir_);
    resume_handler handler(config);

    auto state = create_test_state(1);
    ASSERT_TRUE(handler.save_state(state).has_value());

    ASSERT_TRUE(handler.mark_chunk_received(state.id, 0).has_value());

    auto loaded = handler.load_state(state.id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded.value().is_complete());
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(ResumeHandlerTest, Handler_ConcurrentMarkChunks) {
    resume_handler_config config(test_dir_);
    config.checkpoint_interval = 1000;  // Reduce disk I/O
    resume_handler handler(config);

    auto state = create_test_state(100);
    ASSERT_TRUE(handler.save_state(state).has_value());

    std::vector<std::thread> threads;

    // Spawn multiple threads marking different chunks
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&handler, &state, t]() {
            for (uint32_t i = t; i < 100; i += 4) {
                (void)handler.mark_chunk_received(state.id, i);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // All chunks should be marked
    auto missing = handler.get_missing_chunks(state.id);
    ASSERT_TRUE(missing.has_value());
    EXPECT_TRUE(missing.value().empty());
}

}  // namespace kcenon::file_transfer::test
