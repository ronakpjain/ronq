#pragma once

#include <string_view>

enum class CommandError {
    PipeCreationFailed,
    ForkFailed,
    ExecFailed,
    OutputReadFailed,
    OpenDevNullFailed
};

[[nodiscard]] std::string_view to_string(CommandError error) noexcept;
