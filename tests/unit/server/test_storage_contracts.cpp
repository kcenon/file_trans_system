#include <gtest/gtest.h>
#include <kcenon/file_transfer/server/storage_manager.h>
#include "../../integration/cloud_storage_stub.h"
#include <fstream>

namespace kcenon::file_transfer::test {
namespace {
// An in-memory service boundary for testing the backend adapter's contracts.
// Unknown operations still throw through cloud_storage_stub.
class object_service : public cloud_storage_stub {
public:
    bool fail = false, connected = false;
    std::map<std::string, std::vector<std::byte>> objects;
    cloud_transfer_options last_options;
    std::string storage_class = "STANDARD";
    error failure{error_code::connection_failed, "Injected service outage"};
    auto connect() -> result<void> override {
        if (fail) return unexpected{failure};
        connected = true; return {};
    }
    auto disconnect() -> result<void> override {
        connected = false;
        if (fail) return unexpected{failure};
        return {};
    }
    auto is_connected() const -> bool override { return connected; }
    auto upload(const std::string& key, std::span<const std::byte> bytes,
                const cloud_transfer_options& options) -> result<upload_result> override {
        if (fail) return unexpected{failure};
        objects[key] = {bytes.begin(), bytes.end()}; last_options = options;
        upload_result out; out.key = key; out.bytes_uploaded = bytes.size(); out.etag = "etag";
        return out;
    }
    auto upload_file(const std::filesystem::path& file, const std::string& key,
                     const cloud_transfer_options& options) -> result<upload_result> override {
        if (fail) return unexpected{failure};
        std::ifstream input(file, std::ios::binary);
        if (!input) return unexpected{error{error_code::file_not_found, "Missing fixture"}};
        std::string bytes{std::istreambuf_iterator<char>(input), {}};
        return upload(key, std::as_bytes(std::span(bytes)), options);
    }
    auto download(const std::string& key) -> result<std::vector<std::byte>> override {
        if (fail) return unexpected{failure};
        if (!objects.contains(key)) return unexpected{error{error_code::file_not_found, "Missing object"}};
        return objects.at(key);
    }
    auto get_metadata(const std::string& key) -> result<cloud_object_metadata> override {
        auto bytes = download(key); if (!bytes) return unexpected{bytes.error()};
        cloud_object_metadata out; out.key = key; out.size = bytes.value().size();
        out.etag = "etag"; out.content_type = "application/octet-stream";
        out.storage_class = storage_class; out.custom_metadata = {{"owner", "fixture"}};
        return out;
    }
    auto download_file(const std::string& key, const std::filesystem::path& file)
        -> result<download_result> override {
        auto bytes = download(key); if (!bytes) return unexpected{bytes.error()};
        std::ofstream output(file, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
        if (!output) return unexpected{error{error_code::file_write_error, "Cannot write fixture"}};
        download_result out; out.key = key; out.bytes_downloaded = bytes.value().size();
        out.metadata = get_metadata(key).value(); return out;
    }
    auto exists(const std::string& key) -> result<bool> override {
        if (fail) return unexpected{failure};
        return objects.contains(key);
    }
    auto delete_object(const std::string& key) -> result<delete_result> override {
        if (fail) return unexpected{failure};
        objects.erase(key); delete_result out; out.key = key; return out;
    }
    auto list_objects(const list_objects_options& options) -> result<list_objects_result> override {
        if (fail) return unexpected{failure};
        list_objects_result out;
        for (const auto& [key, bytes] : objects)
            if (key.starts_with(options.prefix.value_or(""))) out.objects.push_back(get_metadata(key).value());
        out.total_count = out.objects.size(); return out;
    }
};

class StorageContractTest : public ::testing::Test {
protected:
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("fts-storage-contract-" + transfer_id::generate().to_string());
    std::vector<std::byte> bytes{std::byte{0}, std::byte{255}, std::byte{7}};
    void SetUp() override { std::filesystem::create_directories(root); }
    void TearDown() override { std::error_code ec; std::filesystem::remove_all(root, ec); }
};

TEST_F(StorageContractTest, CloudAdapterPreservesPrefixesOptionsMetadataAndAsyncBytes) {
    for (const auto type : {storage_backend_type::cloud_s3, storage_backend_type::cloud_azure,
                           storage_backend_type::cloud_gcs}) {
        SCOPED_TRACE(static_cast<int>(type));
        auto service = std::make_shared<object_service>();
        auto backend = cloud_storage_backend::create_shared(service, type, "tenant/");
        ASSERT_TRUE(backend); ASSERT_TRUE(backend->connect()); EXPECT_TRUE(backend->is_available());
        EXPECT_EQ(backend->type(), type); EXPECT_FALSE(backend->name().empty());
        std::vector<storage_progress> progress;
        backend->on_progress([&](const auto& value) { progress.push_back(value); });
        store_options options; options.content_type = "custom/type"; options.storage_class = "GLACIER";
        options.custom_metadata = {{"owner", "fixture"}}; options.tier = storage_tier::cold;
        auto stored = backend->store_async("object", bytes, options).get(); ASSERT_TRUE(stored);
        EXPECT_EQ(stored.value().bytes_stored, bytes.size()); EXPECT_EQ(stored.value().key, "object");
        EXPECT_EQ(stored.value().tier, storage_tier::cold); EXPECT_EQ(stored.value().etag, "etag");
        EXPECT_EQ(service->objects.at("tenant/object"), bytes);
        EXPECT_EQ(service->last_options.content_type, "custom/type");
        EXPECT_EQ(service->last_options.storage_class, "GLACIER");
        EXPECT_EQ(service->last_options.metadata, options.custom_metadata);
        auto fetched = backend->retrieve_async("object").get(); ASSERT_TRUE(fetched); EXPECT_EQ(fetched.value(), bytes);
        auto file = backend->retrieve_file_async("object", root / "download").get(); ASSERT_TRUE(file);
        EXPECT_EQ(file.value().bytes_retrieved, bytes.size()); EXPECT_EQ(file.value().metadata.key, "object");
        EXPECT_EQ(file.value().metadata.custom_metadata, options.custom_metadata);
        auto copied = backend->store_file_async("copy", root / "download", options).get(); ASSERT_TRUE(copied);
        EXPECT_EQ(service->objects.at("tenant/copy"), bytes);
        ASSERT_EQ(progress.size(), 4);
        for (const auto& value : progress) { EXPECT_EQ(value.bytes_transferred, bytes.size()); EXPECT_EQ(value.percentage(), 100); }
        for (const auto& [name, tier] : std::vector<std::pair<std::string, storage_tier>>{
            {"STANDARD", storage_tier::hot}, {"STANDARD_IA", storage_tier::warm}, {"ONEZONE_IA", storage_tier::warm},
            {"GLACIER", storage_tier::cold}, {"GLACIER_IR", storage_tier::cold}, {"DEEP_ARCHIVE", storage_tier::archive}}) {
            service->storage_class = name;
            auto metadata = backend->get_metadata("object"); ASSERT_TRUE(metadata);
            EXPECT_EQ(metadata.value().tier, tier); EXPECT_EQ(metadata.value().custom_metadata, options.custom_metadata);
        }
        auto listed = backend->list(); ASSERT_TRUE(listed); ASSERT_EQ(listed.value().objects.size(), 2);
        EXPECT_EQ(listed.value().objects.front().key, "copy");
        EXPECT_TRUE(backend->remove("object")); EXPECT_FALSE(backend->exists("object").value());
        EXPECT_TRUE(backend->disconnect()); EXPECT_FALSE(backend->is_available());
    }
}

TEST_F(StorageContractTest, ServiceOutagesPropagateThroughEveryBackendOperation) {
    auto service = std::make_shared<object_service>(); service->fail = true;
    auto backend = cloud_storage_backend::create_shared(service, storage_backend_type::cloud_s3);
    EXPECT_FALSE(cloud_storage_backend::create(nullptr, storage_backend_type::cloud_s3));
    EXPECT_FALSE(cloud_storage_backend::create_shared(nullptr, storage_backend_type::cloud_s3));
    EXPECT_FALSE(backend->connect()); EXPECT_FALSE(backend->disconnect());
    EXPECT_FALSE(backend->store("key", bytes)); EXPECT_FALSE(backend->store_file("key", root / "source"));
    EXPECT_FALSE(backend->retrieve("key")); EXPECT_FALSE(backend->retrieve_file("key", root / "output"));
    EXPECT_FALSE(backend->remove("key")); EXPECT_FALSE(backend->exists("key"));
    EXPECT_FALSE(backend->get_metadata("key")); EXPECT_FALSE(backend->list());
}

TEST_F(StorageContractTest, ManagerRequiresInitializationAndRollsBackFailedSecondaryConnection) {
    auto service = std::make_shared<object_service>(); service->fail = true;
    auto primary = std::make_shared<object_service>();
    storage_manager_config config;
    config.primary_backend = cloud_storage_backend::create_shared(primary, storage_backend_type::cloud_azure);
    config.secondary_backend = cloud_storage_backend::create_shared(service, storage_backend_type::cloud_s3);
    auto manager = storage_manager::create(config); ASSERT_TRUE(manager);
    EXPECT_FALSE(manager->store("key", bytes)); EXPECT_FALSE(manager->store_file("key", root / "file"));
    EXPECT_FALSE(manager->retrieve("key")); EXPECT_FALSE(manager->retrieve_file("key", root / "out"));
    EXPECT_FALSE(manager->exists("key")); EXPECT_FALSE(manager->get_metadata("key"));
    EXPECT_FALSE(manager->list()); EXPECT_FALSE(manager->remove("key"));
    EXPECT_FALSE(manager->change_tier("key", storage_tier::cold));
    EXPECT_FALSE(manager->copy_between_backends("key", storage_backend_type::local, storage_backend_type::cloud_s3));
    EXPECT_FALSE(manager->initialize()); EXPECT_FALSE(primary->connected);
    service->fail = false; EXPECT_TRUE(manager->initialize()); EXPECT_TRUE(manager->initialize());
    EXPECT_TRUE(manager->shutdown()); EXPECT_TRUE(manager->shutdown());
    config.primary_backend = config.secondary_backend; config.secondary_backend.reset();
    service->fail = true; auto failed_primary = storage_manager::create(config); EXPECT_FALSE(failed_primary->initialize());
}

TEST_F(StorageContractTest, HybridFallbackReplicationCopyAndTieringPreserveData) {
    auto service = std::make_shared<object_service>();
    storage_manager_config config; config.primary_backend = local_storage_backend::create(root / "local");
    config.secondary_backend = cloud_storage_backend::create_shared(service, storage_backend_type::cloud_s3);
    config.hybrid_storage = true; config.replicate_writes = true;
    auto manager = storage_manager::create(config); ASSERT_TRUE(manager->initialize());
    ASSERT_TRUE(manager->store("object", bytes)); EXPECT_EQ(service->objects.at("object"), bytes);
    ASSERT_TRUE(config.primary_backend->remove("object"));
    EXPECT_EQ(manager->retrieve("object").value(), bytes); EXPECT_TRUE(manager->exists("object").value());
    EXPECT_EQ(manager->get_metadata("object").value().size, bytes.size());
    ASSERT_TRUE(manager->retrieve_file_async("object", root / "copy").get());
    ASSERT_TRUE(manager->store_file_async("from-file", root / "copy").get());
    EXPECT_EQ(service->objects.at("from-file"), bytes);
    ASSERT_TRUE(manager->copy_between_backends("object", storage_backend_type::cloud_s3, storage_backend_type::local));
    EXPECT_EQ(config.primary_backend->retrieve("object").value(), bytes);
    EXPECT_TRUE(manager->copy_between_backends("object", storage_backend_type::local, storage_backend_type::cloud_s3));
    EXPECT_FALSE(manager->copy_between_backends("object", storage_backend_type::local, storage_backend_type::cloud_gcs));
    EXPECT_FALSE(manager->copy_between_backends("missing", storage_backend_type::local, storage_backend_type::cloud_s3));
    for (auto tier : {storage_tier::hot, storage_tier::warm, storage_tier::cold, storage_tier::archive}) {
        EXPECT_TRUE(manager->change_tier("object", tier)); EXPECT_EQ(service->objects.at("object"), bytes);
    }
    EXPECT_EQ(manager->get_statistics().tier_change_count, 4);
    EXPECT_FALSE(manager->change_tier("missing", storage_tier::hot));
    service->fail = true;
    EXPECT_FALSE(manager->store("failed-replica", bytes));
    EXPECT_FALSE(manager->copy_between_backends("object", storage_backend_type::local, storage_backend_type::cloud_s3));
    EXPECT_FALSE(manager->change_tier("object", storage_tier::warm));
    service->fail = false;
    EXPECT_TRUE(manager->remove("object")); EXPECT_FALSE(service->objects.contains("object"));
}

TEST_F(StorageContractTest, LocalAsyncFileOperationsReportMissingSourcesAndInvalidDestinations) {
    auto backend = local_storage_backend::create(root / "local"); ASSERT_TRUE(backend->connect());
    ASSERT_TRUE(backend->store_async("source", bytes).get());
    ASSERT_TRUE(backend->retrieve_file_async("source", root / "copied").get());
    ASSERT_TRUE(backend->store_file_async("second", root / "copied").get());
    EXPECT_EQ(backend->retrieve_async("second").get().value(), bytes);
    EXPECT_FALSE(backend->store_file("second", root / "copied"));
    EXPECT_FALSE(backend->store_file("missing", root / "absent"));
    EXPECT_FALSE(backend->store_file("directory", root));
    EXPECT_FALSE(backend->retrieve_file("absent", root / "output"));
    EXPECT_FALSE(backend->retrieve_file("source", root));
    EXPECT_FALSE(backend->retrieve_file("source", root / "copied/child"));
    EXPECT_FALSE(backend->remove("absent"));
    std::filesystem::create_directories(root / "local/directory");
    EXPECT_FALSE(backend->retrieve("directory"));
    EXPECT_FALSE(backend->retrieve_file("directory", root / "output"));
    EXPECT_FALSE(backend->get_metadata("directory"));
    EXPECT_FALSE(backend->store("directory", bytes));
    EXPECT_FALSE(backend->store("source/child", bytes));
}
} // namespace
} // namespace kcenon::file_transfer::test
