#pragma once

#include <unistd.h>

#include <utility>

class FileDescriptor {
  public:
    FileDescriptor() noexcept = default;

    explicit FileDescriptor(int fd) noexcept : fd_{fd} {}

    ~FileDescriptor() { close(); }

    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;

    FileDescriptor(FileDescriptor &&other) noexcept
        : fd_{std::exchange(other.fd_, -1)} {}

    FileDescriptor &operator=(FileDescriptor &&other) noexcept {
        if (this != &other) {
            close();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return valid(); }

    int release() noexcept { return std::exchange(fd_, -1); }

    void close() noexcept {
        if (valid()) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    [[nodiscard]] bool duplicate_to(int target_fd) const noexcept {
        return valid() && ::dup2(fd_, target_fd) != -1;
    }

  private:
    int fd_ = -1;
};
