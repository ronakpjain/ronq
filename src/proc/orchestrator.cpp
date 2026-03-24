#include "ronq/proc/orchestrator.hpp"

#include "ronq/proc/signals.hpp"
#include "ronq/proc/spawn.hpp"

#include <cerrno>
#include <chrono>
#include <format>
#include <optional>
#include <sys/wait.h>
#include <thread>
#include <utility>

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
