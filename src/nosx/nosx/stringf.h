#pragma once

#include <string>

#include "nosx/config.h"

namespace nosx {
std::string stringf(const char* fmt, ...) NOSX_PRINTFLIKE(1, 2);
std::string vstringf(const char* fmt, va_list arg) NOSX_PRINTFLIKE(1, 0);
}  // namespace nosx
