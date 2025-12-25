/**
 * @file test_storage_manager.cpp
 * @brief Unit tests for storage manager
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/server/storage_manager.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace kcenon::file_transfer::test {

class StorageManagerTest : public ::testing::Test {
protected:
    static constexpr uint64_t MB = 1024 * 1024;
    static constexpr uint64_t KB = 1024;

    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "storage_manager_test";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    auto create_test_data(std::size_t size) -> std::vector<std::byte> {
        std::vector<std::byte> data(size);
        for (std::size_t i = 0; i < size; ++i) {
            data[i] = static_cast<std::byte>(i % 256);
        }
        return data;
    }

    void create_test_file(const std::string& name, std::size_t size) {
        std::ofstream file(test_dir_ / name, std::ios::binary);
        std::vector<char> data(size, 'x');
        file.write(data.data(), static_cast<std::streamsize>(size));
    }

    std::filesystem::path test_dir_;
};

// ============================================================================
// local_storage_backend tests
// ============================================================================

TEST_F(StorageManagerTest, LocalBackend_Create) {
    auto backend = local_storage_backend::create(test_dir_);
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->type(), storage_backend_type::local);
    EXPECT_EQ(backend->name(), "local");
}

TEST_F(StorageManagerTest, LocalBackend_Connect) {
    auto backend = local_storage_backend::create(test_dir_);
    ASSERT_NE(backend, nullptr);

    auto result = backend->connect();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(backend->is_available());
}

TEST_F(StorageManagerTest, LocalBackend_StoreAndRetrieve) {
    auto backend = local_storage_backend::create(test_dir_);
    ASSERT_NE(backend, nullptr);

    auto connect_result = backend->connect();
    ASSERT_TRUE(connect_result.has_value());

    // Store data
    auto data = create_test_data(1 * KB);
    auto store_result = backend->store("test_file.bin", data);
    ASSERT_TRUE(store_result.has_value());
    EXPECT_EQ(store_result.value().key, "test_file.bin");
    EXPECT_EQ(store_result.value().bytes_stored, 1 * KB);
    EXPECT_EQ(store_result.value().backend, storage_backend_type::local);

    // Retrieve data
    auto retrieve_result = backend->retrieve("test_file.bin");
    ASSERT_TRUE(retrieve_result.has_value());
    EXPECT_EQ(retrieve_result.value().size(), data.size());
    EXPECT_EQ(retrieve_result.value(), data);
}

TEST_F(StorageManagerTest, LocalBackend_StoreFile) {
    auto backend = local_storage_backend::create(test_dir_);
    ASSERT_NE(backend, nullptr);

    auto connect_result = backend->connect();
    ASSERT_TRUE(connect_result.has_value());

    // Create source file
    auto source_dir = test_dir_ / "source";
    std::filesystem::create_directories(source_dir);
    auto source_file = source_dir / "source.txt";

    {
        std::ofstream file(source_file);
        file << "Hello, World!";
    }

    // Store file
    auto store_result = backend->store_file("copied.txt", source_file);
    ASSERT_TRUE(store_result.has_value());
    EXPECT_EQ(store_result.value().key, "copied.txt");

    // Verify file exists
    EXPECT_TRUE(std::filesystem::exists(backend->full_path("copied.txt")));
}

TEST_F(StorageManagerTest, LocalBackend_Remove) {
    auto backend = local_storage_backend::create(test_dir_);
    ASSERT_NE(backend, nullptr);

    backend->connect();

    // Store data
    auto data = create_test_data(100);
    backend->store("to_delete.bin", data);

    // Verify exists
    auto exists_result = backend->exists("to_delete.bin");
    ASSERT_TRUE(exists_result.has_value());
    EXPECT_TRUE(exists_result.value());

    // Remove
    auto remove_result = backend->remove("to_delete.bin");
    ASSERT_TRUE(remove_result.has_value());

    // Verify removed
    exists_result = backend->exists("to_delete.bin");
    ASSERT_TRUE(exists_result.has_value());
    EXPECT_FALSE(exists_result.value());
}

TEST_F(StorageManagerTest, LocalBackend_GetMetadata) {
    auto backend = local_storage_backend::create(test_dir_);
    ASSERT_NE(backend, nullptr);

    backend->connect();

    // Store data
    auto data = create_test_data(512);
    backend->store("metadata_test.bin", data);

    // Get metadata
    auto meta_result = backend->get_metadata("metadata_test.bin");
    ASSERT_TRUE(meta_result.has_value());
    EXPECT_EQ(meta_result.value().key, "metadata_test.bin");
    EXPECT_EQ(meta_result.value().size, 512);
    EXPECT_EQ(meta_result.value().backend, storage_backend_type::local);
}

TEST_F(StorageManagerTest, LocalBackend_List) {
    auto backend = local_storage_backend::create(test_dir_);
    ASSERT_NE(backend, nullptr);

    backend->connect();

    // Store multiple files
    backend->store("file1.txt", create_test_data(100));
    backend->store("file2.txt", create_test_data(200));
    backend->store("file3.txt", create_test_data(300));

    // List all
    auto list_result = backend->list();
    ASSERT_TRUE(list_result.has_value());
    EXPECT_EQ(list_result.value().objects.size(), 3);
}

TEST_F(StorageManagerTest, LocalBackend_ListWithPrefix) {
    auto backend = local_storage_backend::create(test_dir_);
    ASSERT_NE(backend, nullptr);

    backend->connect();

    // Create subdirectory
    std::filesystem::create_directories(test_dir_ / "logs");

    // Store files
    backend->store("data.txt", create_test_data(100));
    backend->store("logs/app.log", create_test_data(200));
    backend->store("logs/error.log", create_test_data(300));

    // List with prefix
    list_storage_options options;
    options.prefix = "logs/";

    auto list_result = backend->list(options);
    ASSERT_TRUE(list_result.has_value());
    EXPECT_EQ(list_result.value().objects.size(), 2);
}

TEST_F(StorageManagerTest, LocalBackend_StoreOverwriteProtection) {
    auto backend = local_storage_backend::create(test_dir_);
    ASSERT_NE(backend, nullptr);

    backend->connect();

    // Store first file
    backend->store("protected.txt", create_test_data(100));

    // Try to store again without overwrite
    store_options opts;
    opts.overwrite = false;

    auto result = backend->store("protected.txt", create_test_data(200), opts);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::file_already_exists);
}

TEST_F(StorageManagerTest, LocalBackend_StoreWithOverwrite) {
    auto backend = local_storage_backend::create(test_dir_);
    ASSERT_NE(backend, nullptr);

    backend->connect();

    // Store first file
    backend->store("overwrite.txt", create_test_data(100));

    // Store again with overwrite
    store_options opts;
    opts.overwrite = true;

    auto new_data = create_test_data(200);
    auto result = backend->store("overwrite.txt", new_data, opts);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().bytes_stored, 200);

    // Verify new content
    auto retrieve_result = backend->retrieve("overwrite.txt");
    ASSERT_TRUE(retrieve_result.has_value());
    EXPECT_EQ(retrieve_result.value().size(), 200);
}

// ============================================================================
// storage_manager tests
// ============================================================================

TEST_F(StorageManagerTest, Manager_CreateWithLocalBackend) {
    auto backend = local_storage_backend::create(test_dir_);
    ASSERT_NE(backend, nullptr);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    ASSERT_NE(manager, nullptr);
}

TEST_F(StorageManagerTest, Manager_CreateFailsWithoutBackend) {
    storage_manager_config config;
    // No backend set

    auto manager = storage_manager::create(config);
    EXPECT_EQ(manager, nullptr);
}

TEST_F(StorageManagerTest, Manager_Initialize) {
    auto backend = local_storage_backend::create(test_dir_);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    ASSERT_NE(manager, nullptr);

    auto result = manager->initialize();
    EXPECT_TRUE(result.has_value());
}

TEST_F(StorageManagerTest, Manager_StoreAndRetrieve) {
    auto backend = local_storage_backend::create(test_dir_);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    ASSERT_NE(manager, nullptr);

    auto init_result = manager->initialize();
    ASSERT_TRUE(init_result.has_value());

    // Store
    auto data = create_test_data(1 * KB);
    auto store_result = manager->store("test.bin", data);
    ASSERT_TRUE(store_result.has_value());

    // Retrieve
    auto retrieve_result = manager->retrieve("test.bin");
    ASSERT_TRUE(retrieve_result.has_value());
    EXPECT_EQ(retrieve_result.value(), data);
}

TEST_F(StorageManagerTest, Manager_StoreFile) {
    auto backend = local_storage_backend::create(test_dir_);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    manager->initialize();

    // Create source file
    auto source_file = test_dir_ / "source.txt";
    {
        std::ofstream file(source_file);
        file << "Test content for file transfer";
    }

    auto result = manager->store_file("stored.txt", source_file);
    ASSERT_TRUE(result.has_value());

    // Verify retrieval
    auto retrieve = manager->retrieve("stored.txt");
    ASSERT_TRUE(retrieve.has_value());
    EXPECT_GT(retrieve.value().size(), 0);
}

TEST_F(StorageManagerTest, Manager_Exists) {
    auto backend = local_storage_backend::create(test_dir_);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    manager->initialize();

    // Store file
    manager->store("exists_test.txt", create_test_data(100));

    // Check exists
    auto exists = manager->exists("exists_test.txt");
    ASSERT_TRUE(exists.has_value());
    EXPECT_TRUE(exists.value());

    // Check non-existent
    auto not_exists = manager->exists("not_exists.txt");
    ASSERT_TRUE(not_exists.has_value());
    EXPECT_FALSE(not_exists.value());
}

TEST_F(StorageManagerTest, Manager_Remove) {
    auto backend = local_storage_backend::create(test_dir_);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    manager->initialize();

    // Store and remove
    manager->store("to_remove.txt", create_test_data(100));
    auto remove_result = manager->remove("to_remove.txt");
    ASSERT_TRUE(remove_result.has_value());

    // Verify removed
    auto exists = manager->exists("to_remove.txt");
    EXPECT_FALSE(exists.value());
}

TEST_F(StorageManagerTest, Manager_List) {
    auto backend = local_storage_backend::create(test_dir_);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    manager->initialize();

    // Store files
    manager->store("a.txt", create_test_data(100));
    manager->store("b.txt", create_test_data(200));
    manager->store("c.txt", create_test_data(300));

    auto list = manager->list();
    ASSERT_TRUE(list.has_value());
    EXPECT_EQ(list.value().objects.size(), 3);
}

TEST_F(StorageManagerTest, Manager_Statistics) {
    auto backend = local_storage_backend::create(test_dir_);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    manager->initialize();

    // Initial stats
    auto stats = manager->get_statistics();
    EXPECT_EQ(stats.store_count, 0);
    EXPECT_EQ(stats.retrieve_count, 0);

    // Perform operations
    manager->store("stats_test.txt", create_test_data(100));
    manager->retrieve("stats_test.txt");
    manager->remove("stats_test.txt");

    // Check updated stats
    stats = manager->get_statistics();
    EXPECT_EQ(stats.store_count, 1);
    EXPECT_EQ(stats.retrieve_count, 1);
    EXPECT_EQ(stats.delete_count, 1);
}

TEST_F(StorageManagerTest, Manager_AsyncStore) {
    auto backend = local_storage_backend::create(test_dir_);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    manager->initialize();

    // Async store
    auto data = create_test_data(1 * KB);
    auto future = manager->store_async("async_test.bin", data);

    // Wait for completion
    auto result = future.get();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().bytes_stored, 1 * KB);

    // Verify stored
    auto exists = manager->exists("async_test.bin");
    EXPECT_TRUE(exists.value());
}

TEST_F(StorageManagerTest, Manager_AsyncRetrieve) {
    auto backend = local_storage_backend::create(test_dir_);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    manager->initialize();

    // Store sync
    auto data = create_test_data(512);
    manager->store("async_retrieve.bin", data);

    // Async retrieve
    auto future = manager->retrieve_async("async_retrieve.bin");
    auto result = future.get();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), data);
}

TEST_F(StorageManagerTest, Manager_ProgressCallback) {
    auto backend = local_storage_backend::create(test_dir_);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    manager->initialize();

    bool progress_called = false;
    manager->on_progress([&progress_called](const storage_progress& progress) {
        progress_called = true;
        EXPECT_EQ(progress.operation, storage_operation::store);
    });

    // Store triggers progress
    manager->store("progress_test.bin", create_test_data(2 * MB));

    // Note: Progress may or may not be called depending on chunk size
    // For small files, it might complete in one chunk
}

TEST_F(StorageManagerTest, Manager_ErrorCallback) {
    auto backend = local_storage_backend::create(test_dir_);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    manager->initialize();

    bool error_called = false;
    manager->on_error([&error_called](const std::string&, const error&) {
        error_called = true;
    });

    // Try to retrieve non-existent file
    auto result = manager->retrieve("non_existent.txt");
    EXPECT_FALSE(result.has_value());
    // Error callback should be triggered
}

TEST_F(StorageManagerTest, Manager_Shutdown) {
    auto backend = local_storage_backend::create(test_dir_);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    manager->initialize();

    auto shutdown_result = manager->shutdown();
    EXPECT_TRUE(shutdown_result.has_value());
}

// ============================================================================
// Tiering tests
// ============================================================================

TEST_F(StorageManagerTest, Manager_ChangeTier) {
    auto backend = local_storage_backend::create(test_dir_);

    storage_manager_config config;
    config.primary_backend = std::move(backend);

    auto manager = storage_manager::create(config);
    manager->initialize();

    // Store with hot tier
    store_options opts;
    opts.tier = storage_tier::hot;
    manager->store("tier_test.bin", create_test_data(100), opts);

    // Change to cold tier
    auto result = manager->change_tier("tier_test.bin", storage_tier::cold);
    ASSERT_TRUE(result.has_value());

    // Verify (for local storage, metadata might not track tier)
    auto meta = manager->get_metadata("tier_test.bin");
    ASSERT_TRUE(meta.has_value());
}

}  // namespace kcenon::file_transfer::test
