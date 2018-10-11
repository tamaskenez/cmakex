#pragma once

#include <cstdio>
#include <exception>

#define LOG_TRACE(...)

#ifdef NDEBUG
#define LOG_DEBUG(...)
#else
#define LOG_DEBUG(...) fprintf(stderr, __VA_ARGS__)
#endif

#define LOG_INFO(...) fprintf(stderr, __VA_ARGS__)

#define LOG_FATAL(...) (fprintf(stderr, __VA_ARGS__), std::terminate())
