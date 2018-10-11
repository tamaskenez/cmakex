#pragma once

// stringize: from boost 1.59
#ifdef _MSC_VER
#define NOSX_STRINGIZE(text) NOSX_STRINGIZE_A((text))
#define NOSX_STRINGIZE_A(arg) NOSX_STRINGIZE_I arg
#else
#define NOSX_STRINGIZE(text) NOSX_STRINGIZE_I(text)
#endif

#define NOSX_STRINGIZE_I(text) #text
