#include "build.h"

#include <adasworks/sx/check.h>

#include "cmakex_utils.h"
#include "installdb.h"
#include "misc_utils.h"
#include "out_err_messages.h"
#include "print.h"

namespace cmakex {

void build(string_par binary_dir,
           const pkg_desc_t& request,
           string_par config,
           bool first_config,
           bool force_config_step)
{
    log_info("Configuration: %s", config.c_str());

    // create binary dir
    cmakex_config_t cfg(binary_dir);

    auto pkg_clone_dir = cfg.pkg_clone_dir(request.name);
    string source_dir = pkg_clone_dir;

    if (!request.b.source_dir.empty())
        source_dir += "/" + request.b.source_dir;

    vector<string> args;
    args.emplace_back(string("-H") + source_dir);
    CHECK(cfg.cmakex_cache_loaded());
    args.emplace_back(
        string("-B") +
        cfg.pkg_binary_dir_of_config(request.name, config, cfg.cmakex_cache().per_config_bin_dirs));

    vector<string> cmake_args_to_apply = request.b.cmake_args;

    // check if there's no install_prefix
    for (auto& c : cmake_args_to_apply) {
        if (!starts_with(c, "-DCMAKE_INSTALL_PREFIX"))
            continue;
        auto pca = parse_cmake_arg(c);
        if (pca.name == "CMAKE_INSTALL_PREFIX") {
            throwf(
                "Internal error: global and package cmake_args should not change "
                "CMAKE_PREFIX_PATH: '%s'",
                c.c_str());
        }
    }

    cmake_args_to_apply.emplace_back(
        stringf("-DCMAKE_INSTALL_PREFIX=%s", cfg.deps_install_dir().c_str()));

    "what to do with cmake build type";

    if (!cfg.cmakex_cache().multiconfig_generator)
        args.emplace_back(stringf("-DCMAKE_BUILD_TYPE=%s", config.c_str()));

    append(args, cmake_args_to_apply);

    cmakex_cache_save(cfgh.pkg_bin_dir, request.name, request.b.cmake_args);

    log_exec("cmake", args);
    {
        OutErrMessagesBuilder oeb(pipe_capture, pipe_capture);
        int r = exec_process("cmake", args, oeb.stdout_callback(), oeb.stderr_callback());
        auto oem = oeb.move_result();

        save_log_from_oem(
            "CMake-configure", r, oem, cfg.cmakex_log_dir(),
            stringf("%s-%s-configure%s", request.name.c_str(), config.c_str(), k_log_extension));

        if (r != EXIT_SUCCESS)
            throwf("CMake configure step failed, result: %d.", r);
    }

    args.assign({"--build", cfgh.pkg_bin_dir.c_str()});
    if (cfgh.multiconfig_generator)
        args.insert(args.end(), {"--config", config.c_str()});

    // todo add build_args
    // todo add --clean-first if it's needed but not yet added
    // todo add native tool args
    // todo clear install dir if --clean-first

    log_exec("cmake", args);
    {
        OutErrMessagesBuilder oeb(pipe_capture, pipe_capture);
        int r = exec_process("cmake", args, oeb.stdout_callback(), oeb.stderr_callback());
        auto oem = oeb.move_result();

        save_log_from_oem(
            "Build", r, oem, cfg.cmakex_log_dir(),
            stringf("%s-%s-build%s", request.name.c_str(), config.c_str(), k_log_extension));

        if (r != EXIT_SUCCESS)
            throwf("CMake build step failed, result: %d.", r);
    }

    args.assign({"--build", cfgh.pkg_bin_dir.c_str(), "--target", "install"});
    if (cfgh.multiconfig_generator)
        args.insert(args.end(), {"--config", config.c_str()});

    // todo add build_args
    // todo add --clean-first if it's needed but not yet added
    // todo add native tool args

    log_exec("cmake", args);
    {
        OutErrMessagesBuilder oeb(pipe_capture, pipe_capture);
        int r = exec_process("cmake", args, oeb.stdout_callback(), oeb.stderr_callback());
        auto oem = oeb.move_result();

        save_log_from_oem(
            "Install", r, oem, cfg.cmakex_log_dir(),
            stringf("%s-%s-install%s", request.name.c_str(), config.c_str(), k_log_extension));

        if (r != EXIT_SUCCESS)
            throwf("CMake install step failed, result: %d.", r);
    }
}
}
