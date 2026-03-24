#include "ronq/core/errors.hpp"

#include <utility>

[[nodiscard]] std::string_view to_string(CommandError error) noexcept {
    switch (error) {
    case CommandError::PipeCreationFailed:
        return "Failed to create pipe";
    case CommandError::ForkFailed:
        return "Failed to fork process";
    case CommandError::ExecFailed:
        return "Failed to execute command";
    case CommandError::OutputReadFailed:
        return "Failed to read command output";
    case CommandError::OpenDevNullFailed:
        return "Failed to open /dev/null";
    }
    std::unreachable();
}
