#include <array>
#include <cstdio>
#include <expected>
#include <format>
#include <iostream>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

// Error Types
enum class CommandError {
    PipeCreationFailed,
    ForkFailed,
    ExecFailed,
    OutputReadFailed
};

[[nodiscard]] constexpr std::string_view
to_string(CommandError error) noexcept {
    switch (error) {
    case CommandError::PipeCreationFailed:
        return "Failed to create pipe";
    case CommandError::ForkFailed:
        return "Failed to fork process";
    case CommandError::ExecFailed:
        return "Failed to execute command";
    case CommandError::OutputReadFailed:
        return "Failed to read command output";
    }
    std::unreachable();
}

// File Descriptor Wrapper
class FileDescriptor {
  public:
    FileDescriptor() noexcept = default;

    explicit FileDescriptor(int fd) noexcept : fd_{fd} {}

    ~FileDescriptor() { close(); }

    // Non-copyable
    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;

    // Movable
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

// Color Management
class Color {
  public:
    static constexpr std::string_view Reset = "\033[0m";

    static constexpr std::array Colors = {
        std::string_view{"\033[32m"}, // Green
        std::string_view{"\033[36m"}, // Cyan
        std::string_view{"\033[33m"}, // Yellow
        std::string_view{"\033[35m"}, // Magenta
        std::string_view{"\033[34m"}, // Blue
        std::string_view{"\033[91m"}, // Bright Red
        std::string_view{"\033[92m"}, // Bright Green
        std::string_view{"\033[93m"}, // Bright Yellow
        std::string_view{"\033[94m"}, // Bright Blue
        std::string_view{"\033[95m"}, // Bright Magenta
    };

    [[nodiscard]] static constexpr std::string_view
    get(std::size_t index) noexcept {
        return Colors[index % Colors.size()];
    }

    [[nodiscard]] static constexpr std::size_t count() noexcept {
        return Colors.size();
    }
};

// Creates a pipe and returns both ends as RAII objects
[[nodiscard]] std::expected<std::pair<FileDescriptor, FileDescriptor>,
                            CommandError>
create_pipe() noexcept {
    int fds[2];
    if (::pipe(fds) == -1) {
        return std::unexpected{CommandError::PipeCreationFailed};
    }
    return std::pair{FileDescriptor{fds[0]}, FileDescriptor{fds[1]}};
}

// Command Execution
class Command {
  public:
    Command(std::string_view cmd, std::string_view color) noexcept
        : command_{cmd}, color_{color} {}

    [[nodiscard]] std::expected<void, CommandError> execute() const {
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
            // Child process
            read_fd.close();
            execute_in_child(std::move(write_fd));
            // execute_in_child never returns
        }

        // Parent process
        write_fd.close();
        handle_output(std::move(read_fd));

        int status;
        ::waitpid(child, &status, 0);

        return {};
    }

    [[nodiscard]] std::string_view name() const noexcept { return command_; }
    [[nodiscard]] std::string_view color() const noexcept { return color_; }

  private:
    [[noreturn]] void execute_in_child(FileDescriptor write_fd) const noexcept {
        std::ignore = write_fd.duplicate_to(STDOUT_FILENO);
        std::ignore = write_fd.duplicate_to(STDERR_FILENO);
        write_fd.close();

        ::execl("/bin/sh", "sh", "-c", command_.data(),
                static_cast<char *>(nullptr));
        ::perror("exec failed");
        ::_exit(1);
    }

    void handle_output(FileDescriptor read_fd) const {
        FILE *stream = ::fdopen(read_fd.get(), "r");
        if (!stream) {
            return;
        }

        // Release ownership since fdopen takes over
        read_fd.release();

        std::array<char, 4096> buffer{};
        while (std::fgets(buffer.data(), buffer.size(), stream)) {
            std::print("{}[{}]{} {}", color_, command_, Color::Reset,
                       buffer.data());
        }

        std::fclose(stream);
    }

    std::string command_;
    std::string_view color_;
};

// Command Runner - Manages Parallel Execution
class CommandRunner {
  public:
    void add(std::string_view cmd) {
        auto color = Color::get(commands_.size());
        commands_.emplace_back(cmd, color);
    }

    void run_all() {
        std::vector<std::jthread> threads;
        threads.reserve(commands_.size());

        for (const auto &cmd : commands_) {
            threads.emplace_back([&cmd] {
                auto result = cmd.execute();
                if (!result) {
                    std::println(stderr, "{}[{}]{} Error: {}", cmd.color(),
                                 cmd.name(), Color::Reset,
                                 to_string(result.error()));
                }
            });
        }

        // jthreads auto-join on destruction
    }

    [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return commands_.size(); }

  private:
    std::vector<Command> commands_;
};

int main(int argc, char **argv) {
    std::span args{argv, static_cast<std::size_t>(argc)};

    if (args.size() < 2) {
        std::println(stderr, "Usage: {} <command1> <command2> ...", args[0]);
        return 1;
    }

    CommandRunner runner;

    for (const auto &arg : args.subspan(1)) {
        runner.add(arg);
    }

    runner.run_all();

    return 0;
}
