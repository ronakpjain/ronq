#include "ronq/config/cli.hpp"

#include "ronq/config/parser.hpp"

#include <filesystem>

[[nodiscard]] CliResolution resolve_cli(std::span<char *> args) {
    CliResolution out;
    if (args.size() < 2) {
        out.mode = RunMode::Error;
        out.error = "missing arguments";
        return out;
    }

    out.raw_commands.reserve(args.size() - 1);
    for (const auto &arg : args.subspan(1)) {
        out.raw_commands.emplace_back(arg);
    }

    if (out.raw_commands.size() != 1) {
        out.mode = RunMode::Raw;
        return out;
    }

    const std::filesystem::path config_path =
        std::filesystem::current_path() / "ronq.toml";

    if (!std::filesystem::exists(config_path)) {
        out.mode = RunMode::Raw;
        return out;
    }

    auto configs_result = load_configs_from_file(config_path);
    if (!configs_result) {
        out.mode = RunMode::Error;
        out.error = configs_result.error();
        return out;
    }

    auto &configs = *configs_result;
    const auto key = out.raw_commands.front();
    const auto it = configs.find(key);
    if (it == configs.end()) {
        out.mode = RunMode::Raw;
        return out;
    }

    out.mode = RunMode::Config;
    out.config_name = key;
    out.config = it->second;
    return out;
}
