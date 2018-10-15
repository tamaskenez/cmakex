#pragma once

#include <cstdio>
#include <exception>

#define LOG_TRACE(...) ((void)0)

#ifdef NDEBUG
#define LOG_DEBUG(...) ((void)0)
#else
#define LOG_DEBUG(...) (fprintf(stderr, __VA_ARGS__))
#endif

#define LOG_WARN(...) ((void)0)

#define LOG_INFO(...) (fprintf(stderr, __VA_ARGS__))

#define LOG_FATAL(...) (fprintf(stderr, __VA_ARGS__), std::terminate())
