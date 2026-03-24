#include "ronq/config/parser.hpp"

#include <format>
#include <fstream>
#include <string_view>

namespace {

[[nodiscard]] std::string trim(std::string_view input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = input.find_last_not_of(" \t\r\n");
    return std::string{input.substr(first, last - first + 1)};
}

[[nodiscard]] std::string strip_comments(std::string_view line) {
    bool in_quotes = false;
    bool escaped = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        if (c == '#' && !in_quotes) {
            return std::string{line.substr(0, i)};
        }
    }

    return std::string{line};
}

[[nodiscard]] std::expected<std::string, std::string>
parse_toml_string(std::string_view value, std::size_t line_number) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return std::unexpected{std::format(
            "ronq.toml:{}: expected quoted string value", line_number)};
    }

    std::string out;
    out.reserve(value.size() - 2);

    for (std::size_t i = 1; i + 1 < value.size(); ++i) {
        char c = value[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }

        if (i + 1 >= value.size() - 1) {
            return std::unexpected{std::format(
                "ronq.toml:{}: dangling escape in string", line_number)};
        }

        const char next = value[++i];
        switch (next) {
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        case '\\':
            out.push_back('\\');
            break;
        case '"':
            out.push_back('"');
            break;
        default:
            return std::unexpected{
                std::format("ronq.toml:{}: unsupported escape sequence \\{}",
                            line_number, next)};
        }
    }

    return out;
}

} // namespace

[[nodiscard]] std::expected<ConfigMap, std::string>
load_configs_from_file(const std::filesystem::path &path) {
    std::ifstream in{path};
    if (!in) {
        return std::unexpected{std::format("Failed to read {}", path.string())};
    }

    struct PartialConfig {
        std::optional<std::string> bg;
        std::optional<std::string> fg;
    };

    std::unordered_map<std::string, PartialConfig> partials;
    std::optional<std::string> current_config;

    std::string raw_line;
    std::size_t line_number = 0;
    while (std::getline(in, raw_line)) {
        ++line_number;
        const std::string line = trim(strip_comments(raw_line));
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[') {
            if (line.back() != ']') {
                return std::unexpected{std::format(
                    "ronq.toml:{}: malformed section header", line_number)};
            }

            const std::string section_name =
                trim(std::string_view{line}.substr(1, line.size() - 2));
            constexpr std::string_view prefix = "configs.";
            if (!section_name.starts_with(prefix)) {
                return std::unexpected{std::format(
                    "ronq.toml:{}: unsupported section [{}] (expected "
                    "[configs.<name>])",
                    line_number, section_name)};
            }

            std::string name =
                trim(std::string_view{section_name}.substr(prefix.size()));
            if (name.empty()) {
                return std::unexpected{std::format(
                    "ronq.toml:{}: empty config name in section", line_number)};
            }

            current_config = std::move(name);
            partials[*current_config];
            continue;
        }

        if (!current_config) {
            return std::unexpected{std::format(
                "ronq.toml:{}: key/value outside [configs.<name>] section",
                line_number)};
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            return std::unexpected{
                std::format("ronq.toml:{}: expected key = value", line_number)};
        }

        const std::string key = trim(std::string_view{line}.substr(0, eq));
        const std::string value_text =
            trim(std::string_view{line}.substr(eq + 1));

        auto value = parse_toml_string(value_text, line_number);
        if (!value) {
            return std::unexpected{value.error()};
        }

        auto &cfg = partials[*current_config];
        if (key == "bg") {
            cfg.bg = *value;
        } else if (key == "fg") {
            cfg.fg = *value;
        } else {
            return std::unexpected{std::format(
                "ronq.toml:{}: unsupported key '{}' (allowed: bg, fg)",
                line_number, key)};
        }
    }

    ConfigMap configs;
    for (const auto &[name, partial] : partials) {
        if (!partial.fg.has_value() || trim(*partial.fg).empty()) {
            return std::unexpected{std::format(
                "ronq.toml: config '{}' is missing required non-empty 'fg'",
                name)};
        }

        if (partial.bg.has_value() && trim(*partial.bg).empty()) {
            return std::unexpected{
                std::format("ronq.toml: config '{}' has empty 'bg'", name)};
        }

        configs.emplace(name, NamedConfig{.bg = partial.bg, .fg = *partial.fg});
    }

    return configs;
}
