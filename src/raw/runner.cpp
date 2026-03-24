#include "ronq/raw/runner.hpp"

#include "ronq/core/color.hpp"

#include <print>
#include <thread>

void CommandRunner::add(std::string_view cmd) {
    auto color = Color::get(commands_.size());
    commands_.emplace_back(cmd, color);
}

void CommandRunner::run_all() {
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
}

[[nodiscard]] bool CommandRunner::empty() const noexcept {
    return commands_.empty();
}

[[nodiscard]] std::size_t CommandRunner::size() const noexcept {
    return commands_.size();
}
