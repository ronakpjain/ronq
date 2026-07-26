#include "ronq/proc/orchestrator.hpp"

#include "ronq/proc/signals.hpp"
#include "ronq/proc/spawn.hpp"

#include <cerrno>
#include <chrono>
#include <format>
#include <sys/wait.h>
#include <thread>
#include <utility>
#include <vector>

[[nodiscard]] std::expected<int, std::string>
run_config(const std::string &config_name, const NamedConfig &cfg) {
    install_signal_handlers();
    g_received_signal = 0;
    g_signal_count = 0;

    std::vector<BgProcess> backgrounds;
    backgrounds.reserve(cfg.bg.size());

    std::vector<std::jthread> bg_output_threads;
    bg_output_threads.reserve(cfg.bg.size());

    for (std::size_t i = 0; i < cfg.bg.size(); ++i) {
        auto bg_result = spawn_background(cfg.bg[i]);
        if (!bg_result) {
            (void)terminate_backgrounds(backgrounds);
            return std::unexpected{std::format(
                "Failed to start bg command {}: {}", i + 1, bg_result.error())};
        }

        backgrounds.push_back(std::move(*bg_result));

        std::string bg_name = config_name;
        if (cfg.bg.size() > 1) {
            bg_name = std::format("{}#{}", config_name, i + 1);
        }

        bg_output_threads.emplace_back(
            [name = std::move(bg_name),
             fd = std::move(backgrounds.back().read_fd)]() mutable {
                stream_background_output(std::move(fd), name);
            });
    }

    auto fg_result = spawn_foreground(cfg.fg);
    if (!fg_result) {
        (void)terminate_backgrounds(backgrounds);
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

            (void)terminate_backgrounds(backgrounds, true);
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
                (void)terminate_backgrounds(backgrounds);
            } else if (!escalated_to_term && handled_signal_count >= 2) {
                escalated_to_term = true;
                (void)send_signal_to_group(fg.pgid, SIGTERM);
                (void)terminate_backgrounds(backgrounds, true);
            } else if (!escalated_to_kill && handled_signal_count >= 3) {
                escalated_to_kill = true;
                (void)send_signal_to_group(fg.pgid, SIGKILL);
                (void)terminate_backgrounds(backgrounds, true);
            }
        }

        if (shutdown_started) {
            const auto elapsed =
                std::chrono::steady_clock::now() - shutdown_start;
            if (!escalated_to_term && elapsed >= std::chrono::seconds(2)) {
                escalated_to_term = true;
                (void)send_signal_to_group(fg.pgid, SIGTERM);
                (void)terminate_backgrounds(backgrounds, true);
            }
            if (!escalated_to_kill && elapsed >= std::chrono::seconds(4)) {
                escalated_to_kill = true;
                (void)send_signal_to_group(fg.pgid, SIGKILL);
                (void)terminate_backgrounds(backgrounds, true);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    (void)terminate_backgrounds(backgrounds);

    if (WIFEXITED(fg_status)) {
        return WEXITSTATUS(fg_status);
    }
    if (WIFSIGNALED(fg_status)) {
        return 128 + WTERMSIG(fg_status);
    }
    return std::unexpected{"foreground process ended unexpectedly"};
}
