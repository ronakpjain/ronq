#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ronq/config/cli.hpp"
#include "ronq/core/color.hpp"
#include "ronq/core/errors.hpp"
#include "ronq/core/fd.hpp"
#include "ronq/core/pipe.hpp"
#include "ronq/raw/runner.hpp"

struct BgProcess {
    pid_t pid = -1;
    pid_t pgid = -1;
    FileDescriptor read_fd;
};

struct SpawnedProcess {
    pid_t pid = -1;
    pid_t pgid = -1;
};

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

volatile std::sig_atomic_t g_received_signal = 0;
volatile std::sig_atomic_t g_signal_count = 0;

extern "C" void signal_handler(int signo) {
    g_received_signal = signo;
    g_signal_count = g_signal_count + 1;
}

void install_signal_handlers() {
    struct sigaction action{};
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    (void)::sigaction(SIGINT, &action, nullptr);
    (void)::sigaction(SIGTERM, &action, nullptr);
}

[[nodiscard]] bool send_signal_to_group(pid_t pgid, int signo) noexcept {
    if (pgid <= 0) {
        return true;
    }

    if (::kill(-pgid, signo) == -1) {
        return errno == ESRCH;
    }
    return true;
}

[[nodiscard]] bool wait_for_exit(pid_t pid, std::chrono::milliseconds timeout,
                                 int &status_out) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        const pid_t waited = ::waitpid(pid, &status_out, WNOHANG);
        if (waited == pid) {
            return true;
        }
        if (waited == -1) {
            if (errno == ECHILD) {
                return true;
            }
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

[[nodiscard]] bool terminate_group_and_reap(
    pid_t pid, pid_t pgid,
    std::chrono::milliseconds term_grace = std::chrono::milliseconds(1000),
    std::chrono::milliseconds kill_grace = std::chrono::milliseconds(1000),
    bool force_kill = false) {
    if (pid <= 0 || pgid <= 0) {
        return true;
    }

    int status = 0;

    if (force_kill) {
        (void)send_signal_to_group(pgid, SIGKILL);
        return wait_for_exit(pid, kill_grace, status);
    }

    (void)send_signal_to_group(pgid, SIGTERM);
    if (wait_for_exit(pid, term_grace, status)) {
        return true;
    }

    (void)send_signal_to_group(pgid, SIGKILL);
    return wait_for_exit(pid, kill_grace, status);
}

[[nodiscard]] bool terminate_background(const std::optional<BgProcess> &bg,
                                        bool force_kill = false) {
    if (!bg.has_value()) {
        return true;
    }

    return terminate_group_and_reap(
        bg->pid, bg->pgid, std::chrono::milliseconds(1000),
        std::chrono::milliseconds(1000), force_kill);
}

[[nodiscard]] std::expected<int, std::string>
run_config(const std::string &config_name, const NamedConfig &cfg) {
    install_signal_handlers();
    g_received_signal = 0;
    g_signal_count = 0;

    std::optional<BgProcess> bg_process;
    std::jthread bg_output_thread;

    if (cfg.bg.has_value()) {
        auto bg_result = spawn_background(*cfg.bg);
        if (!bg_result) {
            return std::unexpected{std::format("Failed to start bg command: {}",
                                               bg_result.error())};
        }
        bg_process = std::move(*bg_result);
        bg_output_thread =
            std::jthread([name = config_name,
                          fd = std::move(bg_process->read_fd)]() mutable {
                stream_background_output(std::move(fd), name);
            });
    }

    auto fg_result = spawn_foreground(cfg.fg);
    if (!fg_result) {
        (void)terminate_background(bg_process);
        return std::unexpected{
            std::format("Failed to start fg command: {}", fg_result.error())};
    }

    const SpawnedProcess fg = *fg_result;
    int fg_status = 0;
    bool shutdown_started = false;
    bool escalated_to_term = false;
    bool escalated_to_kill = false;
    std::sig_atomic_t handled_signal_count = 0;
    auto shutdown_start = std::chrono::steady_clock::time_point{};

    while (true) {
        const pid_t waited = ::waitpid(fg.pid, &fg_status, WNOHANG);
        if (waited == fg.pid) {
            break;
        }

        if (waited == -1) {
            if (errno == EINTR) {
                continue;
            }

            (void)terminate_background(bg_process, true);
            return std::unexpected{"waitpid failed for foreground process"};
        }

        const auto signal_count = g_signal_count;
        if (signal_count > handled_signal_count) {
            handled_signal_count = signal_count;
            const int received_signal =
                (g_received_signal == SIGTERM) ? SIGTERM : SIGINT;

            if (!shutdown_started) {
                shutdown_started = true;
                shutdown_start = std::chrono::steady_clock::now();
                (void)send_signal_to_group(fg.pgid, received_signal);
                (void)terminate_background(bg_process);
            } else if (!escalated_to_term && handled_signal_count >= 2) {
                escalated_to_term = true;
                (void)send_signal_to_group(fg.pgid, SIGTERM);
                (void)terminate_background(bg_process, true);
            } else if (!escalated_to_kill && handled_signal_count >= 3) {
                escalated_to_kill = true;
                (void)send_signal_to_group(fg.pgid, SIGKILL);
                (void)terminate_background(bg_process, true);
            }
        }

        if (shutdown_started) {
            const auto elapsed =
                std::chrono::steady_clock::now() - shutdown_start;
            if (!escalated_to_term && elapsed >= std::chrono::seconds(2)) {
                escalated_to_term = true;
                (void)send_signal_to_group(fg.pgid, SIGTERM);
                (void)terminate_background(bg_process, true);
            }
            if (!escalated_to_kill && elapsed >= std::chrono::seconds(4)) {
                escalated_to_kill = true;
                (void)send_signal_to_group(fg.pgid, SIGKILL);
                (void)terminate_background(bg_process, true);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    (void)terminate_background(bg_process);

    if (WIFEXITED(fg_status)) {
        return WEXITSTATUS(fg_status);
    }
    if (WIFSIGNALED(fg_status)) {
        return 128 + WTERMSIG(fg_status);
    }
    return std::unexpected{"foreground process ended unexpectedly"};
}

void print_usage(std::string_view argv0) {
    std::println(stderr, "Usage:");
    std::println(stderr, "  {} <command1> [command2 ...]", argv0);
    std::println(stderr, "  {} <config-name>", argv0);
    std::println(stderr, "");
    std::println(stderr, "Config file (optional): ./ronq.toml");
    std::println(stderr, "  [configs.<name>]");
    std::println(stderr, "  bg = \"<optional command>\"");
    std::println(stderr, "  fg = \"<required command>\"");
}

int main(int argc, char **argv) {
    std::span args{argv, static_cast<std::size_t>(argc)};

    if (args.size() < 2) {
        print_usage(args[0]);
        return 1;
    }

    const CliResolution resolution = resolve_cli(args);
    if (resolution.mode == RunMode::Error) {
        std::println(stderr, "Error: {}", resolution.error);
        return 1;
    }

    if (resolution.mode == RunMode::Config) {
        auto config_result =
            run_config(resolution.config_name, resolution.config);
        if (!config_result) {
            std::println(stderr, "Error: {}", config_result.error());
            return 1;
        }
        return *config_result;
    }

    CommandRunner runner;

    for (const auto &arg : resolution.raw_commands) {
        runner.add(arg);
    }

    runner.run_all();

    return 0;
}
