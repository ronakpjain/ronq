#include "ronq/raw/command.hpp"

#include "ronq/core/color.hpp"
#include "ronq/core/pipe.hpp"

#include <array>
#include <cstdio>
#include <print>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

Command::Command(std::string_view cmd, std::string_view color) noexcept
    : command_{cmd}, color_{color} {}

[[nodiscard]] std::expected<void, CommandError> Command::execute() const {
    auto pipe_result = create_pipe();
    if (!pipe_result) {
        return std::unexpected{pipe_result.error()};
    }

    auto [read_fd, write_fd] = std::move(*pipe_result);

    pid_t child = ::fork();
    if (child == -1) {
        return std::unexpected{CommandError::ForkFailed};
    }

    if (child == 0) {
        read_fd.close();
        execute_in_child(std::move(write_fd));
    }

    write_fd.close();
    handle_output(std::move(read_fd));

    int status;
    ::waitpid(child, &status, 0);

    return {};
}

[[nodiscard]] std::string_view Command::name() const noexcept {
    return command_;
}

[[nodiscard]] std::string_view Command::color() const noexcept {
    return color_;
}

[[noreturn]] void
Command::execute_in_child(FileDescriptor write_fd) const noexcept {
    std::ignore = write_fd.duplicate_to(STDOUT_FILENO);
    std::ignore = write_fd.duplicate_to(STDERR_FILENO);
    write_fd.close();

    ::execl("/bin/sh", "sh", "-c", command_.data(),
            static_cast<char *>(nullptr));
    ::perror("exec failed");
    ::_exit(1);
}

void Command::handle_output(FileDescriptor read_fd) const {
    FILE *stream = ::fdopen(read_fd.get(), "r");
    if (!stream) {
        return;
    }

    read_fd.release();

    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), buffer.size(), stream)) {
        std::print("{}[{}]{} {}", color_, command_, Color::Reset,
                   buffer.data());
    }

    std::fclose(stream);
}
