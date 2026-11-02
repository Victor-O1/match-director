#include "md/core/log.hpp"

#include <spdlog/spdlog.h>

namespace md {

void log_info(std::string_view msg) { spdlog::info("{}", msg); }
void log_warn(std::string_view msg) { spdlog::warn("{}", msg); }
void log_error(std::string_view msg) { spdlog::error("{}", msg); }

}  // namespace md
