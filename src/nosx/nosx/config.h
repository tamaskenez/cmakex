#pragma once

#if defined __printflike
#define NOSX_PRINTFLIKE(M, N) __printflike(M, N)
#elif defined __GNUC__
#define NOSX_PRINTFLIKE(M, N) __attribute__((format(printf, M, N)))
#else
#define NOSX_PRINTFLIKE(M, N)
#endif

#ifdef _MSC_VER
#define NOSX_NORETURN __declspec(noreturn)
#elif __cplusplus > 201100
#define NOSX_NORETURN [[noreturn]]
#else
#define NOSX_NORETURN
#endif
