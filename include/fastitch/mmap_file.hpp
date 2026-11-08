#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <span>
#include <utility>

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define FASTITCH_HAS_POSIX_MMAP 1
#endif

#include "fastitch/result.hpp"

namespace fastitch {

class MmapFile {
public:
    MmapFile() noexcept = default;
    ~MmapFile() noexcept {
        close();
    }

    // Move-only semantics
    MmapFile(const MmapFile&) = delete;
    MmapFile& operator=(const MmapFile&) = delete;

    MmapFile(MmapFile&& other) noexcept
        : data_(other.data_), size_(other.size_), fd_(other.fd_), path_(std::move(other.path_)) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.fd_ = -1;
    }

    MmapFile& operator=(MmapFile&& other) noexcept {
        if (this != &other) {
            close();
            data_ = other.data_;
            size_ = other.size_;
            fd_ = other.fd_;
            path_ = std::move(other.path_);
            other.data_ = nullptr;
            other.size_ = 0;
            other.fd_ = -1;
        }
        return *this;
    }

    static Result<MmapFile> open(const std::string& path) noexcept {
#if defined(FASTITCH_HAS_POSIX_MMAP)
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            return ParseError{SourceLayer::IO, ErrorCode::IoError, 0, 0, "Failed to open file: " + path};
        }

        struct stat st;
        if (::fstat(fd, &st) != 0) {
            ::close(fd);
            return ParseError{SourceLayer::IO, ErrorCode::IoError, 0, 0, "Failed to stat file: " + path};
        }

        if (st.st_size == 0) {
            // An empty file has 0 bytes; return valid MmapFile with size 0
            MmapFile empty_file;
            empty_file.fd_ = fd;
            empty_file.path_ = path;
            return empty_file;
        }

        size_t file_size = static_cast<size_t>(st.st_size);
        void* addr = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED) {
            ::close(fd);
            return ParseError{SourceLayer::IO, ErrorCode::SystemError, 0, 0, "mmap failed for file: " + path};
        }

        // madvise is an optional performance hint; do not fail if it fails
        #if defined(MADV_SEQUENTIAL)
        ::madvise(addr, file_size, MADV_SEQUENTIAL);
        #endif

        MmapFile file;
        file.data_ = static_cast<const std::byte*>(addr);
        file.size_ = file_size;
        file.fd_ = fd;
        file.path_ = path;
        return file;
#else
        return ParseError{SourceLayer::IO, ErrorCode::SystemError, 0, 0, "POSIX mmap not supported on this platform"};
#endif
    }

    void close() noexcept {
#if defined(FASTITCH_HAS_POSIX_MMAP)
        if (data_ != nullptr && size_ > 0) {
            ::munmap(const_cast<void*>(static_cast<const void*>(data_)), size_);
            data_ = nullptr;
            size_ = 0;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return {data_, size_};
    }

    [[nodiscard]] const std::byte* data() const noexcept {
        return data_;
    }

    [[nodiscard]] size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] bool is_open() const noexcept {
        return fd_ >= 0;
    }

    [[nodiscard]] const std::string& path() const noexcept {
        return path_;
    }

private:
    const std::byte* data_{nullptr};
    size_t size_{0};
    int fd_{-1};
    std::string path_{};
};

} // namespace fastitch
