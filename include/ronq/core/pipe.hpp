#pragma once

#include "ronq/core/errors.hpp"
#include "ronq/core/fd.hpp"

#include <expected>
#include <utility>

[[nodiscard]] std::expected<std::pair<FileDescriptor, FileDescriptor>,
                            CommandError>
create_pipe() noexcept;
