#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct NamedConfig {
    std::optional<std::string> bg;
    std::string fg;
};

using ConfigMap = std::unordered_map<std::string, NamedConfig>;

enum class RunMode { Raw, Config, Error };

struct CliResolution {
    RunMode mode = RunMode::Error;
    std::vector<std::string> raw_commands;
    std::string config_name;
    NamedConfig config;
    std::string error;
};
