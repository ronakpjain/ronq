#pragma once

#include "ronq/core/errors.hpp"
#include "ronq/core/fd.hpp"

#include <expected>
#include <string>
#include <string_view>

class Command {
  public:
    Command(std::string_view cmd, std::string_view color) noexcept;

    [[nodiscard]] std::expected<void, CommandError> execute() const;

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] std::string_view color() const noexcept;

  private:
    [[noreturn]] void execute_in_child(FileDescriptor write_fd) const noexcept;
    void handle_output(FileDescriptor read_fd) const;

    std::string command_;
    std::string_view color_;
};
