#pragma once
#include "resume_handler.h"
#include "checksum.h"
#include "chunk_config.h"
#include "file_commit.h"
#include <fstream>
#include <algorithm>

namespace kcenon::file_transfer::detail {
inline auto checkpoint_prefix(const transfer_state& state) -> result<uint64_t> {
    if(state.checkpoint_version!=1 || !chunk_config{state.chunk_size}.validate() ||
       state.compression>2 || state.sha256.size()!=64 || state.chunk_bitmap.size()!=state.total_chunks ||
       state.total_chunks!=state.total_size/state.chunk_size+(state.total_size%state.chunk_size!=0))
        return unexpected{error{error_code::invalid_configuration,"Incompatible transfer checkpoint"}};
    uint64_t prefix=0;
    while(prefix<state.chunk_bitmap.size() && state.chunk_bitmap[prefix]) ++prefix;
    if(std::any_of(state.chunk_bitmap.begin()+prefix,state.chunk_bitmap.end(),[](bool b){return b;}) ||
       state.transferred_bytes!=std::min<uint64_t>(state.total_size,prefix*state.chunk_size))
        return unexpected{error{error_code::invalid_configuration,"Invalid checkpoint prefix or byte count"}};
    return prefix;
}
inline auto chain_hash(std::string_view previous, std::span<const std::byte> bytes) -> std::string {
    auto text=std::string(previous)+checksum::sha256(bytes);
    return checksum::sha256(std::as_bytes(std::span{text.data(),text.size()}));
}
inline auto validate_partial(const std::filesystem::path& path, const transfer_state& state) -> result<void> {
    auto prefix=checkpoint_prefix(state); if(!prefix) return unexpected{prefix.error()};
    std::error_code ec;
    if(std::filesystem::is_symlink(path,ec) || !std::filesystem::is_regular_file(path,ec) ||
       std::filesystem::file_size(path,ec)!=state.total_size || ec)
        return unexpected{error{error_code::file_not_found,"Missing or incompatible partial file"}};
    std::ifstream file(path,std::ios::binary); std::vector<std::byte> buffer(state.chunk_size);
    std::string chain(64,'0');
    for(uint64_t i=0;i<prefix.value();++i) {
        auto length=static_cast<std::streamsize>(std::min<uint64_t>(state.chunk_size,state.total_size-i*state.chunk_size));
        file.read(reinterpret_cast<char*>(buffer.data()),length);
        if(!file || file.gcount()!=length) return unexpected{error{error_code::file_read_error,"Cannot read durable partial file"}};
        chain=chain_hash(chain,std::span{buffer}.first(length));
    }
    if(chain!=state.prefix_hash) return unexpected{error{error_code::file_hash_mismatch,"Partial file checkpoint hash mismatch"}};
    return {};
}
inline auto allocate_partial(const std::filesystem::path& path, uint64_t size) -> result<void> {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(size) { out.seekp(static_cast<std::streamoff>(size-1)); out.put('\0'); }
    out.close(); if(!out) return unexpected{error{error_code::file_write_error,"Cannot allocate partial file"}};
    auto flushed=sync_file(path); if(!flushed) return flushed; return sync_directory(path.parent_path());
}
inline auto new_checkpoint(const transfer_id& id,const std::string& name,uint64_t size,uint32_t chunk_size,
                           const std::string& hash,bool upload,uint8_t compression,bool encrypted,bool overwrite) -> transfer_state {
    auto chunks=size/chunk_size+(size%chunk_size!=0);
    if(chunks>3900000) throw std::invalid_argument("Transfer exceeds checkpoint bitmap budget");
    transfer_state state{id,name,size,static_cast<uint32_t>(chunks),hash};
    state.checkpoint_version=1; state.chunk_size=chunk_size; state.upload=upload;
    state.compression=compression; state.encrypted=encrypted; state.overwrite=overwrite; state.prefix_hash=std::string(64,'0');
    return state;
}
inline auto load_checkpoint(resume_handler& handler,const transfer_id& id) -> result<transfer_state> {
    // Restart/recovery reads the durable record, never an unsaved cache entry.
    resume_handler disk(handler.config()); return disk.load_state(id);
}
inline auto mark_prefix(transfer_state& state,uint64_t count) -> void {
    std::fill(state.chunk_bitmap.begin(),state.chunk_bitmap.end(),false);
    std::fill_n(state.chunk_bitmap.begin(),count,true);
    state.transferred_bytes=std::min<uint64_t>(state.total_size,count*state.chunk_size);
    state.last_activity=std::chrono::system_clock::now();
}
} // namespace kcenon::file_transfer::detail
