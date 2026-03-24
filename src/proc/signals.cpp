#include "ronq/proc/signals.hpp"

#include <cerrno>
#include <sys/wait.h>
#include <thread>

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
    pid_t pid, pid_t pgid, std::chrono::milliseconds term_grace,
    std::chrono::milliseconds kill_grace, bool force_kill) {
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
                                        bool force_kill) {
    if (!bg.has_value()) {
        return true;
    }

    return terminate_group_and_reap(
        bg->pid, bg->pgid, std::chrono::milliseconds(1000),
        std::chrono::milliseconds(1000), force_kill);
}
