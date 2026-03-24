#pragma once

#include "ronq/raw/command.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

class CommandRunner {
  public:
    void add(std::string_view cmd);
    void run_all();

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    std::vector<Command> commands_;
};
