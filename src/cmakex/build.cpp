#include "build.h"

#include <adasworks/sx/check.h>

#include "cmakex_utils.h"
#include "filesystem.h"
#include "installdb.h"
#include "misc_utils.h"
#include "out_err_messages.h"
#include "print.h"

// todo for a zlibpng project do an initial build then interrupt zlib and rebuild
/*Error, bad parameters: The source dir specified "." is different from the one stored in the
cmakex_cache.json: "".
Program ended with exit code: 1
*/
namespace cmakex {

namespace fs = filesystem;

void build(string_par binary_dir,
           string_par pkg_name,
           string_par pkg_source_dir,
           const vector<string>& cmake_args_in,
           config_name_t config,
           const vector<string>& build_targets,
           bool force_config_step,
           const cmakex_cache_t& cmakex_cache,
           vector<string> build_args,
           const vector<string>& native_tool_args)
{
    cmakex_config_t cfg(binary_dir);
    CHECK(cmakex_cache.valid);

    // cmake-configure step: Need to do it if
    //
    // force_config_step || new_binary_dir || cmakex_cache_tracker indicates changed settings

    string source_dir;
    string pkg_bin_dir_of_config;
    vector<string> cmake_args = cmake_args_in;

    if (pkg_name.empty()) {           // main project
        source_dir = pkg_source_dir;  // cwd-relative or absolute
        pkg_bin_dir_of_config =
            cfg.main_binary_dir_of_config(config, cmakex_cache.per_config_bin_dirs);

        // make sure CMAKE_PREFIX_PATH contains the deps install dir
        const string deps_install_dir = cfg.deps_install_dir();
        string cmake_prefix_path_value;
        string cmake_prefix_path_type;
        do {  // scope for break
            if (!fs::is_directory(deps_install_dir))
                break;
            bool b;
            // try to prepend the CMAKE_PREFIX_PATH arg (if it exists) with deps install dir
            tie(cmake_args, b) = cmake_args_prepend_cmake_prefix_path(cmake_args, deps_install_dir);
            if (b)
                break;  // current cmake_args contains CMAKE_PREFIX_PATH, and we prepended it
            cmake_prefix_path_value = deps_install_dir;
            auto cmake_cache_path = pkg_bin_dir_of_config + "/CMakeCache.txt";
            if (!fs::is_regular_file(cmake_cache_path))
                break;  // no CMakeCache.txt (initial build), add deps_install as only prefix
            auto cmake_cache = read_cmake_cache(cmake_cache_path);
            auto it = cmake_cache.vars.find("CMAKE_PREFIX_PATH");
            auto itt = cmake_cache.types.find("CMAKE_PREFIX_PATH");
            if (itt != cmake_cache.types.end())
                cmake_prefix_path_type = itt->second;
            if (it == cmake_cache.vars.end() || it->second.empty())
                break;  // CMAKE_PREFIX_PATH not set in cache, add deps_install as only prefix
            auto dirs = split(it->second, ';');
            for (auto& d : dirs) {
                if (fs::is_directory(d) && fs::equivalent(d, deps_install_dir)) {
                    cmake_prefix_path_value.clear();
                    break;
                }
            }
            if (cmake_prefix_path_value.empty())
                break;                                    // already added
            cmake_prefix_path_value += ";" + it->second;  // prepend existing
        } while (false);
        if (!cmake_prefix_path_value.empty())
            cmake_args.emplace_back(stringf(
                "-DCMAKE_PREFIX_PATH%s=%s",
                cmake_prefix_path_type.empty() ? "" : (":" + cmake_prefix_path_type).c_str(),
                cmake_prefix_path_value.c_str()));
    } else {
        source_dir = cfg.pkg_clone_dir(pkg_name);
        if (!pkg_source_dir.empty())
            source_dir += "/" + pkg_source_dir.str();
        pkg_bin_dir_of_config =
            cfg.pkg_binary_dir_of_config(pkg_name, config, cmakex_cache.per_config_bin_dirs);

        // check if there's no install_prefix
        for (auto& c : cmake_args) {
            if (!starts_with(c, "-DCMAKE_INSTALL_PREFIX"))
                continue;
            auto pca = parse_cmake_arg(c);
            if (pca.name == "CMAKE_INSTALL_PREFIX") {
                throwf(
                    "Internal error: global and package cmake_args should not change "
                    "CMAKE_INSTALL_PREFIX: '%s'",
                    c.c_str());
            }
        }
        cmake_args.emplace_back(
            stringf("-DCMAKE_INSTALL_PREFIX=%s", cfg.deps_install_dir().c_str()));
    }

    const auto cmake_cache_path = pkg_bin_dir_of_config + "/CMakeCache.txt";
    const bool initial_build = !fs::is_regular_file(cmake_cache_path);

    cmake_cache_t cmake_cache;
    if (initial_build)
        remove_cmake_cache_tracker(pkg_bin_dir_of_config);
    else
        cmake_cache = read_cmake_cache(cmake_cache_path);

    bool cmake_build_type_changing = false;
    if (!cmakex_cache.multiconfig_generator) {
        string current_cmake_build_type = map_at_or_default(cmake_cache.vars, "CMAKE_BUILD_TYPE");

        if (config.is_noconfig()) {
            if (!current_cmake_build_type.empty()) {
                cmake_args.emplace_back(stringf("-UCMAKE_BUILD_TYPE"));
                cmake_build_type_changing = true;
            }
        } else {
            auto target_cmake_build_type = config.get_prefer_empty();
            if (current_cmake_build_type != target_cmake_build_type) {
                cmake_args.emplace_back("-DCMAKE_BUILD_TYPE=" + target_cmake_build_type);
                cmake_build_type_changing = true;
            }
        }
    }  // if multiconfig generator

    force_config_step = force_config_step || initial_build;

    // clean first handled specially
    // 1. it should be applied only once per config
    // 2. sometimes it should be applied automatically
    bool clean_first = is_one_of("--clean-first", build_args);
    bool clean_first_added = false;
    if (clean_first) {
        build_args.erase(
            remove_if(BEGINEND(build_args), [](const string& x) { return x == "--clean-first"; }),
            build_args.end());
    }

    {  // scope only
        auto cct = load_cmake_cache_tracker(pkg_bin_dir_of_config);
        cct.add_pending(cmake_args);
        save_cmake_cache_tracker(pkg_bin_dir_of_config, cct);

        // do config step only if needed
        if (force_config_step || !cct.pending_cmake_args.empty()) {
            if (cmake_build_type_changing && !initial_build) {
                clean_first_added = !clean_first;
                clean_first = true;
            }

            auto cmake_args_to_apply = cct.pending_cmake_args;
            cmake_args_to_apply.insert(
                cmake_args_to_apply.begin(),
                {string("-H") + source_dir, string("-B") + pkg_bin_dir_of_config});

            log_exec("cmake", cmake_args_to_apply);

            int r;
            if (pkg_name.empty()) {
                r = exec_process("cmake", cmake_args_to_apply);
            } else {
                OutErrMessagesBuilder oeb(pipe_capture, pipe_capture);
                r = exec_process("cmake", cmake_args_to_apply, oeb.stdout_callback(),
                                 oeb.stderr_callback());
                auto oem = oeb.move_result();

                save_log_from_oem("CMake-configure", r, oem, cfg.cmakex_log_dir(),
                                  stringf("%s-%s-configure%s", pkg_name.c_str(),
                                          config.get_prefer_NoConfig().c_str(), k_log_extension));
            }
            if (r != EXIT_SUCCESS) {
                if (initial_build && fs::is_regular_file(cmake_cache_path))
                    fs::remove(cmake_cache_path);
                throwf("CMake configure step failed, result: %d.", r);
            }

            // if cmake_args_to_apply is empty then this may not be executed
            // but if it's empty there are no pending variables so the cmake_config_ok
            // will not be missing
            cct.confirm_pending();
            save_cmake_cache_tracker(pkg_bin_dir_of_config, cct);

            // after successful configuration the cmake generator setting has been validated and
            // fixed so we can write out the cmakex cache if it's dirty
            // when processing dependencies the cmakex cache has already been written out after
            // configuring the helper project
            write_cmakex_cache_if_dirty(binary_dir, cmakex_cache);
        }
    }

    for (auto& target : build_targets) {
        vector<string> args = {"--build", pkg_bin_dir_of_config.c_str()};
        if (!target.empty()) {
            append_inplace(args, vector<string>({"--target", target.c_str()}));
        }

        if (cmakex_cache.multiconfig_generator) {
            CHECK(!config.is_noconfig());
            append_inplace(args,
                           vector<string>({"--config", config.get_prefer_NoConfig().c_str()}));
        }

        append_inplace(args, build_args);

        // when changing CMAKE_BUILD_TYPE the makefile generators usually fail to update the
        // configuration-dependent things. An automatic '--clean-first' helps
        if (target == "clean")
            clean_first = false;
        else if (clean_first) {
            if (clean_first_added)
                log_warn(
                    "Automatically adding '--clean-first' because CMAKE_BUILD_TYPE is changing");
            args.emplace_back("--clean-first");
            clean_first = false;  // add only for the first target
        }

        if (!native_tool_args.empty()) {
            args.emplace_back("--");
            append_inplace(args, native_tool_args);
        }

        log_exec("cmake", args);
        {  // scope only
            int r;
            if (pkg_name.empty()) {
                r = exec_process("cmake", args);
            } else {
                OutErrMessagesBuilder oeb(pipe_capture, pipe_capture);
                r = exec_process("cmake", args, oeb.stdout_callback(), oeb.stderr_callback());
                auto oem = oeb.move_result();

                save_log_from_oem(
                    "Build", r, oem, cfg.cmakex_log_dir(),
                    stringf("%s-%s-build-%s%s", pkg_name.c_str(),
                            config.get_prefer_NoConfig().c_str(),
                            target.empty() ? "all" : target.c_str(), k_log_extension));

                if (r == EXIT_SUCCESS && target == "install") {
                    // write out hijack module that tries the installed config module first
                    // collect the config-modules that has been written
                    for (int i = 0; i < oem.size(); ++i) {
                        auto msg = oem.at(i);
                        auto colon_pos = msg.text.find(':');
                        if (colon_pos == string::npos)
                            continue;
                        fs::path path(trim(msg.text.substr(colon_pos + 1)));
                        if (!fs::is_regular_file(path))
                            continue;
                        auto filename = path.filename().string();
                        string base;
                        for (auto e : {"-config.cmake", "Config.cmake"}) {
                            if (ends_with(filename, e)) {
                                base = filename.substr(0, filename.size() - strlen(e));
                                break;
                            }
                        }
                        if (base.empty())
                            continue;
                        // find out if there's such an official config module
                        if (cmakex_cache.cmake_root.empty())
                            continue;
                        auto find_module =
                            cmakex_cache.cmake_root + "/Modules/Find" + base + ".cmake";
                        if (!fs::is_regular_file(find_module))
                            continue;

                        write_hijack_module(base, binary_dir);
                    }
                }
            }
            if (r != EXIT_SUCCESS)
                throwf("CMake build step failed, result: %d.", r);
        }
    }  // for targets

// todo
#if 0
        // test step
        if (pars.flag_t) {
            auto tic = high_resolution_clock::now();
            string step_string =
                stringf("test step%s", config.empty() ? "" : (string(" (") + config + ")").c_str());
            log_info("Begin %s", step_string.c_str());
            fs::current_path(pars.binary_dir);
            vector<string> args;
            if (!config.empty()) {
                args.emplace_back("-C");
                args.emplace_back(config);
            }
            log_exec("ctest", args);
            int r = exec_process("ctest", args);
            if (r != EXIT_SUCCESS)
                throwf("Testing failed with error code %d", r);
            log_info(
                "End %s, elapsed %s", step_string.c_str(),
                sx::format_duration(dur_sec(high_resolution_clock::now() - tic).count()).c_str());
        }
#endif
}
}
