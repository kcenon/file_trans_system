#pragma once

#include "kcenon/file_transfer/core/checksum.h"
#include "kcenon/file_transfer/core/chunk_assembler.h"
#include "kcenon/file_transfer/core/chunk_config.h"
#include "kcenon/file_transfer/core/compression_engine.h"
#include "kcenon/file_transfer/core/protocol_codec.h"
#include "kcenon/file_transfer/core/transfer_checkpoint.h"
#include "kcenon/file_transfer/encryption/transfer_crypto.h"
#include "kcenon/file_transfer/server/server_pipeline.h"
#include "kcenon/file_transfer/server/server_types.h"
#include "kcenon/file_transfer/server/storage_manager.h"
#include <kcenon/file_transfer/transport/transfer_listener.h>

#include <condition_variable>
#include <deque>
#include <fstream>
#include <future>
#include <map>
#include <set>

namespace kcenon::file_transfer::detail {

inline auto hash_bytes(const std::string& hex) -> std::array<uint8_t, 32> {
    std::array<uint8_t, 32> out{};
    if (hex.size() != 64) throw std::invalid_argument("Invalid SHA-256 hash");
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
    return out;
}
inline auto hash_hex(const std::array<uint8_t, 32>& bytes) -> std::string {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (auto byte : bytes) { out += digits[byte >> 4]; out += digits[byte & 15]; }
    return out;
}

struct server_callbacks {
    std::function<bool(const upload_request&)> upload;
    std::function<bool(const download_request&)> download;
    std::function<void(const client_info&)> connected;
    std::function<void(const client_info&)> disconnected;
    std::function<void(const transfer_result&)> completed;
    std::function<void(const transfer_progress&)> progress;
};

// One ordered control executor owns transfer maps and quota reservations. Disk
// and pipeline work never blocks the network I/O executor. Downloads are
// acknowledged chunk by chunk, bounding both peers' in-flight buffers.
class server_runtime : public std::enable_shared_from_this<server_runtime> {
    using session = accepted_transport;
    struct operation {
        transfer_id id;
        session* owner{};
        std::string filename;
        std::filesystem::path path;
        uint64_t size{}, chunks{}, index{}, bytes{}, wire_bytes{}, previous_size{};
        std::string hash;
        wire_compression_mode compression{};
        std::shared_ptr<transfer_crypto> crypto;
        std::optional<transfer_state> checkpoint;
        bool upload{}, overwrite{}, existed{}, paused{}, already_committed{};
        std::set<uint64_t> received;
    };
    struct peer { std::weak_ptr<session> connection; client_info info; };
    struct task { std::function<void()> run; std::size_t bytes; };
    server_config config_;
    std::function<server_callbacks()> callbacks_;
    std::filesystem::path staging_;
    std::unique_ptr<storage_manager> storage_;
    std::unique_ptr<chunk_assembler> assembler_;
    std::unique_ptr<resume_handler> checkpoints_;
    std::map<transfer_id,uint64_t> retained_;
    std::unique_ptr<server_pipeline> pipeline_;
    std::map<session*, peer> peers_;
    std::map<transfer_id, operation> operations_;
    uint64_t next_client_{1}, reserved_{};
    mutable std::mutex stats_mutex_;
    server_statistics stats_{};
    storage_stats storage_stats_{};
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<task> tasks_;
    std::size_t queued_bytes_{};
    bool stopping_{true};
    std::thread worker_;
    std::mutex pipeline_mutex_;
    std::shared_ptr<std::promise<result<pipeline_chunk>>> pipeline_result_;

    template<class T> static auto read(const wire::frame& frame) -> T {
        auto value = wire::decode<T>(frame.payload);
        if (!value) throw std::invalid_argument(value.error().message);
        return std::move(value.value());
    }
    template<class T> auto send(session& client, message_type type, const T& value) -> bool {
        auto encoded = wire::encode(type, value);
        return encoded && client.send(std::move(encoded.value())).has_value();
    }
    template<class T> auto signed_send(session& client, message_type type, T value,
        const std::shared_ptr<transfer_crypto>& crypto) -> bool {
        if (crypto) { auto signed_value = crypto->sign(type, value); if (!signed_value) return false; }
        return send(client, type, value);
    }
    struct crypto_negotiation { std::shared_ptr<transfer_crypto> cipher; transfer_crypto::salt server_salt{}; };
    template<class T> auto negotiate_crypto(const T& request, bool upload) -> result<crypto_negotiation> {
        if (request.encryption == wire_encryption_algorithm::none) {
            if (config_.require_encryption) return unexpected{error{error_code::invalid_configuration, "Server requires encrypted transfers"}};
            return crypto_negotiation{};
        }
        if (!config_.enable_encryption || request.encryption != wire_encryption_algorithm::aes_256_gcm)
            return unexpected{error{error_code::invalid_configuration, "Requested encryption is unavailable"}};
        auto id = transfer_id{request.transfer_id};
        auto initial = transfer_crypto::create(config_.encryption_key, config_.encryption_password, id, request.encryption_salt, upload);
        if (!initial) return unexpected{initial.error()};
        auto verified = initial.value()->verify(upload ? message_type::upload_request : message_type::download_request, request);
        if (!verified) return unexpected{verified.error()};
        auto salt = transfer_crypto::random_salt(); if (!salt) return unexpected{salt.error()};
        auto accepted = transfer_crypto::create(config_.encryption_key, config_.encryption_password, id,
            request.encryption_salt, upload, &salt.value());
        if (!accepted) return unexpected{accepted.error()};
        return crypto_negotiation{std::move(accepted.value()), salt.value()};
    }
    void reject(session& client, message_type request, const transfer_id& id, const error& err) {
        if (request == message_type::upload_request)
            send(client, message_type::upload_reject, msg_upload_reject{id.bytes, static_cast<int32_t>(err.code), err.message});
        else if (request == message_type::download_request)
            send(client, message_type::download_reject, msg_download_reject{id.bytes, static_cast<int32_t>(err.code), err.message});
        else
            send(client, message_type::error, msg_error{static_cast<int32_t>(err.code), err.message, id.bytes});
    }
    auto valid_filename(const std::string& name) const -> bool {
        // The public file service is flat: no directory or platform-specific
        // path syntax, hidden staging names, or symlink targets are accepted.
        if (name.empty() || name == "." || name == ".." || name.size() > 255 ||
            name.find_first_of("/\\:") != std::string::npos ||
            std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; })) return false;
        if (config_.storage != storage_mode::cloud_only) {
            std::error_code ec;
            auto status = std::filesystem::symlink_status(config_.storage_directory / name, ec);
            if (!ec && (std::filesystem::is_symlink(status) || std::filesystem::is_directory(status))) return false;
        }
        return true;
    }
    auto snapshot_callbacks() const -> server_callbacks { return callbacks_(); }
    void report_progress(const operation& op) {
        auto cb = snapshot_callbacks().progress;
        if (cb) {
            try { cb({op.filename, op.bytes, op.size, op.size ? 100.0 * op.bytes / op.size : 100.0}); }
            catch (...) { /* observer exceptions do not change transfer state */ }
        }
    }
    void finish(transfer_id id, bool success, std::string message = {}, bool preserve = false) {
        auto it = operations_.find(id);
        if (it == operations_.end()) return;
        auto op = std::move(it->second);
        operations_.erase(it);
        preserve = preserve && op.checkpoint.has_value() && !success;
        if (op.upload) {
            if(!preserve) { if(!op.already_committed) reserved_ -= op.size; retained_.erase(id); }
            assembler_->cancel_session(id);
        }
        std::error_code ec;
        if(!preserve || !op.upload) std::filesystem::remove(op.path,ec);
        if(op.checkpoint && !preserve) (void)checkpoints_->delete_state(id);
        {
            std::lock_guard lock(stats_mutex_);
            --stats_.active_transfers;
            if (op.upload) --stats_.active_uploads; else --stats_.active_downloads;
            if (success && !op.already_committed) {
                if (op.upload) {
                    ++stats_.total_files_uploaded;
                    stats_.total_bytes_received += op.size;
                    storage_stats_.used_size = storage_stats_.used_size - op.previous_size + op.size;
                    if (!op.existed) ++storage_stats_.file_count;
                    storage_stats_.available_size = config_.storage_quota ?
                        config_.storage_quota - std::min(config_.storage_quota, storage_stats_.used_size) : UINT64_MAX;
                } else {
                    ++stats_.total_files_downloaded;
                    stats_.total_bytes_sent += op.size;
                }
            }
        }
        auto cb = snapshot_callbacks().completed;
        if (cb) {
            try { cb({success, op.filename, op.bytes, std::move(message)}); }
            catch (...) { /* the terminal outcome is already committed */ }
        }
    }
    void remove_peer(session* client) {
        auto it = peers_.find(client);
        if (it == peers_.end()) return;
        auto info = it->second.info;
        peers_.erase(it);
        std::vector<transfer_id> ids;
        for (const auto& [id, op] : operations_) if (op.owner == client) ids.push_back(id);
        for (auto id : ids) finish(id, false, "Client disconnected", true);
        { std::lock_guard lock(stats_mutex_); stats_.active_connections = peers_.size(); }
        auto cb = snapshot_callbacks().disconnected;
        if (cb) cb(info);
    }
    void fulfill_pipeline(result<pipeline_chunk> value) {
        std::shared_ptr<std::promise<result<pipeline_chunk>>> promise;
        { std::lock_guard lock(pipeline_mutex_); promise = std::exchange(pipeline_result_, {}); }
        if (promise) promise->set_value(std::move(value));
    }
    auto process_pipeline(const chunk& data) -> result<pipeline_chunk> {
        auto promise = std::make_shared<std::promise<result<pipeline_chunk>>>();
        auto future = promise->get_future();
        { std::lock_guard lock(pipeline_mutex_); pipeline_result_ = promise; }
        auto submitted = pipeline_->submit_upload_chunk(pipeline_chunk{data});
        if (!submitted) fulfill_pipeline(unexpected{submitted.error()});
        if (future.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
            // Stop before processing another chunk: a late callback must never
            // be mistaken for a different transfer's result.
            (void)pipeline_->stop(false);
            fulfill_pipeline(unexpected{error{error_code::transfer_timeout, "Pipeline deadline exceeded"}});
        }
        return future.get();
    }
    void begin_upload(session& client, const msg_upload_request& request) {
        transfer_id id{request.transfer_id};
        auto fail = [&](error_code code, std::string message) {
            reject(client, message_type::upload_request, id, error{code, std::move(message)});
        };
        if (id.is_null() || operations_.contains(id)) return fail(error_code::invalid_configuration, "Invalid or duplicate transfer ID");
        if (!valid_filename(request.filename)) return fail(error_code::invalid_file_path, "Invalid filename");
        auto negotiated = negotiate_crypto(request, true);
        if (!negotiated) return fail(negotiated.error().code, negotiated.error().message);
        if(request.resumable>1 || request.resuming>1 || (request.resuming && !request.resumable) ||
           (request.resumable && !checkpoints_) || (!request.resumable && request.resume_from))
            return fail(error_code::invalid_configuration,"Durable resume is not available or invalid");
        std::optional<transfer_state> checkpoint;
        uint64_t resume_index=0;
        bool already_committed=false;
        if(request.resuming) {
            auto loaded=load_checkpoint(*checkpoints_,id);
            if(!loaded && loaded.error().code==error_code::file_not_found) {
                auto stored=storage_->get_metadata(request.filename);
                if(stored && stored.value().size==request.file_size && stored.value().content_hash==hash_hex(request.sha256_hash)) {
                    checkpoint=new_checkpoint(id,request.filename,request.file_size,config_.chunk_size,hash_hex(request.sha256_hash),
                        true,static_cast<uint8_t>(request.compression),request.encryption!=wire_encryption_algorithm::none,
                        has_option(request.options,transfer_options::overwrite_existing));
                    mark_prefix(*checkpoint,checkpoint->total_chunks); already_committed=true;
                }
            }
            if(!loaded && !already_committed) return fail(loaded.error().code,loaded.error().message);
            if(loaded) checkpoint=loaded.value();
            auto prefix=checkpoint_prefix(*checkpoint);
            if(!prefix || !checkpoint->upload || checkpoint->filename!=request.filename ||
               checkpoint->total_size!=request.file_size || checkpoint->sha256!=hash_hex(request.sha256_hash) ||
               checkpoint->chunk_size!=config_.chunk_size || checkpoint->chunk_size!=request.resume_chunk_size ||
               checkpoint->compression!=static_cast<uint8_t>(request.compression) ||
               checkpoint->encrypted!=(request.encryption!=wire_encryption_algorithm::none) ||
               checkpoint->overwrite!=has_option(request.options,transfer_options::overwrite_existing) ||
               request.resume_from>prefix.value())
                return fail(error_code::invalid_configuration,"Upload checkpoint identity or prefix mismatch");
            if(!already_committed) {
                auto valid=validate_partial(config_.checkpoint_directory/(id.to_string()+".part"),*checkpoint);
                if(!valid) return fail(valid.error().code,valid.error().message);
            }
            resume_index=prefix.value();
        } else if(request.resumable && checkpoints_->has_state(id))
            return fail(error_code::already_initialized,"Checkpoint already exists; explicitly request resume");
        if (static_cast<uint8_t>(request.compression) > 2)
            return fail(error_code::invalid_configuration, "Invalid compression mode");
        if (config_.max_file_size && request.file_size > config_.max_file_size)
            return fail(error_code::file_too_large, "File exceeds configured maximum");
        auto usage = storage_statistics();
        const auto previously_reserved=retained_.contains(id) ? retained_.at(id) : 0;
        const auto other_reserved=reserved_-previously_reserved;
        if (!already_committed && (request.file_size > UINT64_MAX - other_reserved ||
            (config_.storage_quota && (usage.used_size > config_.storage_quota ||
             other_reserved > config_.storage_quota - usage.used_size ||
             request.file_size > config_.storage_quota - usage.used_size - other_reserved))))
            return fail(error_code::quota_exceeded, "Storage quota is reserved or exhausted");
        for (const auto& [_, active] : operations_)
            if (active.upload && active.filename == request.filename)
                return fail(error_code::file_already_exists, "An upload already owns this filename");
        auto exists = storage_->exists(request.filename);
        if (!exists) return fail(exists.error().code, exists.error().message);
        bool overwrite = has_option(request.options, transfer_options::overwrite_existing);
        if (exists.value() && !overwrite && !already_committed) return fail(error_code::file_already_exists, "File already exists");
        uint64_t previous_size = 0;
        if (exists.value()) {
            auto metadata = storage_->get_metadata(request.filename);
            if (!metadata) return fail(metadata.error().code, metadata.error().message);
            previous_size = metadata.value().size;
        }
        auto chunks = request.file_size / config_.chunk_size + (request.file_size % config_.chunk_size != 0);
        auto cb = snapshot_callbacks().upload;
        if (cb && !cb({request.filename, request.file_size, chunks, hash_hex(request.sha256_hash), peers_.at(&client).info.id}))
            return fail(error_code::file_access_denied, "Upload rejected by application");
        if(request.resumable && !checkpoint) {
            checkpoint=new_checkpoint(id,request.filename,request.file_size,config_.chunk_size,hash_hex(request.sha256_hash),
                true,static_cast<uint8_t>(request.compression),request.encryption!=wire_encryption_algorithm::none,overwrite);
            auto allocated=allocate_partial(config_.checkpoint_directory/(id.to_string()+".part"),request.file_size);
            if(!allocated) return fail(allocated.error().code,allocated.error().message);
            auto saved=checkpoints_->save_state(*checkpoint);
            if(!saved) {
                std::error_code ec; std::filesystem::remove(config_.checkpoint_directory/(id.to_string()+".part"),ec);
                return fail(saved.error().code,saved.error().message);
            }
        }
        if(!request.resumable) {
            auto started=assembler_->start_session(id,id.to_string(),request.file_size,chunks);
            if(!started) return fail(started.error().code,started.error().message);
        }
        operation op;
        op.id = id; op.owner = &client; op.filename = request.filename;
        op.path = staging_ / id.to_string(); op.size = request.file_size; op.chunks = chunks;
        op.hash = hash_hex(request.sha256_hash); op.upload = true; op.overwrite = overwrite;
        op.existed = exists.value(); op.previous_size = previous_size; op.compression = request.compression;
        op.crypto = negotiated.value().cipher; op.checkpoint=std::move(checkpoint);
        if(op.checkpoint) {
            op.path=config_.checkpoint_directory/(id.to_string()+".part");
            op.bytes=op.checkpoint->transferred_bytes;
            for(uint64_t i=0;i<resume_index;++i) op.received.insert(i);
            if(!already_committed) retained_[id]=op.size;
        }
        op.already_committed=already_committed;
        operations_.emplace(id, std::move(op));
        if(!already_committed) reserved_ += request.file_size-previously_reserved;
        { std::lock_guard lock(stats_mutex_); ++stats_.active_transfers; ++stats_.active_uploads; }
        msg_upload_accept accepted{id.bytes, request.compression, request.encryption, static_cast<uint32_t>(config_.chunk_size), std::min<uint64_t>(request.file_size,resume_index*config_.chunk_size)};
        accepted.resumable=request.resumable;
        accepted.encryption_salt = negotiated.value().server_salt;
        if (!signed_send(client, message_type::upload_accept, accepted, negotiated.value().cipher))
            finish(id, false, "Failed to send upload acceptance");
    }
    auto owned(session& client, const transfer_id& id) -> operation& {
        auto it = operations_.find(id);
        if (it == operations_.end() || it->second.owner != &client)
            throw std::invalid_argument("Unknown transfer for this session");
        return it->second;
    }
    void receive_chunk(session& client, chunk data) {
        auto& op = owned(client, data.header.id);
        const auto& h = data.header;
        if (!op.upload || op.paused || h.chunk_index >= op.chunks ||
            h.chunk_offset != h.chunk_index * config_.chunk_size ||
            h.original_size != std::min<uint64_t>(config_.chunk_size, op.size - h.chunk_offset) ||
            h.original_size > chunk_config::max_chunk_size || h.compressed_size > chunk_config::max_chunk_size + 65536 ||
            is_encrypted(h.flags) != static_cast<bool>(op.crypto) || (data.is_compressed() && op.compression == wire_compression_mode::none))
            throw std::invalid_argument("Chunk does not match accepted transfer metadata");
        const auto wire_size = h.compressed_size;
        if (op.crypto) { auto decrypted = op.crypto->open(data); if (!decrypted) throw std::invalid_argument(decrypted.error().message); }
        if (!op.received.contains(h.chunk_index)) {
            auto processed = process_pipeline(data);
            if (!processed) throw std::invalid_argument(processed.error().message);
            data.data = std::move(processed.value().data);
            data.header.flags &= ~chunk_flags::compressed;
            data.header.compressed_size = static_cast<uint32_t>(data.data.size());
            if(op.checkpoint) {
                if(h.chunk_index!=op.received.size()) throw std::invalid_argument("Resumable uploads require ordered chunks");
                std::fstream file(op.path,std::ios::binary|std::ios::in|std::ios::out);
                file.seekp(h.chunk_offset); file.write(reinterpret_cast<const char*>(data.data.data()),data.data.size()); file.close();
                if(!file) throw std::invalid_argument("Cannot write durable upload chunk");
                auto flushed=sync_file(op.path); if(!flushed) throw std::invalid_argument(flushed.error().message);
                auto checkpoint=*op.checkpoint; mark_prefix(checkpoint,h.chunk_index+1);
                checkpoint.prefix_hash=chain_hash(checkpoint.prefix_hash,data.data);
                auto saved=checkpoints_->save_state(checkpoint); if(!saved) throw std::invalid_argument(saved.error().message);
                op.checkpoint=std::move(checkpoint);
            } else {
                auto assembled=assembler_->process_chunk(data);
                if(!assembled) throw std::invalid_argument(assembled.error().message);
            }
            op.received.insert(h.chunk_index);
            op.bytes += h.original_size;
            op.wire_bytes += wire_size;
            report_progress(op);
        }
        if (!signed_send(client, message_type::chunk_ack, msg_chunk_ack{op.id.bytes, h.chunk_index}, op.crypto))
            finish(op.id, false, "Failed to acknowledge uploaded chunk");
    }
    void complete_upload(session& client, const msg_upload_complete& complete) {
        transfer_id id{complete.transfer_id};
        auto& op = owned(client, id);
        if (op.crypto) { auto verified = op.crypto->verify(message_type::upload_complete, complete); if (!verified) throw std::invalid_argument(verified.error().message); }
        if (!op.upload || complete.total_chunks != op.chunks || complete.bytes_sent != op.size ||
            op.bytes != op.size || op.received.size() != op.chunks || complete.bytes_on_wire != op.wire_bytes)
            throw std::invalid_argument("Upload is incomplete");
        if(op.already_committed) {
            auto filename=op.filename; auto crypto=op.crypto;
            finish(id,true);
            signed_send(client,message_type::upload_ack,msg_upload_ack{id.bytes,1,filename},crypto); return;
        }
        result<std::filesystem::path> assembled=op.path;
        if(op.checkpoint) {
            if(!checksum::verify_sha256(op.path,op.hash)) throw std::invalid_argument("Resumed upload SHA-256 mismatch");
        } else assembled=assembler_->finalize(id,op.hash);
        if(!assembled) throw std::invalid_argument(assembled.error().message);
        store_options options;
        options.overwrite = op.overwrite; options.content_hash = op.hash;
        auto stored = storage_->store_file(op.filename, assembled.value(), options);
        if (!stored) {
            // A hybrid backend may have persisted the primary before its
            // required replica failed. Account for those bytes despite the
            // failed transfer, so the next reservation cannot overcommit.
            auto actual = list_all();
            if (actual) {
                std::lock_guard lock(stats_mutex_);
                storage_stats_.used_size = 0; storage_stats_.file_count = actual.value().size();
                for (const auto& object : actual.value()) storage_stats_.used_size += object.size;
                storage_stats_.available_size = config_.storage_quota ? config_.storage_quota -
                    std::min(config_.storage_quota, storage_stats_.used_size) : UINT64_MAX;
            }
            throw std::invalid_argument(stored.error().message);
        }
        if (stored.value().bytes_stored != op.size) throw std::invalid_argument("Backend did not store the complete file");
        auto filename = op.filename;
        auto crypto = op.crypto;
        finish(id, true);
        signed_send(client, message_type::upload_ack, msg_upload_ack{id.bytes, 1, filename}, crypto);
    }
    void send_next(session& client, operation& op) {
        if (op.paused) return;
        if (op.index == op.chunks) {
            signed_send(client, message_type::download_complete,
                 msg_download_complete{op.id.bytes, op.chunks, op.bytes, op.wire_bytes}, op.crypto);
            return;
        }
        chunk data;
        auto& h = data.header;
        h.id = op.id; h.chunk_index = op.index; h.chunk_offset = op.index * config_.chunk_size;
        h.original_size = static_cast<uint32_t>(std::min<uint64_t>(config_.chunk_size, op.size - h.chunk_offset));
        data.data.resize(h.original_size);
        std::ifstream input(op.path, std::ios::binary);
        input.seekg(static_cast<std::streamoff>(h.chunk_offset));
        input.read(reinterpret_cast<char*>(data.data.data()), h.original_size);
        if (!input || input.gcount() != h.original_size) throw std::invalid_argument("Failed to read requested chunk");
        h.checksum = checksum::crc32(data.data);
        if (!op.index) h.flags |= chunk_flags::first_chunk;
        if (op.index + 1 == op.chunks) h.flags |= chunk_flags::last_chunk;
        if (op.compression != wire_compression_mode::none) {
            compression_engine engine;
            auto compressed = engine.compress(data.data);
            if (compressed && compressed.value().size() < data.data.size()) {
                data.data = std::move(compressed.value());
                h.flags |= chunk_flags::compressed;
            }
        }
        h.compressed_size = static_cast<uint32_t>(data.data.size());
        if (op.crypto) { auto encrypted = op.crypto->seal(data); if (!encrypted) throw std::invalid_argument(encrypted.error().message); }
        op.wire_bytes += h.compressed_size;
        if (!send(client, message_type::chunk_data, data)) throw std::invalid_argument("Failed to send download chunk");
    }
    void begin_download(session& client, const msg_download_request& request) {
        transfer_id id{request.transfer_id};
        auto fail = [&](error_code code, std::string message) {
            reject(client, message_type::download_request, id, error{code, std::move(message)});
        };
        if (id.is_null() || operations_.contains(id)) return fail(error_code::invalid_configuration, "Invalid or duplicate transfer ID");
        if (!valid_filename(request.filename)) return fail(error_code::invalid_file_path, "Invalid filename");
        auto negotiated = negotiate_crypto(request, false);
        if (!negotiated) return fail(negotiated.error().code, negotiated.error().message);
        if(request.resumable>1 || request.resuming>1 || (request.resuming && !request.resumable) ||
           (request.resumable && !checkpoints_) || (!request.resumable && request.resume_from))
            return fail(error_code::invalid_configuration,"Durable resume is not available or invalid");
        std::optional<transfer_state> checkpoint;
        if(request.resuming) {
            auto loaded=load_checkpoint(*checkpoints_,id);
            if(!loaded) return fail(loaded.error().code,loaded.error().message);
            checkpoint=loaded.value();
            if(!checkpoint_prefix(*checkpoint) || checkpoint->upload || checkpoint->filename!=request.filename ||
               checkpoint->chunk_size!=config_.chunk_size || checkpoint->chunk_size!=request.resume_chunk_size ||
               checkpoint->sha256!=hash_hex(request.resume_hash) || request.resume_from>checkpoint->total_chunks ||
               checkpoint->compression!=static_cast<uint8_t>(request.compression) ||
               checkpoint->encrypted!=(request.encryption!=wire_encryption_algorithm::none))
                return fail(error_code::invalid_configuration,"Download checkpoint identity mismatch");
        }
        if (static_cast<uint8_t>(request.compression) > 2) return fail(error_code::invalid_configuration, "Invalid compression mode");
        auto cb = snapshot_callbacks().download;
        if (cb && !cb({request.filename, peers_.at(&client).info.id})) return fail(error_code::file_access_denied, "Download rejected by application");
        auto exists = storage_->exists(request.filename);
        if (!exists) return fail(exists.error().code, exists.error().message);
        if (!exists.value()) return fail(error_code::file_not_found, "Requested file not found");
        auto path = staging_ / (id.to_string() + ".download");
        auto retrieved = storage_->retrieve_file(request.filename, path);
        if (!retrieved) return fail(retrieved.error().code, retrieved.error().message);
        auto hash = checksum::sha256_file(path);
        if (!hash) { std::error_code ec; std::filesystem::remove(path, ec); return fail(hash.error().code, hash.error().message); }
        if(checkpoint && (checkpoint->sha256!=hash.value() || checkpoint->total_size!=retrieved.value().bytes_retrieved)) {
            std::error_code ec; std::filesystem::remove(path,ec);
            return fail(error_code::file_hash_mismatch,"Download source changed since checkpoint");
        }
        if(request.resumable && !checkpoint) {
            checkpoint=new_checkpoint(id,request.filename,retrieved.value().bytes_retrieved,config_.chunk_size,hash.value(),
                false,static_cast<uint8_t>(request.compression),request.encryption!=wire_encryption_algorithm::none,false);
            auto saved=checkpoints_->save_state(*checkpoint); if(!saved) return fail(saved.error().code,saved.error().message);
        }
        operation op;
        op.id = id; op.owner = &client; op.filename = request.filename; op.path = path;
        op.size = retrieved.value().bytes_retrieved; op.hash = hash.value(); op.compression = request.compression;
        op.crypto = negotiated.value().cipher;
        op.chunks = op.size / config_.chunk_size + (op.size % config_.chunk_size != 0);
        op.checkpoint=std::move(checkpoint); op.index=request.resume_from;
        op.bytes=std::min<uint64_t>(op.size,op.index*config_.chunk_size);
        operations_.emplace(id, std::move(op));
        { std::lock_guard lock(stats_mutex_); ++stats_.active_transfers; ++stats_.active_downloads; }
        auto& active = operations_.at(id);
        msg_download_accept accepted{id.bytes, active.size, hash_bytes(active.hash), active.compression,
            request.encryption, static_cast<uint32_t>(config_.chunk_size), active.chunks, active.bytes, 0};
        accepted.resumable=request.resumable;
        accepted.encryption_salt = negotiated.value().server_salt;
        if (!signed_send(client, message_type::download_accept, accepted, active.crypto)) { finish(id, false, "Failed to accept download"); return; }
        send_next(client, active);
    }
    static auto glob(std::string_view pattern, std::string_view value) -> bool {
        std::size_t p = 0, v = 0, star = std::string_view::npos, match = 0;
        while (v < value.size()) {
            if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == value[v])) { ++p; ++v; }
            else if (p < pattern.size() && pattern[p] == '*') { star = p++; match = v; }
            else if (star != std::string_view::npos) { p = star + 1; v = ++match; }
            else return false;
        }
        while (p < pattern.size() && pattern[p] == '*') ++p;
        return p == pattern.size();
    }
    auto list_all() -> result<std::vector<stored_object_metadata>> {
        std::vector<stored_object_metadata> objects;
        list_storage_options options;
        options.max_results = 100000;
        for (;;) {
            auto page = storage_->list(options);
            if (!page) return unexpected{page.error()};
            for (auto& object : page.value().objects) objects.push_back(std::move(object));
            if (!page.value().is_truncated) break;
            auto token = page.value().continuation_token;
            if (!token || token == options.continuation_token)
                return unexpected{error{error_code::file_read_error, "Backend returned an incomplete listing without a continuation token"}};
            options.continuation_token = token;
        }
        return objects;
    }
    void list(session& client, const msg_list_request& request) {
        if (static_cast<uint8_t>(request.sort_by) > 2 || static_cast<uint8_t>(request.sort_order) > 1)
            throw std::invalid_argument("Invalid listing sort option");
        auto all = list_all();
        if (!all) { reject(client, message_type::list_request, transfer_id{request.request_id}, all.error()); return; }
        auto& objects = all.value();
        std::erase_if(objects, [&](const auto& object) {
            return !valid_filename(object.key) || !glob(request.pattern.empty() ? "*" : request.pattern, object.key);
        });
        std::sort(objects.begin(), objects.end(), [&](const auto& a, const auto& b) {
            bool less;
            if (request.sort_by == list_sort_field::size && a.size != b.size) less = a.size < b.size;
            else if (request.sort_by == list_sort_field::time && a.last_modified != b.last_modified) less = a.last_modified < b.last_modified;
            else less = a.key < b.key;
            return request.sort_order == list_sort_order::ascending ? less :
                (a.key != b.key && !less);
        });
        msg_list_response response{};
        response.request_id = request.request_id;
        response.total_count = static_cast<uint32_t>(objects.size());
        auto end = std::min<uint64_t>(objects.size(), uint64_t{request.offset} + std::min<uint32_t>(request.limit ? request.limit : 1000, 10000));
        for (uint64_t i = request.offset; i < end; ++i) {
            auto object = objects[i];
            if (!object.content_hash && object.backend == storage_backend_type::local) {
                auto metadata = storage_->get_metadata(object.key);
                if (!metadata) { reject(client, message_type::list_request, transfer_id{request.request_id}, metadata.error()); return; }
                object = std::move(metadata.value());
            }
            file_entry entry{};
            entry.filename = object.key; entry.file_size = object.size;
            entry.modified_time = std::chrono::duration_cast<std::chrono::seconds>(object.last_modified.time_since_epoch()).count();
            entry.created_time = entry.modified_time;
            if (object.content_hash) entry.sha256_hash = hash_bytes(*object.content_hash);
            response.entries.push_back(std::move(entry));
        }
        response.has_more = end < objects.size();
        send(client, message_type::list_response, response);
    }
    void serve(const std::shared_ptr<session>& client, const std::vector<uint8_t>& bytes) {
        if (!peers_.contains(client.get())) return;
        auto decoded = wire::decode_frame(bytes);
        if (!decoded) { reject(*client, message_type::error, {}, decoded.error()); return; }
        const auto& frame = decoded.value();
        transfer_id related;
        if (frame.payload.size() >= 16) std::copy_n(frame.payload.begin(), 16, related.bytes.begin());
        try {
            switch (frame.type) {
            case message_type::connect: {
                auto request = read<msg_connect>(frame);
                if (request.version != current_protocol_version) throw std::invalid_argument("Unsupported protocol version");
                send(*client, message_type::connect_ack, msg_connect_ack{current_protocol_version,
                    request.capabilities & client_capabilities::compression, transfer_id::generate().bytes,
                    static_cast<uint32_t>(config_.chunk_size), config_.max_file_size, "file_trans_system"});
                break;
            }
            case message_type::heartbeat: send(*client, message_type::heartbeat_ack, read<msg_heartbeat>(frame)); break;
            case message_type::upload_request: begin_upload(*client, read<msg_upload_request>(frame)); break;
            case message_type::chunk_data: receive_chunk(*client, read<chunk>(frame)); break;
            case message_type::upload_complete: complete_upload(*client, read<msg_upload_complete>(frame)); break;
            case message_type::download_request: begin_download(*client, read<msg_download_request>(frame)); break;
            case message_type::chunk_ack: {
                auto ack = read<msg_chunk_ack>(frame);
                auto& op = owned(*client, transfer_id{ack.transfer_id});
                if (op.crypto) { auto verified = op.crypto->verify(message_type::chunk_ack, ack); if (!verified) throw std::invalid_argument(verified.error().message); }
                if (op.upload || op.index >= op.chunks || ack.chunk_index != op.index) throw std::invalid_argument("Unexpected chunk acknowledgement");
                ++op.index; op.bytes = std::min<uint64_t>(op.size, op.index * config_.chunk_size);
                if(op.checkpoint) {
                    mark_prefix(*op.checkpoint,op.index);
                    auto saved=checkpoints_->save_state(*op.checkpoint); if(!saved) throw std::invalid_argument(saved.error().message);
                }
                report_progress(op); send_next(*client, op); break;
            }
            case message_type::download_ack: {
                auto ack = read<msg_download_ack>(frame);
                auto& op = owned(*client, transfer_id{ack.transfer_id});
                if (op.crypto) { auto verified = op.crypto->verify(message_type::download_ack, ack); if (!verified) throw std::invalid_argument(verified.error().message); }
                if (op.upload || op.index != op.chunks || ack.verified != 1 || ack.bytes_received != op.size)
                    throw std::invalid_argument("Download verification failed");
                finish(op.id, true); break;
            }
            case message_type::list_request: list(*client, read<msg_list_request>(frame)); break;
            case message_type::transfer_cancel: {
                auto request = read<msg_transfer_cancel>(frame);
                auto& op = owned(*client, transfer_id{request.transfer_id});
                finish(op.id, false, "Transfer cancelled"); break;
            }
            case message_type::transfer_pause: owned(*client, transfer_id{read<msg_transfer_pause>(frame).transfer_id}).paused = true; break;
            case message_type::transfer_resume: {
                auto& op = owned(*client, transfer_id{read<msg_transfer_resume>(frame).transfer_id});
                bool paused = std::exchange(op.paused, false);
                if (paused && !op.upload) send_next(*client, op); break;
            }
            case message_type::error: {
                auto err = read<msg_error>(frame);
                auto& op = owned(*client, transfer_id{err.related_id});
                finish(op.id, false, err.message); break;
            }
            case message_type::disconnect: client->close(); break;
            default: throw std::invalid_argument("Message is not supported in this state");
            }
        } catch (const std::exception& e) {
            reject(*client, frame.type, related, error{error_code::invalid_configuration, e.what()});
            auto it = operations_.find(related);
            if (it != operations_.end() && it->second.owner == client.get()) finish(related, false, e.what());
        }
    }

public:
    server_runtime(server_config config, std::function<server_callbacks()> callbacks)
        : config_(std::move(config)), callbacks_(std::move(callbacks)) {}
    ~server_runtime() { stop(); }

    auto start() -> result<void> {
        auto chunk_validation = chunk_config{config_.chunk_size}.validate();
        if (!chunk_validation) return chunk_validation;
        if(!config_.checkpoint_directory.empty()) {
            std::error_code ec; std::filesystem::create_directories(config_.checkpoint_directory,ec);
            if(ec) return unexpected{error{error_code::file_write_error,ec.message()}};
            resume_handler_config cfg(config_.checkpoint_directory); cfg.checkpoint_interval=1; cfg.auto_cleanup=false;
            checkpoints_=std::make_unique<resume_handler>(cfg);
            for(const auto& entry:std::filesystem::directory_iterator(config_.checkpoint_directory)) {
                if(entry.path().extension()!=".part" || entry.is_symlink() || !entry.is_regular_file()) continue;
                auto id=transfer_id::from_string(entry.path().stem().string()); if(!id) continue;
                auto size=entry.file_size(); if(size>UINT64_MAX-reserved_) return unexpected{error{error_code::quota_exceeded,"Retained resume quota overflow"}};
                retained_[*id]=size; reserved_+=size;
            }
        }
        staging_ = std::filesystem::temp_directory_path() / ("fts-" + transfer_id::generate().to_string());
        std::error_code ec;
        std::filesystem::create_directories(staging_, ec);
        if (ec) return unexpected{error{error_code::file_write_error, ec.message()}};
        assembler_ = std::make_unique<chunk_assembler>(staging_);
        storage_manager_config storage_config;
        if (config_.storage != storage_mode::cloud_only)
            storage_config.primary_backend = local_storage_backend::create(config_.storage_directory);
        if (config_.cloud_config) {
            const auto& cloud = *config_.cloud_config;
            auto backend = cloud_storage_backend::create_shared(cloud.cloud_storage, storage_backend_type::cloud_s3, cloud.key_prefix);
            if (config_.storage == storage_mode::cloud_only) storage_config.primary_backend = std::move(backend);
            else if (config_.storage == storage_mode::hybrid) storage_config.secondary_backend = std::move(backend);
            storage_config.hybrid_storage = config_.storage == storage_mode::hybrid;
            storage_config.replicate_writes = cloud.replicate_writes;
            storage_config.fallback_reads = cloud.fallback_reads;
            if (cloud.enable_cache) storage_config.cache_directory = cloud.cache_directory;
            storage_config.max_cache_size = cloud.max_cache_size;
        }
        storage_ = storage_manager::create(storage_config);
        if (!storage_) return unexpected{error{error_code::invalid_configuration, "Invalid storage configuration"}};
        auto initialized = storage_->initialize();
        if (!initialized) return initialized;
        auto objects = list_all();
        if (!objects) return unexpected{objects.error()};
        storage_stats_.total_capacity = config_.storage_quota;
        for (const auto& object : objects.value()) { storage_stats_.used_size += object.size; ++storage_stats_.file_count; }
        storage_stats_.available_size = config_.storage_quota ? config_.storage_quota - std::min(config_.storage_quota, storage_stats_.used_size) : UINT64_MAX;
        pipeline_config pipeline_config;
        pipeline_config.io_workers = pipeline_config.compression_workers = pipeline_config.network_workers = 1;
        auto pipeline = server_pipeline::create(pipeline_config);
        if (!pipeline) return unexpected{pipeline.error()};
        pipeline_ = std::make_unique<server_pipeline>(std::move(pipeline.value()));
        pipeline_->on_stage_complete([this](pipeline_stage stage, const pipeline_chunk& data) {
            if (stage == pipeline_stage::file_write) fulfill_pipeline(data);
        });
        pipeline_->on_error([this](pipeline_stage, const std::string& message) {
            fulfill_pipeline(unexpected{error{error_code::chunk_checksum_error, message}});
        });
        auto started = pipeline_->start();
        if (!started) return started;
        stopping_ = false;
        worker_ = std::thread([this] {
            for (;;) {
                task next;
                {
                    std::unique_lock lock(queue_mutex_);
                    queue_cv_.wait(lock, [&] { return stopping_ || !tasks_.empty(); });
                    if (stopping_ && tasks_.empty()) break;
                    next = std::move(tasks_.front()); tasks_.pop_front(); queued_bytes_ -= next.bytes;
                }
                try { next.run(); } catch (const std::exception&) { /* application callbacks cannot terminate the executor */ }
            }
            while (!operations_.empty()) finish(operations_.begin()->first, false, "Server stopped", true);
            while (!peers_.empty()) remove_peer(peers_.begin()->first);
        });
        return {};
    }
    auto post(std::function<void()> task, std::size_t bytes = 0) -> bool {
        std::lock_guard lock(queue_mutex_);
        if (stopping_ || (bytes && (tasks_.size() >= 256 || bytes > 8 * 1024 * 1024 - queued_bytes_))) return false;
        tasks_.push_back({std::move(task), bytes}); queued_bytes_ += bytes; queue_cv_.notify_one(); return true;
    }
    void connect(const std::shared_ptr<session>& client) {
        auto weak = weak_from_this();
        auto session_weak = std::weak_ptr<session>{client};
        // The dependency reports the same server_id for every session. Capture
        // the actual session identity rather than using that string as a key.
        client->on_disconnect([weak, identity = client.get()] {
            if (auto self = weak.lock()) self->post([self, identity] { self->remove_peer(identity); });
        });
        auto decoder = std::make_shared<wire::stream_decoder>();
        client->on_receive([weak, session_weak, decoder](const std::vector<uint8_t>& data) {
            auto self = weak.lock(); auto peer = session_weak.lock();
            if (!self || !peer) return;
            auto frames = decoder->feed(data);
            if (!frames) { self->reject(*peer, message_type::error, {}, frames.error()); return; }
            for (auto& frame : frames.value()) {
                auto size = frame.size();
                if (!self->post([self, peer, data = std::move(frame)] { self->serve(peer, data); }, size)) {
                    self->reject(*peer, message_type::error, {}, error{error_code::storage_full, "Server receive budget exhausted"});
                    self->post([peer] { peer->close(); }); return;
                }
            }
        });
        post([self = shared_from_this(), client] {
            if (self->peers_.size() >= self->config_.max_connections) {
                self->reject(*client, message_type::error, {}, error{error_code::connection_refused, "Connection limit exceeded"});
                client->close(); return;
            }
            client_info info{client_id{self->next_client_++}, "", 0};
            self->peers_[client.get()] = {client, info};
            { std::lock_guard lock(self->stats_mutex_); self->stats_.active_connections = self->peers_.size(); }
            auto cb = self->snapshot_callbacks().connected;
            if (cb) cb(info);
        });
    }
    void stop() {
        { std::lock_guard lock(queue_mutex_); stopping_ = true; queue_cv_.notify_all(); }
        if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) worker_.join();
        if (pipeline_ && pipeline_->is_running()) (void)pipeline_->stop(true);
        if (storage_) (void)storage_->shutdown();
        std::error_code ec;
        if (!staging_.empty()) std::filesystem::remove_all(staging_, ec);
    }
    auto statistics() const -> server_statistics { std::lock_guard lock(stats_mutex_); return stats_; }
    auto storage_statistics() const -> storage_stats { std::lock_guard lock(stats_mutex_); return storage_stats_; }
};
} // namespace kcenon::file_transfer::detail
