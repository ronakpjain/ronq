#pragma once

#include <array>
#include <string_view>

class Color {
  public:
    static constexpr std::string_view Reset = "\033[0m";

    static constexpr std::array Colors = {
        std::string_view{"\033[32m"}, // Green
        std::string_view{"\033[36m"}, // Cyan
        std::string_view{"\033[33m"}, // Yellow
        std::string_view{"\033[35m"}, // Magenta
        std::string_view{"\033[34m"}, // Blue
        std::string_view{"\033[91m"}, // Bright Red
        std::string_view{"\033[92m"}, // Bright Green
        std::string_view{"\033[93m"}, // Bright Yellow
        std::string_view{"\033[94m"}, // Bright Blue
        std::string_view{"\033[95m"}, // Bright Magenta
    };

    [[nodiscard]] static constexpr std::string_view
    get(std::size_t index) noexcept {
        return Colors[index % Colors.size()];
    }

    [[nodiscard]] static constexpr std::size_t count() noexcept {
        return Colors.size();
    }
};
