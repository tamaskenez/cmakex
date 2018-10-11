#include "nosx/stringf.h"

#ifndef _MSC_VER
#define NOSX_VSNPRINTF vsnprintf
#else
#define NOSX_VSNPRINTF _vsnprintf
#endif

namespace nosx {

const int c_bufsize = 65536;

std::string stringf(const char* fmt, ...)
{
    char buf[c_bufsize];
    va_list ap;
    va_start(ap, fmt);
    int r = NOSX_VSNPRINTF(buf, c_bufsize, fmt, ap);
    va_end(ap);

    if (0 <= r && static_cast<size_t>(r) < c_bufsize)
        return buf;
    else
        return "<ERROR calling vsnprintf>";
}

std::string vstringf(const char* fmt, va_list args)
{
    char buf[c_bufsize];
    va_list args_copy;
    va_copy(args_copy, args);
    int r = NOSX_VSNPRINTF(buf, c_bufsize, fmt, args_copy);
    va_end(args_copy);

    if (0 <= r && static_cast<size_t>(r) < c_bufsize)
        return buf;
    else
        return "<ERROR calling vsnprintf>";
}

}  // namespace nosx
