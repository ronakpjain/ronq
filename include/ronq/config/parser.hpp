#pragma once

#include "ronq/config/types.hpp"

#include <expected>
#include <filesystem>
#include <string>

[[nodiscard]] std::expected<ConfigMap, std::string>
load_configs_from_file(const std::filesystem::path &path);
