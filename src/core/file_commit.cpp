#include "kcenon/file_transfer/core/file_commit.h"
#include <fstream>
#include <cerrno>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kcenon::file_transfer {
auto commit_file(const std::filesystem::path& temporary,
                 const std::filesystem::path& destination,
                 bool overwrite) -> result<void> {
    std::error_code ec;
    if (!overwrite) {
        // Creating a link fails atomically if the destination already exists.
        std::filesystem::create_hard_link(temporary, destination, ec);
        if (!ec) {
            std::error_code cleanup;
            std::filesystem::remove(temporary, cleanup);
        }
    } else {
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            ec = std::error_code(GetLastError(), std::system_category());
#else
        std::filesystem::rename(temporary, destination, ec);
#endif
    }
    if (ec) return unexpected{error{ec == std::errc::file_exists ? error_code::file_already_exists : error_code::file_write_error,
        "Cannot commit verified file: " + ec.message()}};
    return {};
}
auto sync_file(const std::filesystem::path& path) -> result<void> {
#ifdef _WIN32
    auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE) return unexpected{error{error_code::file_write_error,"Cannot open file for durable flush"}};
    auto ok=FlushFileBuffers(handle); CloseHandle(handle);
    if(!ok) return unexpected{error{error_code::file_write_error,"Cannot flush file to stable storage"}};
#else
    int fd=::open(path.c_str(),O_RDWR);
    if(fd<0) return unexpected{error{error_code::file_write_error,"Cannot open file for durable flush"}};
    int status=::fsync(fd); int code=errno; ::close(fd);
    if(status) return unexpected{error{error_code::file_write_error,std::error_code(code,std::generic_category()).message()}};
#endif
    return {};
}
auto sync_directory(const std::filesystem::path& path) -> result<void> {
#ifndef _WIN32
    auto directory=path.empty() ? std::filesystem::path{"."} : path;
    int fd=::open(directory.c_str(),O_RDONLY|O_DIRECTORY);
    if(fd<0) return unexpected{error{error_code::file_write_error,"Cannot open checkpoint directory"}};
    int status=::fsync(fd); int code=errno; ::close(fd);
    if(status) return unexpected{error{error_code::file_write_error,std::error_code(code,std::generic_category()).message()}};
#else
    (void)path; // Replacement uses MoveFileExW with MOVEFILE_WRITE_THROUGH.
#endif
    return {};
}
auto durable_write(const std::filesystem::path& path, std::string_view contents) -> result<void> {
    auto temporary=path; temporary += "."+transfer_id::generate().to_string()+".tmp";
    struct cleanup { std::filesystem::path path; ~cleanup(){ std::error_code ec; std::filesystem::remove(path,ec); } } cleanup{temporary};
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    out.write(contents.data(),static_cast<std::streamsize>(contents.size())); out.close();
    if(!out) return unexpected{error{error_code::file_write_error,"Cannot write checkpoint"}};
    auto flushed=sync_file(temporary); if(!flushed) return flushed;
    auto committed=commit_file(temporary,path,true); if(!committed) return committed;
    return sync_directory(path.parent_path());
}

}
