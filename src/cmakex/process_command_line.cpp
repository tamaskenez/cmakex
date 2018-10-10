#include "filesystem.h"

#include <nosx/check.h>

#include "cmakex_utils.h"
#include "getpreset.h"
#include "misc_utils.h"
#include "print.h"
#include "process_command_line.h"

namespace cmakex {

const char* default_cmakex_preset_filename();

namespace fs = filesystem;

static const char* const cmakex_version_with_meta = STRINGIZE(CMAKEX_VERSION_WITH_META);

static const char* const brief_usage_text =
    R"~~~~(- lightweight package manager + multiple repos for CMake

Usage: cmakex [--help] [--version]
              [c][b][i][t][d][r][w] [<source/build-dir-spec]
              [<cmake-args>...] [<additional-args>...]
              [-- <native-build-tool-args>...]

For brief help, use `--help`
For detailed help, see README.md

)~~~~";

static const char* const usage_text =
    R"~~~~(- lightweight package manager + multiple repos for CMake

Usage: cmakex [--help] [--version]
              [c][b][i][t][d][r][w] [<source/build-dir-spec]
              [<cmake-args>...] [<additional-args>...]
              [-- <native-build-tool-args>...]

For detailed help, see README.md
Note: Invoke CTest is not yet implemented.

General commands:
=================

    --help       display this message
    --version    display version information
    -V           verbose

CMake-wrapper mode:
===================

Execute multiple CMake commands with concise syntax.

The command-word consisting of the letters c, b, i, t, d, r, w specifies the
CMake steps to execute and the configurations.

Use one or more of:

    c, b, i, t: to perform CMake configure/build/install/test steps
       d, r, w: for Debug, Release and RelWithDebInfo configurations

The order of the letters is not important.

<source/build-dir-spec> is one of
---------------------------------

    1. -H <path-to-source> -B <path-to-build>
    2. <path-to-existing-build>
    3. -B <path-to-existing-build>
    4. <path-to-source> (build directory is the current working directory)

`-H` and `-B` options can be written with or without spaces.

Accepted <cmake-args>:
----------------------

For detailed help on these arguments see the documentation of the `cmake`
command: https://cmake.org/cmake/help/latest/manual/cmake.1.html

    --config <cfg>: For specifying configs other than Debug, Release
                    RelWithDebInfo. Can be used multiple times.

    --target <tgt>: For specifying targets other than ALL (default) and INSTALL
                    (for that one, use 'i' in the command-word). Can be used
                    multiple times.

    --clean-first

    -C, -D, -U, -G, -T, -A

    -N, all the -W* options

    --debug-trycompile, --debug-output, --trace, --trace-expand
    --warn-uninitialized, --warn-unused-vars, --no-warn-unused-cli,
    --check-system-vars, --graphwiz=

Package-management
==================

    --deps[=<path>]
                  Before executing the cmake-steps on the main project, process
                  the dependency-script at <source-dir>/deps.cmake (default) or
                  at <path> and download/configure/build/install the packages
                  defined in the script (on demand)

    --deps-only[=<path>]
                  Same as `--deps` but does not process the main project.
                  Tip: use `cmakex -B <path-to-new-or-existing-build>
                  --deps-only=<path> ...` to build a list of packages without a
                  main project

    --force-build Configure and build each dependency even if no build options
                  or dependencies have been changed for a package.

    --update[=MODE]
                  The update operation tries to set the previously cloned repos
                  of the dependencies to the state as if they were freshly cloned.
                  The MODE option can be `if-clean`, `if-very-clean`, `all-clean`,
                  `all-very-clean` and `force`.

                  The `all-clean` mode updates only if there are no local changes
                  and allows only fast-forward merge. It may change (or leave) the
                  current branch, for another branch, tag or commit, as needed.
                  Halts with an error if the update is not possible.

                  The `all-very-clean` is similar does not allow to change (or
                  leave) the current branch.

                  The `if-clean` and `if-very-clean` modes are similar, but they
                  don't stop at the first error, simply skip updating the project
                  where it's not possible.

                  The `force` mode updates in all cases, may abandon local changes
                  and commits (uses 'git reset --hard' when fast-forward merge is
                  not possible)

                  The default MODE is 'all-very-clean`, the safest mode.

    --update-includes
                  The CMake `include()` command used in the dependency scripts
                  can include a URL. The file the URL refers to will be
                  downloaded and included with the normal `include` command.
                  Further runs will use the local copy. Use this option to purge
                  the local copies and download the files again.

Presets
=======

    -p <path>#preset[#preset]...
                  Load the YAML file from <path> and add the args defined for
                  the presets to the current command line.

    -p preset[#preset]...
                  Use the file specified in the CMAKEX_PRESET_FILE environment
                  variable.

If the CMAKEX_PRESET_FILE environment variable is not set and there's a
`default-cmakex-presets.yaml` file in the directory of the cmakex executable
it will be used as default preset file.

Miscellaneous
=============

    --manifest=<path>
              Save a manifest file to <path> which can be used to reproduce the
              same build later. It contains `add_pkg` commands to describe the
              dependencies and also information about the command line and the
              main project. It's advised to use the `.cmake` extenstion so it
              can be given to the `--deps=` argument later.
              
    -q        Quiet logging of cmake operations on dependencies. The stdout and
              stderr from cmake will only be saved to disk but not forwarded to
              stdout, except if the command fails.

Environment variables
---------------------

    CMAKEX_LOG_GIT=<cmake-recognized-boolean-value>
              If enabled, logs all git commands to stdout. For debugging.


cmakex configuration
====================

    --deps-source=<DIR>
    --deps-build=<DIR>
    --deps-install=<DIR>
                    Parent directories of the of the source directories (cloned
                    repositories), build directories and install directory for
                    the dependencies. The default values are `_deps`,
                    `_deps-build` and `_deps-install` under `CMAKE_BINARY_DIR`.

    --single-build-dir
                    With makefile-generators (as opposed to multiconfig) the
                    default is to create separate build directories for each
                    configuration (Debug, Release, etc..).
                    Use this option to force a single build directory.
                    Effective only on the initial configuration and only
                    for makefile-generators.

Examples:
=========

Configure, install and test a project from scrach, for `Debug` and `Release`
configurations, clean build:

    cmakex itdr -H . -B b -DCMAKE_INSTALL_PREFIX=$PWD/out -DFOO=BAR

Configure new project, `Debug` config, use a preset:

    cmakex cd -H . -B b -p preset-dir/presets.yaml#android-toolchain

Build `Release` config in existing build dir, with dependencies

    cmakex br my-build-dir --deps
    
)~~~~";

AW_NORETURN void display_usage_and_exit(int exit_code, bool brief)
{
    auto s = stringf("cmakex v%s", cmakex_version_with_meta);
    fprintf(exit_code ? stderr : stdout, "%s ", s.c_str());
    fprintf(exit_code ? stderr : stdout, "%s", brief ? brief_usage_text : usage_text);
    exit(exit_code);
}

AW_NORETURN void display_version_and_exit(int exit_code)
{
    fprintf(exit_code ? stderr : stdout, "%s\n", cmakex_version_with_meta);
    exit(exit_code);
}

bool is_valid_binary_dir(string_par x)
{
    return fs::is_regular_file(x.str() + "/" + k_cmakex_cache_filename) ||
           fs::is_regular_file(x.str() + "/CMakeCache.txt");
}

bool is_valid_source_dir(string_par x)
{
    return fs::is_regular_file(x.str() + "/CMakeLists.txt");
}

fs::path strip_trailing_dot(const fs::path& x)
{
    string s = x.string();
    if (s.size() >= 2) {
        auto t = s.substr(s.size() - 2);
        if (t == "/." || t == "\\.")
            return s.substr(0, s.size() - 2);
    }
    return x;
}

command_line_args_cmake_mode_t process_command_line_1(int argc, char* argv[])
{
    command_line_args_cmake_mode_t pars;
    if (argc <= 1)
        display_usage_and_exit(EXIT_FAILURE, true);

    for (int argix = 1; argix < argc; ++argix) {
        string arg = argv[argix];
        if ((argix == 1 && arg == "help") || arg == "--help")
            display_usage_and_exit(EXIT_SUCCESS, false);
        if ((argix == 1 && arg == "version") || arg == "--version")
            display_version_and_exit(EXIT_SUCCESS);
    }

    for (int argix = 1; argix < argc; ++argix) {
        string arg = argv[argix];

        // subcommand
        if (argix == 1 && !arg.empty() && arg[0] != '-') {
            for (auto c : arg) {
                switch (c) {
                    case 'c':
                        pars.flag_c = true;
                        break;
                    case 'b':
                        pars.flag_b = true;
                        break;
                    case 'i':
                        pars.build_targets.emplace_back("install");
                        break;
                    case 't':
                        pars.flag_t = true;
                        break;
                    case 'd':
                        pars.configs.emplace_back("Debug");
                        break;
                    case 'r':
                        pars.configs.emplace_back("Release");
                        break;
                    case 'w':
                        pars.configs.emplace_back("RelWithDebInfo");
                        break;
                    default:
                        badpars_exit(stringf("Invalid character in subcommand: %c", c));
                }
            }
            pars.subcommand = arg;
        } else {
            if (arg == "-V")
                continue;  // verbose flag processed earlier
            if (pars.subcommand.empty())
                badpars_exit("Invalid arguments: the first argument must be the subcommand.");
            if (arg == "--") {
                pars.native_tool_args.assign(argv + argix + 1, argv + argc);
                break;
            }
            if (arg == "--target") {
                if (++argix >= argc)
                    badpars_exit("Missing target name after '--target'");
                pars.build_targets.emplace_back(argv[argix]);
            } else if (arg == "--config") {
                if (++argix >= argc)
                    badpars_exit("Missing config name after '--config'");
                string c = trim(argv[argix]);
                if (c.empty())
                    badpars_exit("Invalid empty argument for '--config'");
                pars.configs.emplace_back(c);
            } else if (is_one_of(arg, {"--clean-first", "--use-stderr"})) {
                pars.build_args.emplace_back(arg);
            } else if (arg == "--deps" || starts_with(arg, "--deps=")) {
                if (pars.deps_mode != dm_main_only)
                    badpars_exit("'--deps' or '--deps-only' specified multiple times");
                pars.deps_mode = dm_deps_and_main;
                if (starts_with(arg, "--deps=")) {
                    pars.deps_script = make_string(butleft(arg, strlen("--deps=")));
                    if (pars.deps_script.empty())
                        badpars_exit("Missing path after '--deps='");
                }
            } else if (arg == "--deps-only" || starts_with(arg, "--deps-only=")) {
                if (pars.deps_mode != dm_main_only)
                    badpars_exit("'--deps' or '--deps-only' specified multiple times");
                pars.deps_mode = dm_deps_only;
                if (starts_with(arg, "--deps-only=")) {
                    pars.deps_script = make_string(butleft(arg, strlen("--deps-only=")));
                    if (pars.deps_script.empty())
                        badpars_exit("Missing path after '--deps-only='");
                }
            } else if (arg == "--single-build-dir") {
                pars.single_build_dir = true;
            } else if (starts_with(arg, "-H")) {
                if (arg == "-H") {
                    // unlike cmake, here we support the '-H <path>' style, too
                    if (++argix >= argc)
                        badpars_exit("Missing path after '-H'");
                    pars.arg_H = argv[argix];
                } else
                    pars.arg_H = make_string(butleft(arg, 2));
            } else if (starts_with(arg, "-B")) {
                if (arg == "-B") {
                    // unlike cmake, here we support the '-B <path>' style, too
                    if (++argix >= argc)
                        badpars_exit("Missing path after '-B'");
                    pars.arg_B = argv[argix];
                } else
                    pars.arg_B = make_string(butleft(arg, 2));
            } else if (arg == "-p") {
                if (++argix >= argc)
                    badpars_exit("Missing argument after '-p'");
                if (!pars.arg_p.empty())
                    badpars_exit("Multiple '-p' options.");
                pars.arg_p = argv[argix];
            } else if (arg == "--force-build") {
                pars.force_build = true;
            } else if (starts_with(arg, "--manifest=")) {
                pars.manifest = make_string(butleft(arg, strlen("--manifest=")));
                if (pars.manifest.empty())
                    badpars_exit("Missing path after '--manifest='");
            } else if (arg == "--update-includes") {
                pars.clear_downloaded_include_files = true;
            } else if (starts_with(arg, "--deps-source=")) {
                pars.deps_source_dir = make_string(butleft(arg, strlen("--deps-source=")));
                if (pars.deps_source_dir.empty())
                    badpars_exit("Missing path after '--deps-source='");
            } else if (starts_with(arg, "--deps-build=")) {
                pars.deps_build_dir = make_string(butleft(arg, strlen("--deps-build=")));
                if (pars.deps_build_dir.empty())
                    badpars_exit("Missing path after '--deps-build='");
            } else if (starts_with(arg, "--deps-install=")) {
                pars.deps_install_dir = make_string(butleft(arg, strlen("--deps-install=")));
                if (pars.deps_install_dir.empty())
                    badpars_exit("Missing path after '--deps-install='");
            } else if (arg == "--update" || starts_with(arg, "--update=")) {
                string mode;
                if (arg == "--update")
                    mode = "all-very-clean";
                else
                    mode = make_string(butleft(arg, strlen("--update=")));
                if (mode == "all-very-clean")
                    pars.update_mode = update_mode_all_very_clean;
                else if (mode == "all-clean")
                    pars.update_mode = update_mode_all_clean;
                else if (mode == "if-very-clean")
                    pars.update_mode = update_mode_if_very_clean;
                else if (mode == "if-clean")
                    pars.update_mode = update_mode_if_clean;
                else if (mode == "force")
                    pars.update_mode = update_mode_force;
                else
                    badpars_exit(stringf("Invalid mode in '%s'", arg.c_str()));
            } else if (arg == "-q") {
                g_supress_deps_cmake_logs = true;
            } else if (!starts_with(arg, '-')) {
                pars.free_args.emplace_back(arg);
            } else {
                if (arg.size() == 2 && is_one_of(arg, {"-C", "-D", "-U", "-G", "-T", "-A"})) {
                    if (++argix >= argc)
                        badpars_exit(stringf("Missing argument after '%s'", arg.c_str()));
                    arg += argv[argix];
                }

                try {
                    auto pca = parse_cmake_arg(arg);
                } catch (...) {
                    badpars_exit(stringf("Invalid option: '%s'", arg.c_str()));
                }

                pars.cmake_args.emplace_back(arg);
            }  // last else
        }
    }  // foreach arg
    return pars;
}

tuple<processed_command_line_args_cmake_mode_t, cmakex_cache_t> process_command_line_2(
    const command_line_args_cmake_mode_t& cla)

{
    processed_command_line_args_cmake_mode_t pcla;
    *static_cast<base_command_line_args_cmake_mode_t*>(&pcla) = cla;  // slice to common base

    // complete relative paths for CMAKE_INSTALL_PREFIX, CMAKE_PREFIX_PATH
    for (auto& a : pcla.cmake_args) {
        auto b = parse_cmake_arg(a);
        bool changed = false;
        if (b.switch_ == "-D" &&
            is_one_of(b.name, {"CMAKE_PREFIX_PATH", "CMAKE_MODULE_PATH", "CMAKE_INSTALL_PREFIX",
                               "CMAKE_TOOLCHAIN_FILE"})) {
#ifdef _WIN32
            for (auto& c : b.value)
                if (c == '\\') {
                    c = '/';
                    changed = true;
                }
#endif
            if ((b.name == "CMAKE_PREFIX_PATH" || b.name == "CMAKE_INSTALL_PREFIX") &&
                b.switch_ == "-D") {
                auto v = cmakex_prefix_path_to_vector(b.value, false);
                for (auto& p : v)
                    p = fs::lexically_normal(fs::absolute(p));
                b.value = join(v, ";");
                changed = true;
            }
            if (changed)
                a = format_cmake_arg(b);
        }
    }

    // resolve preset
    if (!cla.arg_p.empty()) {
        string msg;
        string file;
        vector<string> preset_args, names, aliases;
        try {
            tie(names, file, aliases) = libgetpreset::getpreset(cla.arg_p, "name");
            tie(preset_args, file, aliases) = libgetpreset::getpreset(cla.arg_p, "args");
        } catch (const exception& e) {
            msg = e.what();
        } catch (...) {
            msg = "Unknown error";
        }
        if (!msg.empty())
            throwf(
                "Invalid '-p' argument, reason: %s Remember, you can specify preset file with '-p "
                "<file>#preset' or in the "
                "CMAKEX_PRESET_FILE environment variable, or copying '%s' to "
                "the directory of the cmakex executable.",
                msg.c_str(), default_cmakex_preset_filename());
        CHECK(!names.empty());
        if (names.size() == 1)
            log_info("Using preset `%s` from %s", names[0].c_str(), path_for_log(file).c_str());
        else
            log_info("Using presets [%s] from %s", join(names, ", ").c_str(),
                     path_for_log(file).c_str());
        auto npa = normalize_cmake_args(preset_args);
        if (g_verbose) {
            log_info("CMAKE_ARGS from preset: [%s]", join(preset_args, ", ").c_str());
            log_info("normalized CMAKE_ARGS from preset: [%s]", join(npa, ", ").c_str());
        }
        pcla.cmake_args = normalize_cmake_args(concat(preset_args, pcla.cmake_args));
    }

    if (g_verbose)
        log_info("Global CMAKE_ARGS: [%s]", join(pcla.cmake_args, ", ").c_str());

    // -B, valid binary
    // -B, any binary --deps-only
    // -H and -B
    // <path-to-existing-binary>
    // <path-to-source>, binary=pwd

    if (cla.free_args.empty()) {
        if (cla.arg_H.empty()) {
            if (cla.arg_B.empty()) {
                // no -H, -B, free-arg
                badpars_exit(
                    "No source or binary directories specified. CMake would set both to the "
                    "current directory but cmakex requires the source and binary directories "
                    "to be "
                    "different.");
            } else {
                // only -B
                pcla.binary_dir = cla.arg_B;
                if (cla.deps_mode != dm_deps_only && !is_valid_binary_dir(cla.arg_B))
                    badpars_exit(
                        "Only a binary directory is specified: it must be an existing "
                        "binary-dir.");
            }
        } else {
            if (cla.arg_B.empty()) {
                // only H specified
                badpars_exit("Missing '-B' option for the '-H' option.");
            } else {
                // -H and -B, -H must be exisiting source dir
                pcla.source_dir = cla.arg_H;
                pcla.binary_dir = cla.arg_B;
                if (!is_valid_source_dir(cla.arg_H))
                    badpars_exit(stringf("Invalid source dir (no CMakeLists.txt): %s",
                                         path_for_log(cla.arg_H).c_str()));
            }
        }
    } else if (cla.free_args.size() == 1) {
        if (!cla.arg_H.empty() || !cla.arg_B.empty())
            badpars_exit(
                "Specify either '-H' and '-B' options or a standalone path "
                "(path-to-existing-build "
                "or path-to-source).");
        string d = cla.free_args[0];
        bool b = is_valid_binary_dir(d);
        bool s = is_valid_source_dir(d);
        if (b) {
            if (s) {
                badpars_exit(
                    stringf("The directory %s is both a valid source and a valid binary "
                            "directory. This is not permitted with cmakex",
                            path_for_log(d).c_str()));
            } else {
                pcla.binary_dir = d;
            }
        } else {
            if (s) {
                pcla.source_dir = d;
                pcla.binary_dir = fs::current_path().string();
            } else {
                badpars_exit(
                    stringf("The directory %s is neither a valid source (no CMakeLists.txt), "
                            "nor a valid binary directory.",
                            path_for_log(d).c_str()));
            }
        }
    } else if (cla.free_args.size() > 1) {
        badpars_exit(
            stringf("More than one free arguments: %s", join(cla.free_args, ", ").c_str()));
    }

    // at this point we have a new or an existing binary dir
    CHECK(!pcla.binary_dir.empty());
    CHECK(pcla.source_dir.empty() || fs::is_directory(pcla.source_dir));

    fs::create_directories(pcla.binary_dir);

    if (!pcla.source_dir.empty()) {
        if (fs::equivalent(pcla.source_dir, pcla.binary_dir))
            badpars_exit(
                stringf("The source and binary dirs are identical: %s, this is not permitted "
                        "with cmakex",
                        path_for_log(fs::canonical(pcla.source_dir.c_str()).c_str()).c_str()));
    }

    // extract source dir from existing binary dir
    cmakex_config_t cfg(pcla.binary_dir);

    cmake_cache_t cmake_cache;
    bool binary_dir_has_cmake_cache =
        false;  // silencing warning, otherwise it always will be set before read
    bool cmake_cache_checked = false;
    auto check_cmake_cache = [&pcla, &binary_dir_has_cmake_cache, &cmake_cache_checked,
                              &cmake_cache]() {
        if (cmake_cache_checked)
            return;

        const string cmake_cache_path = pcla.binary_dir + "/CMakeCache.txt";

        binary_dir_has_cmake_cache = fs::is_regular_file(cmake_cache_path);
        cmake_cache_checked = true;

        if (!binary_dir_has_cmake_cache)
            return;

        cmake_cache = read_cmake_cache(cmake_cache_path);
    };

    cmakex_cache_t cmakex_cache = cfg.cmakex_cache();
    const bool cmakex_cache_was_valid = cmakex_cache.valid;

    string home_directory;

    // we need source dir if it's not deps_only or empty deps_only
    if (pcla.deps_mode != dm_deps_only || pcla.deps_script.empty()) {
        if (cmakex_cache.valid) {
            if (cmakex_cache.home_directory.empty())
                cmakex_cache.home_directory = pcla.source_dir;

            string source_dir = cmakex_cache.home_directory;

            if (!pcla.source_dir.empty() && !fs::equivalent(source_dir, pcla.source_dir)) {
                badpars_exit(
                    stringf("The source dir specified %s is different from the one stored in the "
                            "%s: %s",
                            path_for_log(pcla.source_dir).c_str(), k_cmakex_cache_filename,
                            path_for_log(source_dir).c_str()));
            }
            if (pcla.source_dir.empty())
                pcla.source_dir = source_dir;
        } else {
            check_cmake_cache();
            if (binary_dir_has_cmake_cache) {
                string source_dir = cmake_cache.vars["CMAKE_HOME_DIRECTORY"];
                if (!pcla.source_dir.empty() && !fs::equivalent(source_dir, pcla.source_dir)) {
                    badpars_exit(stringf(
                        "The source dir specified %s is different from the one "
                        "stored in "
                        "CMakeCache.txt: %s",
                        path_for_log(pcla.source_dir).c_str(), path_for_log(source_dir).c_str()));
                }
                if (pcla.source_dir.empty())
                    pcla.source_dir = source_dir;
            }
        }

        if (pcla.source_dir.empty())
            badpars_exit(
                stringf("No source dir has been specified and the binary dir %s is not valid "
                        "(contains no CMakeCache.txt or %s)%s",
                        path_for_log(pcla.binary_dir).c_str(), k_cmakex_cache_filename,
                        pcla.deps_mode == dm_deps_only
                            ? ". In '--deps-only' mode you can omit source dir only "
                              "if you specify a script with '--deps-only=<DIR>'"
                            : ""));

        CHECK(!pcla.source_dir.empty());

        if (!is_valid_source_dir(pcla.source_dir))
            badpars_exit(stringf("The source dir %s is not valid (no CMakeLists.txt)",
                                 path_for_log(pcla.source_dir).c_str()));

        home_directory = fs::canonical(pcla.source_dir).string();
    }

    if (cmakex_cache.valid) {
        if (cmakex_cache.home_directory.empty())
            cmakex_cache.home_directory = home_directory;
        else
            CHECK(fs::equivalent(cmakex_cache.home_directory, home_directory));
    } else {
        cmakex_cache.home_directory = home_directory;
        string cmake_generator;
        check_cmake_cache();
        if (binary_dir_has_cmake_cache) {
            cmake_generator = cmake_cache.vars["CMAKE_GENERATOR"];
            THROW_IF(
                cmake_generator.empty(),
                "The file %s/CMakeCache.txt contains invalid (empty) CMAKE_GENERATOR variable.",
                pcla.binary_dir.c_str());
        } else {
            // new binary dir
            cmake_generator = extract_generator_from_cmake_args(pcla.cmake_args);
        }
        cmakex_cache.multiconfig_generator = is_generator_multiconfig(cmake_generator);
        bool b = !cmakex_cache.multiconfig_generator && cfg.per_config_bin_dirs();
        if (binary_dir_has_cmake_cache && b) {
            b = false;
            log_warn(
                "Per-config build directories were requested but this is an existing, "
                "non-cmakex-enabled, single build directory, so we're using single build "
                "directories for this build. Remove the build directory and reconfigure for "
                "per-config build directories.");
        }
        cmakex_cache.per_config_bin_dirs = b;
        cmakex_cache.valid = true;
    }

    string cmake_build_type;
    bool defines_cmake_build_type = false;
    bool undefines_cmake_build_type = false;
    for (auto it = pcla.cmake_args.begin(); it != pcla.cmake_args.end(); /* no incr */) {
        auto pca = parse_cmake_arg(*it);
        if (pca.name == "CMAKE_BUILD_TYPE") {
            if (pca.switch_ == "-D") {
                cmake_build_type = trim(pca.value);
                undefines_cmake_build_type = false;
                defines_cmake_build_type = true;
            } else if (pca.switch_ == "-U") {
                cmake_build_type.clear();
                undefines_cmake_build_type = true;
                defines_cmake_build_type = false;
            }
            it = pcla.cmake_args.erase(it);
        } else
            ++it;
    }

    if (pcla.configs.empty()) {
        // for multiconfig generator not specifying a config defaults to Debug
        // for singleconfig, it's a special NoConfig configuration
        pcla.configs.assign(1, cmakex_cache.multiconfig_generator ? "Debug" : "");
    } else {
        for (auto& c : pcla.configs) {
            CHECK(!c.empty());  // this is an internal error
        }
        pcla.configs = stable_unique(pcla.configs);
    }

    // CMAKE_BUILD_TYPE rules
    // - if no config has been specified, this is only possible with single-config generators,
    // because multiconfig defaults to Debug. In this case, configs = {""} but it will be
    // overwritten with the config the CMAKE_BUILD_TYPE specifies
    // - otherwise it must be the same as the config specified otherwise
    // in all cases CMAKE_BUILD_TYPE itself will be removed from the command line

    CHECK(!pcla.configs.empty());  // previous lines must ensure this

    if (pcla.configs.size() == 1 && pcla.configs.front().empty()) {
        if (defines_cmake_build_type && !cmake_build_type.empty())
            pcla.configs.assign(1, cmake_build_type);
    } else {  // non-empty configs specified
        for (auto& c : pcla.configs) {
            CHECK(!c.empty());
            if (undefines_cmake_build_type)
                throwf(
                    "Incompatible configuration settings: CMAKE_BUILD_TYPE is removed with "
                    "'-U' but a configuration setting requires it to be defined to '%s'",
                    c.c_str());
            else if (defines_cmake_build_type && cmake_build_type != c)
                throwf(
                    "Incompatible configuration settings: CMAKE_BUILD_TYPE is defined to '%s' "
                    "but a configuration setting requires it to be defined to '%s'",
                    cmake_build_type.c_str(), c.c_str());
        }
    }

    if (!pcla.source_dir.empty()) {
        if (fs::path(pcla.source_dir).is_absolute())
            log_info("Using source dir: %s", path_for_log(pcla.source_dir).c_str());
        else {
            log_info(
                "Using source dir: %s -> %s", path_for_log(pcla.source_dir).c_str(),
                path_for_log(
                    strip_trailing_dot(fs::lexically_normal(fs::absolute(pcla.source_dir))).c_str())
                    .c_str());
        }
    }

    const char* using_or_creating =
        is_valid_binary_dir(pcla.binary_dir) ? "Using existing" : "Creating";

    if (fs::path(pcla.binary_dir).is_absolute())
        log_info("%s binary dir: %s", using_or_creating, path_for_log(pcla.binary_dir).c_str());
    else
        log_info(
            "%s binary dir: %s -> %s", using_or_creating, path_for_log(pcla.binary_dir).c_str(),
            path_for_log(
                strip_trailing_dot(fs::lexically_normal(fs::absolute(pcla.binary_dir))).c_str())
                .c_str());

    pcla.cmake_args = normalize_cmake_args(pcla.cmake_args);

    if (!cla.deps_source_dir.empty())
        cmakex_cache.deps_source_dir = fs::absolute(cla.deps_source_dir);
    else if (cmakex_cache.deps_source_dir.empty())
        cmakex_cache.deps_source_dir = cfg.default_deps_source_dir();
    if (!cla.deps_build_dir.empty())
        cmakex_cache.deps_build_dir = fs::absolute(cla.deps_build_dir);
    else if (cmakex_cache.deps_build_dir.empty())
        cmakex_cache.deps_build_dir = cfg.default_deps_build_dir();
    if (!cla.deps_install_dir.empty())
        cmakex_cache.deps_install_dir = fs::absolute(cla.deps_install_dir);
    else if (cmakex_cache.deps_install_dir.empty())
        cmakex_cache.deps_install_dir = cfg.default_deps_install_dir();

    if (cla.single_build_dir && !cmakex_cache.multiconfig_generator) {
        if (cmakex_cache_was_valid) {
            if (cmakex_cache.per_config_bin_dirs)
                log_warn(
                    "The --single-build-dir option is effective only on the initial "
                    "configuration.");
        } else
            cmakex_cache.per_config_bin_dirs = false;
    }

    return make_tuple(move(pcla), move(cmakex_cache));
}
}
