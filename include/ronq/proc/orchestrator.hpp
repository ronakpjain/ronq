#pragma once

#include "ronq/config/types.hpp"

#include <expected>
#include <string>

[[nodiscard]] std::expected<int, std::string>
run_config(const std::string &config_name, const NamedConfig &cfg);
