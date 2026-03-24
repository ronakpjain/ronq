#pragma once

#include "ronq/config/types.hpp"

#include <span>

[[nodiscard]] CliResolution resolve_cli(std::span<char *> args);
