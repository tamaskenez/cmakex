#include "exec_process.h"

#include <thread>

#include <Poco/Pipe.h>
#include <Poco/Process.h>

#include <nosx/log.h>

namespace cmakex {
using Poco::Pipe;
using Poco::Process;

void pipereader(Pipe* pipe, exec_process_output_callback_t* callback)
{
    const int c_bufsize = 4096;
    char buf[c_bufsize];
    for (;;) {
        int r = pipe->readBytes(buf, c_bufsize);
        if (r == 0)
            return;
        (*callback)(array_view<const char>(&buf[0], r));
    }
}

int exec_process(string_par path,
                 const vector<string>& args,
                 string_par working_directory,
                 exec_process_output_callback_t stdout_callback,
                 exec_process_output_callback_t stderr_callback)
{
    Pipe outpipe, errpipe;
    std::thread outpipe_thread, errpipe_thread;
    if (stdout_callback)
        outpipe_thread = std::thread(&pipereader, &outpipe, &stdout_callback);
    if (stderr_callback)
        errpipe_thread = std::thread(&pipereader, &errpipe, &stderr_callback);
    int exit_code = EXIT_FAILURE;
    try {
        auto handle =
            working_directory.empty()
                ? Process::launch(path.str(), args, nullptr, stdout_callback ? &outpipe : nullptr,
                                  stderr_callback ? &errpipe : nullptr)
                : Process::launch(path.str(), args, working_directory.str(), nullptr,
                                  stdout_callback ? &outpipe : nullptr,
                                  stderr_callback ? &errpipe : nullptr);
        exit_code = handle.wait();
    } catch (...) {
        if (outpipe_thread.joinable())
            outpipe_thread.join();
        if (errpipe_thread.joinable())
            errpipe_thread.join();
        throw;
    }

    if (outpipe_thread.joinable())
        outpipe_thread.join();
    if (errpipe_thread.joinable())
        errpipe_thread.join();

    return exit_code;
}
}  // namespace cmakex
