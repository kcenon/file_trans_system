#pragma once
#include <kcenon/file_transfer/cloud/cloud_storage_interface.h>
#include <stdexcept>
namespace kcenon::file_transfer::test {
// Explicit test-only backend. Unexpected operations fail immediately.
class cloud_storage_stub : public cloud_storage_interface {
public:
    auto provider() const -> cloud_provider override { throw std::logic_error("Unexpected cloud operation"); }
    auto provider_name() const -> std::string_view override { throw std::logic_error("Unexpected cloud operation"); }
    auto connect() -> result<void> override { throw std::logic_error("Unexpected cloud operation"); }
    auto disconnect() -> result<void> override { throw std::logic_error("Unexpected cloud operation"); }
    auto is_connected() const -> bool override { throw std::logic_error("Unexpected cloud operation"); }
    auto state() const -> cloud_storage_state override { throw std::logic_error("Unexpected cloud operation"); }
    auto upload( const std::string& key, std::span<const std::byte> data, const cloud_transfer_options& options = {}) -> result<upload_result> override { throw std::logic_error("Unexpected cloud operation"); }
    auto upload_file( const std::filesystem::path& local_path, const std::string& key, const cloud_transfer_options& options = {}) -> result<upload_result> override { throw std::logic_error("Unexpected cloud operation"); }
    auto download( const std::string& key) -> result<std::vector<std::byte>> override { throw std::logic_error("Unexpected cloud operation"); }
    auto download_file( const std::string& key, const std::filesystem::path& local_path) -> result<download_result> override { throw std::logic_error("Unexpected cloud operation"); }
    auto delete_object( const std::string& key) -> result<delete_result> override { throw std::logic_error("Unexpected cloud operation"); }
    auto delete_objects( const std::vector<std::string>& keys) -> result<std::vector<delete_result>> override { throw std::logic_error("Unexpected cloud operation"); }
    auto exists( const std::string& key) -> result<bool> override { throw std::logic_error("Unexpected cloud operation"); }
    auto get_metadata( const std::string& key) -> result<cloud_object_metadata> override { throw std::logic_error("Unexpected cloud operation"); }
    auto list_objects( const list_objects_options& options = {}) -> result<list_objects_result> override { throw std::logic_error("Unexpected cloud operation"); }
    auto copy_object( const std::string& source_key, const std::string& dest_key, const cloud_transfer_options& options = {}) -> result<cloud_object_metadata> override { throw std::logic_error("Unexpected cloud operation"); }
    auto upload_async( const std::string& key, std::span<const std::byte> data, const cloud_transfer_options& options = {}) -> std::future<result<upload_result>> override { throw std::logic_error("Unexpected cloud operation"); }
    auto upload_file_async( const std::filesystem::path& local_path, const std::string& key, const cloud_transfer_options& options = {}) -> std::future<result<upload_result>> override { throw std::logic_error("Unexpected cloud operation"); }
    auto download_async( const std::string& key) -> std::future<result<std::vector<std::byte>>> override { throw std::logic_error("Unexpected cloud operation"); }
    auto download_file_async( const std::string& key, const std::filesystem::path& local_path) -> std::future<result<download_result>> override { throw std::logic_error("Unexpected cloud operation"); }
    auto create_upload_stream( const std::string& key, const cloud_transfer_options& options = {}) -> std::unique_ptr<cloud_upload_stream> override { throw std::logic_error("Unexpected cloud operation"); }
    auto create_download_stream( const std::string& key) -> std::unique_ptr<cloud_download_stream> override { throw std::logic_error("Unexpected cloud operation"); }
    auto generate_presigned_url( const std::string& key, const presigned_url_options& options = {}) -> result<std::string> override { throw std::logic_error("Unexpected cloud operation"); }
    void on_upload_progress( std::function<void(const upload_progress&)> callback) override { throw std::logic_error("Unexpected cloud operation"); }
    void on_download_progress( std::function<void(const download_progress&)> callback) override { throw std::logic_error("Unexpected cloud operation"); }
    void on_state_changed( std::function<void(cloud_storage_state)> callback) override { throw std::logic_error("Unexpected cloud operation"); }
    auto get_statistics() const -> cloud_storage_statistics override { throw std::logic_error("Unexpected cloud operation"); }
    void reset_statistics() override { throw std::logic_error("Unexpected cloud operation"); }
    auto config() const -> const cloud_storage_config& override { throw std::logic_error("Unexpected cloud operation"); }
    auto bucket() const -> std::string_view override { throw std::logic_error("Unexpected cloud operation"); }
    auto region() const -> std::string_view override { throw std::logic_error("Unexpected cloud operation"); }
};
}
