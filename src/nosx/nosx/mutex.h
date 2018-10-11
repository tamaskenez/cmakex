#pragma once

#include <atomic>

namespace nosx {

class atomic_flag_mutex
{
public:
#if defined _MSC_VER && _MSC_VER < 1900
    atomic_flag_mutex() { af.clear(); }
#endif
    using native_handle_type = std::atomic_flag*;
    void lock()
    {
        while (af.test_and_set())
            ;
    }
    bool try_lock() { return !af.test_and_set(); }
    void unlock() { af.clear(); }
    native_handle_type native_handle() { return &af; }

private:
#if defined _MSC_VER && _MSC_VER < 1900
    std::atomic_flag af = ATOMIC_FLAG_INIT;
#else
    std::atomic_flag af;
#endif
};
}  // namespace nosx
