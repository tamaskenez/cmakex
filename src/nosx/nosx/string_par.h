#pragma once

#include <cstring>
#include <string>

namespace nosx {

class string_par
{
public:
    using iterator = const char*;
    using const_iterator = iterator;
    using string = std::string;
    using size_t = std::size_t;

    string_par() = delete;
    string_par(const string_par&) = default;
    string_par(const char* s) : s(s), size_(string::npos) {}
    string_par(const string& s) : s(s.c_str()), size_(s.size()) {}

    bool empty() const { return !s || *s == 0; }
    size_t size() const { return size_ == string::npos ? strlen(s) : size_; }

    iterator begin() const { return s; }
    iterator end() const { return s + size(); }

    const char* c_str() const { return s; }
    string str() const { return s; }
    operator const char*() const { return s; }

private:
    const char* s;
    size_t size_;
};

inline bool operator==(string_par x, const std::string& y)
{
    return y == x.c_str();
}
inline bool operator==(const std::string& y, string_par x)
{
    return y == x.c_str();
}
inline bool operator==(string_par x, const char* y)
{
    return strcmp(y, x.c_str()) == 0;
}
inline bool operator==(const char* y, string_par x)
{
    return strcmp(y, x.c_str()) == 0;
}
inline bool operator!=(string_par x, const std::string& y)
{
    return y != x.c_str();
}
inline bool operator!=(const std::string& y, string_par x)
{
    return y != x.c_str();
}
inline bool operator!=(string_par x, const char* y)
{
    return strcmp(y, x.c_str()) != 0;
}
inline bool operator!=(const char* y, string_par x)
{
    return strcmp(y, x.c_str()) != 0;
}
}  // namespace nosx
