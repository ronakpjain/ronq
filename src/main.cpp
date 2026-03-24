#include "ronq/config/cli.hpp"
#include "ronq/proc/orchestrator.hpp"
#include "ronq/raw/runner.hpp"

#include <print>
#include <span>
#include <string_view>

namespace {

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

} // namespace

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
