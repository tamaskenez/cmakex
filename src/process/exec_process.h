#ifndef EXEC_PROCESS_0394723
#define EXEC_PROCESS_0394723

#include <functional>
#include <string_view>
#include <vector>

#include <nosx/mutex.h>
#include <nosx/string_par.h>

namespace cmakex {

using nosx::atomic_flag_mutex;
using nosx::string_par;
using std::string;
using std::vector;
template <class T>
using array_view = std::basic_string_view<T>;

using exec_process_output_callback_t = std::function<void(array_view<const char>)>;

namespace exec_process_callbacks {

inline void print_to_stdout(array_view<const char> x)
{
    printf("%.*s", (int)x.size(), x.data());
}
inline void print_to_stderr(array_view<const char> x)
{
    fprintf(stderr, "%.*s", (int)x.size(), x.data());
}

}  // namespace exec_process_callbacks

// launch new process with args (synchronous)
int exec_process(string_par path,
                 const vector<string>& args,
                 string_par working_directory,
                 exec_process_output_callback_t stdout_callback = nullptr,
                 exec_process_output_callback_t stderr_callback = nullptr);

inline int exec_process(string_par path,
                        const vector<string>& args,
                        exec_process_output_callback_t stdout_callback = nullptr,
                        exec_process_output_callback_t stderr_callback = nullptr)
{
    return exec_process(path, args, "", stdout_callback, stderr_callback);
}

inline int exec_process(string_par path,
                        exec_process_output_callback_t stdout_callback = nullptr,
                        exec_process_output_callback_t stderr_callback = nullptr)
{
    return exec_process(path, vector<string>{}, "", stdout_callback, stderr_callback);
}
}  // namespace cmakex

#endif
