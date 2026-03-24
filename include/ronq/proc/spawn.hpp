#pragma once

#include "ronq/proc/types.hpp"

#include <expected>
#include <string>
#include <string_view>

[[nodiscard]] std::expected<SpawnedProcess, std::string>
spawn_foreground(const std::string &cmd);

[[nodiscard]] std::expected<BgProcess, std::string>
spawn_background(const std::string &cmd);

void stream_background_output(FileDescriptor read_fd,
                              std::string_view config_name);
