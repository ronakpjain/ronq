#pragma once

#include "ronq/proc/types.hpp"

#include <chrono>
#include <csignal>
#include <optional>

extern volatile std::sig_atomic_t g_received_signal;
extern volatile std::sig_atomic_t g_signal_count;

void install_signal_handlers();

[[nodiscard]] bool send_signal_to_group(pid_t pgid, int signo) noexcept;

[[nodiscard]] bool wait_for_exit(pid_t pid, std::chrono::milliseconds timeout,
                                 int &status_out);

[[nodiscard]] bool terminate_group_and_reap(
    pid_t pid, pid_t pgid,
    std::chrono::milliseconds term_grace = std::chrono::milliseconds(1000),
    std::chrono::milliseconds kill_grace = std::chrono::milliseconds(1000),
    bool force_kill = false);

[[nodiscard]] bool terminate_background(const std::optional<BgProcess> &bg,
                                        bool force_kill = false);
