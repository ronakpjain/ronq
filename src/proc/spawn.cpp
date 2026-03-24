#include "ronq/proc/spawn.hpp"

#include "ronq/core/color.hpp"
#include "ronq/core/errors.hpp"
#include "ronq/core/pipe.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <print>
#include <unistd.h>
#include <utility>

namespace {

[[noreturn]] void exec_shell_command(const std::string &cmd) noexcept {
    ::execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char *>(nullptr));
    ::perror("exec failed");
    ::_exit(127);
}

[[nodiscard]] bool set_process_group(pid_t pid, pid_t pgid) noexcept {
    if (::setpgid(pid, pgid) == 0) {
        return true;
    }

    if (errno == EACCES || errno == EPERM || errno == ESRCH) {
        return true;
    }

    return false;
}

[[noreturn]] void fail_child_setup(const char *message) noexcept {
    ::perror(message);
    ::_exit(127);
}

} // namespace

[[nodiscard]] std::expected<SpawnedProcess, std::string>
spawn_foreground(const std::string &cmd) {
    const pid_t pid = ::fork();
    if (pid == -1) {
        return std::unexpected{"Failed to fork foreground process"};
    }
    if (pid == 0) {
        if (::setpgid(0, 0) == -1) {
            fail_child_setup("setpgid failed for foreground process");
        }
        exec_shell_command(cmd);
    }

    if (!set_process_group(pid, pid)) {
        return std::unexpected{"Failed to configure foreground process group"};
    }

    return SpawnedProcess{.pid = pid, .pgid = pid};
}

[[nodiscard]] std::expected<BgProcess, std::string>
spawn_background(const std::string &cmd) {
    auto pipe_result = create_pipe();
    if (!pipe_result) {
        return std::unexpected{std::string{to_string(pipe_result.error())}};
    }
    auto [read_fd, write_fd] = std::move(*pipe_result);

    const int dev_null_fd = ::open("/dev/null", O_RDONLY);
    if (dev_null_fd == -1) {
        return std::unexpected{
            std::string{to_string(CommandError::OpenDevNullFailed)}};
    }
    FileDescriptor dev_null{dev_null_fd};

    const pid_t pid = ::fork();
    if (pid == -1) {
        return std::unexpected{
            std::string{to_string(CommandError::ForkFailed)}};
    }

    if (pid == 0) {
        read_fd.close();

        if (::setpgid(0, 0) == -1) {
            fail_child_setup("setpgid failed for background process");
        }

        if (!dev_null.duplicate_to(STDIN_FILENO)) {
            fail_child_setup("dup2 failed redirecting bg stdin");
        }
        if (!write_fd.duplicate_to(STDOUT_FILENO)) {
            fail_child_setup("dup2 failed redirecting bg stdout");
        }
        if (!write_fd.duplicate_to(STDERR_FILENO)) {
            fail_child_setup("dup2 failed redirecting bg stderr");
        }

        dev_null.close();
        write_fd.close();
        exec_shell_command(cmd);
    }

    if (!set_process_group(pid, pid)) {
        return std::unexpected{"Failed to configure background process group"};
    }

    write_fd.close();
    dev_null.close();
    return BgProcess{.pid = pid, .pgid = pid, .read_fd = std::move(read_fd)};
}

void stream_background_output(FileDescriptor read_fd,
                              std::string_view config_name) {
    FILE *stream = ::fdopen(read_fd.get(), "r");
    if (!stream) {
        return;
    }
    std::ignore = read_fd.release();

    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), buffer.size(), stream)) {
        std::print("{}[bg:{}]{} {}", Color::get(0), config_name, Color::Reset,
                   buffer.data());
    }
    std::fclose(stream);
}
