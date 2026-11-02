#pragma once

#include <string_view>

namespace md {

// Thin logging facade over spdlog. Keeping spdlog/{fmt} out of the big
// pipeline translation unit avoids GCC 13 false-positive -Warray-bounds /
// -Wstringop-overflow reports from {fmt} v9 internals (see LESSONS.md), and
// keeps compile times down. Callers format with std::snprintf / std::string.
void log_info(std::string_view msg);
void log_warn(std::string_view msg);
void log_error(std::string_view msg);

}  // namespace md
