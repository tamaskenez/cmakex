#include "nosx/check.h"
#include "nosx/stringf.h"

#include <cmath>

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

std::string format_duration(double seconds)
{
    const char* sign = seconds < 0 ? "-" : "";
    unsigned seconds_int = (unsigned)round(fabs(seconds));
    unsigned h = seconds_int / 3600;
    seconds_int -= h * 3600;
    unsigned m = seconds_int / 60;
    seconds_int -= m * 60;
    unsigned s = seconds_int;
    CHECK(0 <= s && s < 60);
    char buf[256];
    if (h == 0 && m == 0)
        sprintf(buf, "%fs", seconds);
    else if (h == 0)
        sprintf(buf, "%s%u'%02u\"", sign, m, s);
    else
        sprintf(buf, "%s%u:%02u:%02u", sign, h, m, s);
    return buf;
}

}  // namespace nosx
