#include "install_deps_phase_one.h"

#include <nosx/algorithm.h>
#include <nosx/check.h>
#include <nosx/log.h>

#include "clone.h"
#include "cmakex_utils.h"
#include "exec_process.h"
#include "filesystem.h"
#include "git.h"
#include "helper_cmake_project.h"
#include "installdb.h"
#include "misc_utils.h"
#include "print.h"

namespace cmakex {

namespace fs = filesystem;

void idpo_recursion_result_t::add(const idpo_recursion_result_t& x)
{
    append_inplace(pkgs_encountered, x.pkgs_encountered);
    building_some_pkg |= x.building_some_pkg;
    normalize();
}

void idpo_recursion_result_t::normalize()
{
    std::sort(BEGINEND(pkgs_encountered));
    nosx::unique_trunc(pkgs_encountered);
}

void idpo_recursion_result_t::add_pkg(string_par x)
{
    pkgs_encountered.emplace_back(x.str());
    normalize();
}

idpo_recursion_result_t install_deps_phase_one_deps_script(
    string_par binary_dir,
    string_par deps_script_filename,
    const vector<string>& command_line_cmake_args,
    const vector<config_name_t>& command_line_configs,
    deps_recursion_wsp_t& wsp,
    const cmakex_cache_t& cmakex_cache);

// todo what if the existing r1 is name-only, r2 is normal
// is it possible or what to do then?
void verify_if_requests_are_compatible(const pkg_request_t& r1,
                                       const pkg_request_t& r2,
                                       bool was_accepted_from_prefix_path,
                                       bool dependencies_from_script)
{
    CHECK(r1.name == r2.name);
    CHECK(!r1.name_only());
    CHECK(!r2.name_only());
    auto& pkg_name = r1.name;

    // check for possible conflicts with existing requests
    auto& b1 = r1.b;
    auto& b2 = r2.b;
    auto& c1 = r1.c;
    auto& c2 = r2.c;

    // compare SOURCE_DIR
    auto& s1 = b1.source_dir;
    auto& s2 = b2.source_dir;
    if (s1 != s2) {
        throwf(
            "Different SOURCE_DIR args for the same package. The package '%s' is being "
            "added "
            "for the second time. The first time the SOURCE_DIR was \"%s\" and not it's "
            "\"%s\".",
            pkg_name.c_str(), s1.c_str(), s2.c_str());
    }

    // compare CMAKE_ARGS
    auto v = incompatible_cmake_args(b1.cmake_args, b2.cmake_args, true);
    if (!get<0>(v).empty()) {
        throwf(
            "Different CMAKE_ARGS args for the same package. The package '%s' is being "
            "added "
            "for the second time but the following CMAKE_ARGS are incompatible with the "
            "first "
            "request: %s",
            pkg_name.c_str(), join(get<0>(v), ", ").c_str());
    }

    // compare configs
    auto& ec1 = b1.configs();
    CHECK(!ec1.empty());
    const char* fcl = " (from command line)";
    const char* fcl1 = b1.using_default_configs() ? fcl : "";
    auto& ec2 = b2.configs();
    CHECK(!ec2.empty());
    const char* fcl2 = b2.using_default_configs() ? fcl : "";

    if (!was_accepted_from_prefix_path && ec1 != ec2) {
        auto v1 = join(get_prefer_NoConfig(ec1), ", ");
        auto v2 = join(get_prefer_NoConfig(ec2), ", ");
        throwf(
            "Different configurations requested for the same package. Previously, for "
            "package '%s' these configurations had been requested: [%s]%s and now these: [%s]%s",
            pkg_name.c_str(), v1.c_str(), fcl1, v2.c_str(), fcl2);
    }

    // compare clone pars
    if (c1.git_url != c2.git_url)
        throwf(
            "Different repository URLs requested for the same package. Previously, for "
            "%s this URL was specified: '%s', now this: '%s'",
            pkg_for_log(pkg_name).c_str(), c1.git_url.c_str(), c2.git_url.c_str());
    // for git_tag it's acceptable if the later git_tag is empty, it means don't care
    if (c1.git_tag != c2.git_tag && !c2.git_tag.empty())
        throwf(
            "Different commits requested for the same package. Previously, for "
            "%s this GIT_TAG was specified: '%s', now this: '%s'",
            pkg_for_log(pkg_name).c_str(), c1.git_tag.c_str(), c2.git_tag.c_str());

    // compare dependencies
    auto& d1 = r1.depends;
    auto& d2 = r2.depends;

    if (!dependencies_from_script && d1 != d2)
        throwf(
            "Different dependencies requested for the same package. Previously, for package "
            "'%s' these dependencies were requested: [%s], now these: [%s]",
            pkg_name.c_str(), join(d1, ", ").c_str(), join(d2, ", ").c_str());
}

void update_request(pkg_request_t& x, const pkg_request_t& y)
{
    // merge into exisiting definition
    CHECK(x.name == y.name);
    x.git_shallow |= y.git_shallow;
    if (!y.c.git_url.empty())
        x.c.git_url = y.c.git_url;
    if (!y.c.git_tag.empty()) {
        if (x.c.git_tag.empty()) {
            CHECK(!x.git_tag_override);
            x.c.git_tag = y.c.git_tag;
            x.git_tag_override = y.git_tag_override;
        } else {
            // both x and y are non-empty
            if (x.git_tag_override) {
                if (y.git_tag_override) {
                    // error if they're different
                    if (x.c.git_tag != y.c.git_tag)
                        throwf(
                            "%s: In def_pkg() commands two GIT_TAG_OVERRIDE options specify "
                            "different GIT_TAGs: '%s' and '%s'",
                            pkg_for_log(x.name).c_str(), x.c.git_tag.c_str(), y.c.git_tag.c_str());
                }
                // else ignore second, non-override GIT_TAG
            } else {
                if (!y.git_tag_override && x.c.git_tag != y.c.git_tag)
                    log_warn(
                        "%s: In def_pkg() commands, overwriting a previous GIT_TAG options: '%s' "
                        "-> '%s'",
                        pkg_for_log(x.name).c_str(), x.c.git_tag.c_str(), y.c.git_tag.c_str());
                x.c.git_tag = y.c.git_tag;
                x.git_tag_override = y.git_tag_override;
            }
        }
    } else {
        CHECK(!y.git_tag_override);
    }
    if (!y.b.source_dir.empty())
        x.b.source_dir = y.b.source_dir;
    x.b.cmake_args = normalize_cmake_args(concat(x.b.cmake_args, y.b.cmake_args));
    if (!y.depends.empty())
        x.depends = y.depends;
    if (!y.b.configs().empty())
        x.b.update_configs(y.b.configs(), y.b.using_default_configs());
}

void insert_new_definition_into_wsp(const pkg_request_t& req, deps_recursion_wsp_t& wsp)
{
    CHECK(req.define_only);

    auto it = wsp.pkg_def_map.find(req.name);
    if (it == wsp.pkg_def_map.end()) {
        // first time we encounter this package
        auto it = wsp.pkg_def_map.emplace(std::piecewise_construct, std::forward_as_tuple(req.name),
                                          std::forward_as_tuple(req));
        it.first->second.b.cmake_args = normalize_cmake_args(it.first->second.b.cmake_args);
    } else {
        update_request(it->second, req);
    }
}

// if it's not there, insert
// if it's there:
// - if it's compatible (identical), do nothing
// - if it's not compatible:
//   - if the existing is name-only: check if it's not yet processed and overwrite
//   - throw otherwise
void insert_new_request_into_wsp(const pkg_request_t& req_in, deps_recursion_wsp_t& wsp)
{
    if (req_in.define_only) {
        insert_new_definition_into_wsp(req_in, wsp);
        return;
    }
    auto it = wsp.pkg_map.find(req_in.name);
    auto itdef = wsp.pkg_def_map.find(req_in.name);
    maybe<pkg_request_t> maybe_defreq;
    const pkg_request_t* req = nullptr;
    if (itdef == wsp.pkg_def_map.end())
        req = &req_in;
    else {
        maybe_defreq = itdef->second;
        update_request(*maybe_defreq, req_in);
        req = &*maybe_defreq;
    }
    const bool to_be_processed = wsp.pkgs_to_process.count(req_in.name) > 0;
    if (it == wsp.pkg_map.end()) {
        // first time we encounter this package
        CHECK(!to_be_processed);
        LOG_TRACE("First time encountering %s", req_in.name.c_str());
        wsp.pkgs_to_process.insert(req_in.name);
        wsp.pkg_map.emplace(std::piecewise_construct, std::forward_as_tuple(req_in.name),
                            std::forward_as_tuple(*req));

    } else {
        if (it->second.request.name_only()) {
            CHECK(to_be_processed);
            if (!req->name_only()) {
                it->second.request = *req;
            }
        } else {
            if (!req->name_only()) {
                // handle git_tag_override
                auto& x = it->second.request;
                auto y = *req;
                if (x.git_tag_override && !y.git_tag_override)
                    y.c.git_tag = x.c.git_tag;
                verify_if_requests_are_compatible(it->second.request, y,
                                                  !it->second.found_on_prefix_path.empty(),
                                                  it->second.dependencies_from_script);
            }
        }
    }
}

idpo_recursion_result_t process_pkgs_to_process(string_par binary_dir,
                                                const vector<string>& command_line_cmake_args,
                                                const vector<config_name_t>& command_line_configs,
                                                deps_recursion_wsp_t& wsp,
                                                const cmakex_cache_t& cmakex_cache,
                                                const vector<string>& pkgs_to_process)
{
    idpo_recursion_result_t rr;

    for (auto& pkg_name : pkgs_to_process) {
        auto it = wsp.pkgs_to_process.find(pkg_name);
        if (it == wsp.pkgs_to_process.end()) {
            // if it's not there it means we've already processed it
            rr.add_pkg(pkg_name);  // but we still need to record that we've encountered it for the
                                   // requestor package

            // also need to record if the already processed package is about to build
            auto itp = wsp.pkg_map.find(pkg_name);
            if (itp == wsp.pkg_map.end())
                LOG_FATAL(
                    "Internal error, %s must have been already processed and it's not in pkg_map",
                    pkg_for_log(pkg_name).c_str());
            rr.building_some_pkg |= itp->second.building_now;
        } else {
            // we're processing it now
            wsp.pkgs_to_process.erase(it);
            auto rr_below = run_deps_add_pkg(pkg_name, binary_dir, command_line_cmake_args,
                                             command_line_configs, wsp, cmakex_cache);
            rr.add(rr_below);
        }
    }

    return rr;
}

idpo_recursion_result_t install_deps_phase_one_request_deps(
    string_par binary_dir,
    const vector<string>& request_deps,
    const vector<string>& command_line_cmake_args,
    const vector<config_name_t>& command_line_configs,
    deps_recursion_wsp_t& wsp,
    const cmakex_cache_t& cmakex_cache)
{
    if (g_verbose) {
        string p = wsp.requester_stack.empty() ? string("the main project")
                                               : pkg_for_log(wsp.requester_stack.back());
        if (request_deps.empty())
            log_verbose("No dependencies found for %s", p.c_str());
        else
            log_verbose("Dependencies for %s (from DEPENDS): %s", p.c_str(),
                        join(request_deps, ", ").c_str());
    }
    // for each pkg:
    for (auto& d : request_deps)
        insert_new_request_into_wsp(pkg_request_t(d, command_line_configs, true), wsp);

    return process_pkgs_to_process(binary_dir, command_line_cmake_args, command_line_configs, wsp,
                                   cmakex_cache, request_deps);
}

tuple<idpo_recursion_result_t, bool> install_deps_phase_one(
    string_par binary_dir,
    string_par source_dir,
    const vector<string>& request_deps,
    const vector<string>& command_line_cmake_args,
    const vector<config_name_t>& command_line_configs,
    deps_recursion_wsp_t& wsp,
    const cmakex_cache_t& cmakex_cache,
    string_par custom_deps_script_file)
{
    CHECK(!binary_dir.empty());
    CHECK(!command_line_configs.empty());
    if (!source_dir.empty() || !custom_deps_script_file.empty()) {
        string deps_script_file;
        if (custom_deps_script_file.empty()) {
            deps_script_file = fs::lexically_normal(fs::absolute(source_dir.str()).string() + "/" +
                                                    k_deps_script_filename);
            if (fs::is_regular_file(deps_script_file)) {
                if (!request_deps.empty())
                    log_warn("Using dependency script %s instead of specified dependencies.",
                             path_for_log(deps_script_file).c_str());
                return make_tuple(install_deps_phase_one_deps_script(
                                      binary_dir, deps_script_file, command_line_cmake_args,
                                      command_line_configs, wsp, cmakex_cache),
                                  true);
            }
        } else {
            deps_script_file = custom_deps_script_file;
            if (!request_deps.empty())
                log_warn("Using dependency script %s instead of specified dependencies.",
                         path_for_log(deps_script_file).c_str());
            return make_tuple(install_deps_phase_one_deps_script(
                                  binary_dir, deps_script_file, command_line_cmake_args,
                                  command_line_configs, wsp, cmakex_cache),
                              true);
        }
    }

    return make_tuple(
        install_deps_phase_one_request_deps(binary_dir, request_deps, command_line_cmake_args,
                                            command_line_configs, wsp, cmakex_cache),
        false);
}

idpo_recursion_result_t install_deps_phase_one_deps_script(
    string_par binary_dir,
    string_par deps_script_file,
    const vector<string>& global_cmake_args,
    const vector<config_name_t>& command_line_configs,
    deps_recursion_wsp_t& wsp,
    const cmakex_cache_t& cmakex_cache)
{
    // Create helper cmake project
    // Configure it again with specifying the build script as parameter
    // The background project executes the build script and
    // - records the add_pkg commands
    // - records the cmakex commands
    // Then the configure ends.
    // Process the recorded add_pkg commands which installs the requested dependency to the
    // install
    // directory.

    // create source dir

    log_info("Processing dependency script: %s", path_for_log(deps_script_file).c_str());
    HelperCmakeProject hcp(binary_dir);
    auto addpkgs_lines = hcp.run_deps_script(
        deps_script_file, wsp.clear_downloaded_include_files,
        wsp.requester_stack.empty() ? "_main_project" : wsp.requester_stack.back().c_str());

    vector<string> deps;

    // for each pkg:
    for (auto& addpkg_line : addpkgs_lines) {
        auto args = split(addpkg_line, '\t');
        auto req = pkg_request_from_args(args, command_line_configs);
        if (!wsp.requester_stack.empty())  // git tag override effective only in main project
            req.git_tag_override = false;
        if (!req.define_only)
            deps.emplace_back(req.name);
        insert_new_request_into_wsp(req, wsp);
    }
    if (g_verbose) {
        if (!wsp.requester_stack.empty()) {
            auto pkg_name = wsp.requester_stack.back();
            auto it = wsp.pkg_map.find(pkg_name);
            if (it == wsp.pkg_map.end())
                log_verbose("No dependencies for %s.", pkg_for_log(pkg_name).c_str());
            else
                log_verbose("Dependencies for %s (from script): [%s]",
                            pkg_for_log(pkg_name).c_str(), join(deps, ", ").c_str());
        } else {
            if (wsp.pkg_map.empty())
                log_verbose("No dependencies for the main project.");
            else
                log_verbose("Dependencies for the main project (from script): [%s]",
                            join(keys_of_map(wsp.pkg_map), ", ").c_str());
        }
    }
    return process_pkgs_to_process(binary_dir, global_cmake_args, command_line_configs, wsp,
                                   cmakex_cache, deps);
}

idpo_recursion_result_t run_deps_add_pkg(string_par pkg_name,
                                         string_par binary_dir,
                                         const vector<string>& command_line_cmake_args,
                                         const vector<config_name_t>& command_line_configs,
                                         deps_recursion_wsp_t& wsp,
                                         const cmakex_cache_t& cmakex_cache)
{
    LOG_DEBUG("run_deps_add_pkg(%s, ...)", pkg_for_log(pkg_name).c_str());

    const cmakex_config_t cfg(binary_dir);

    CHECK(wsp.pkg_map.count(pkg_name.str()) > 0);
    auto& pkg = wsp.pkg_map.at(pkg_name.str());

    if (linear_search(wsp.requester_stack, pkg_name)) {
        // report circular dependency error
        string s;
        for (auto& x : wsp.requester_stack) {
            if (!s.empty())
                s += " -> ";
            s += x;
        }
        s += "- > ";
        s += pkg_name;
        throwf("Circular dependency: %s", s.c_str());
    }

    // todo: get data from optional package registry here

    InstallDB installdb(binary_dir);

    // verify if this package is installed on one of the available prefix paths (from
    // CMAKE_PREFIX_PATH cmake and environment variables)
    // - it's an error if it's installed on multiple paths
    // - if it's installed on a prefix path we accept that installed package as it is
    auto prefix_paths = stable_unique(
        concat(cmakex_cache.cmakex_prefix_path_vector, cmakex_cache.env_cmakex_prefix_path_vector));

    do {  // one-shot scope
        vector<config_name_t> configs_on_prefix_path;
        if (!prefix_paths.empty())
            tie(pkg.found_on_prefix_path, configs_on_prefix_path) =
                installdb.quick_check_on_prefix_paths(pkg_name, prefix_paths);

        if (pkg.found_on_prefix_path.empty())
            break;

        vector<config_name_t> requested_configs = pkg.request.b.configs();
        CHECK(!requested_configs
                   .empty());  // even for name-only requests must contain the default configs

        // overwrite requested configs with the installed ones
        std::sort(BEGINEND(requested_configs));
        nosx::unique_trunc(requested_configs);
        std::sort(BEGINEND(configs_on_prefix_path));

        auto missing_configs = set_difference(requested_configs, configs_on_prefix_path);
        // in case the requested configs and the installed configs are different, we're
        // accepting the installed configs
        string cmsg;
        if (!missing_configs.empty()) {
            cmsg = stringf(
                ", accepting installed configuration%s (%s) instead of the requested one%s "
                "(%s)%s",
                configs_on_prefix_path.size() > 1 ? "s" : "",
                join(get_prefer_NoConfig(configs_on_prefix_path), ", ").c_str(),
                requested_configs.size() > 1 ? "s" : "",
                join(get_prefer_NoConfig(requested_configs), ", ").c_str(),
                pkg.request.b.using_default_configs() ? " (from the command line)" : "");
            pkg.request.b.update_configs(configs_on_prefix_path, false);
        } else
            cmsg =
                stringf(" (%s)", join(get_prefer_NoConfig(configs_on_prefix_path), ", ").c_str());

        log_info("Using %s from %s%s.", pkg_for_log(pkg_name).c_str(),
                 path_for_log(pkg.found_on_prefix_path).c_str(), cmsg.c_str());
        // in case the request is only a name, we're accepting the installed desc as request
    } while (false);  // one-shot scope

    struct per_config_data_t
    {
        bool initial_build;
    };

    std::map<config_name_t, per_config_data_t> pcd;

    {
        bool first_iteration = true;
        for (auto& c : pkg.request.b.configs()) {
            auto& pcd_c = pcd[c];
            auto& pkg_c = pkg.pcd[c];
            if (cmakex_cache.per_config_bin_dirs || first_iteration) {
                auto pkg_bin_dir_of_config =
                    cfg.pkg_binary_dir_of_config(pkg_name, c, cmakex_cache.per_config_bin_dirs);
                pcd_c.initial_build =
                    !fs::is_regular_file(pkg_bin_dir_of_config + "/CMakeCache.txt");

                auto& cmake_args_to_apply = pkg_c.cmake_args_to_apply;

                if (pcd_c.initial_build) {
                    auto pkg_cmake_args = normalize_cmake_args(pkg.request.b.cmake_args);
                    auto* cpp =
                        find_specific_cmake_arg_or_null("CMAKE_INSTALL_PREFIX", pkg_cmake_args);
                    CHECK(!cpp,
                          "Internal error: package CMAKE_ARGS's should not change "
                          "CMAKE_INSTALL_PREFIX: '%s'",
                          cpp->c_str());
                    cmake_args_to_apply = pkg_cmake_args;
                    cmake_args_to_apply.emplace_back("-DCMAKE_INSTALL_PREFIX=" +
                                                     cfg.deps_install_dir());
                }

                {
                    auto* cpp = find_specific_cmake_arg_or_null("CMAKE_INSTALL_PREFIX",
                                                                command_line_cmake_args);
                    CHECK(!cpp,
                          "Internal error: command-line CMAKE_ARGS's forwarded to a dependency "
                          "should not change CMAKE_INSTALL_PREFIX: '%s'",
                          cpp->c_str());
                }

                cmake_args_to_apply =
                    normalize_cmake_args(concat(cmake_args_to_apply, command_line_cmake_args));

                cmake_args_to_apply = make_sure_cmake_path_var_contains_path(
                    pkg_bin_dir_of_config, "CMAKE_MODULE_PATH", cfg.find_module_hijack_dir(),
                    cmake_args_to_apply);

                cmake_args_to_apply = make_sure_cmake_path_var_contains_path(
                    pkg_bin_dir_of_config, "CMAKE_PREFIX_PATH", cfg.deps_install_dir(),
                    cmake_args_to_apply);

                // get tentative per-config final_cmake_args from cmake cache tracker by
                // applying
                // these cmake_args onto the current tracked values
                auto cct = load_cmake_cache_tracker(pkg_bin_dir_of_config);
                cct.add_pending(cmake_args_to_apply);
                cct.confirm_pending();
                pkg_c.tentative_final_cmake_args.assign(cct.cached_cmake_args, cct.c_sha,
                                                        cct.cmake_toolchain_file_sha);
            } else {
                auto first_c = *pkg.request.b.configs().begin();
                pcd_c.initial_build = pcd[first_c].initial_build;
                pkg_c = pkg.pcd[first_c];
            }
        }
    }

    // if it's already installed we still need to process this:
    // - to enumerate all dependencies
    // - to check if only compatible installations are requested

    clone_helper_t clone_helper(binary_dir, pkg_name);
    auto& cloned = clone_helper.cloned;
    auto& cloned_sha = clone_helper.cloned_sha;

    if (cloned && wsp.update) {
        string clone_dir = cfg.pkg_clone_dir(pkg_name);
        int r = exec_git({"remote", "set-url", "origin", pkg.request.c.git_url.c_str()}, clone_dir,
                         nullptr, nullptr, log_git_command_on_error);
        if (r)
            exit(r);
        auto lsr = git_ls_remote(pkg.request.c.git_url);

        string target_git_branch, target_git_tag, target_git_sha;

        string gt = pkg.request.c.git_tag;

        // resolve unspecified or HEAD GIT_TAG
        if (gt.empty() || gt == "HEAD")
            gt = lsr.head_branch();

        if (lsr.branches.count(gt) > 0) {
            target_git_branch = gt;
            target_git_sha = lsr.branches.at(gt);
        } else if (lsr.tags.count(gt) > 0) {
            target_git_tag = gt;
            target_git_sha = lsr.tags.at(gt);
        } else {
            // gt must be an SHA
            if (!sha_like(gt)) {
                throwf(
                    "%s requests GIT_TAG '%s' but no remote branch or tag at \"%s\" found "
                    "with this name.",
                    pkg_for_log(pkg_name).c_str(), gt.c_str(), pkg.request.c.git_url.c_str());
            }
            target_git_sha = gt;
        }

        CHECK(!target_git_sha.empty());

        do {  // scope for break
            if (cloned_sha == target_git_sha) {
                // no need to update, we're there
                log_verbose("%s is up-to-date with remote.", pkg_for_log(pkg_name).c_str());
                break;
            }

            LOG_INFO("Updating %s from remote.", pkg_for_log(pkg_name).c_str());

            // we should update, check for local changes
            bool local_changes = false;
            if (get<0>(clone_helper.clone_status) == pkg_clone_dir_git_local_changes) {
                if (wsp.update_can_reset) {
                    // 'local_changes' implies that we can git-reset (update mode is force)
                    local_changes = true;
                } else if (wsp.update_stop_on_error)
                    throwf(
                        "Can't update %s because of local changes. Stash or commit your changes or "
                        "use '--update=[if-clean|if-very-clean|force]",
                        pkg_for_log(pkg_name).c_str());
                else {
                    log_warn("Not updating %s because of local changes.",
                             pkg_for_log(pkg_name).c_str());
                    break;
                }
            }

            // check if we need to leave branch
            string current_branch = git_current_branch_or_HEAD(clone_dir);  // HEAD or some branch
            bool leave_branch = current_branch != "HEAD" &&
                                (target_git_branch.empty() || target_git_branch != current_branch);
            if (leave_branch && !wsp.update_can_leave_branch) {
                if (wsp.update_stop_on_error)
                    throwf(
                        "Can't update %s because the current branch should be changed and the "
                        "current update mode doesn't allow that. %s manually or use "
                        "'--update=[if-clean|if-very-clean|all-clean]'",
                        pkg_for_log(pkg_name).c_str(),
                        target_git_branch.empty()
                            ? "Detach"
                            : stringf("Checkout '%s'", target_git_branch.c_str()).c_str());
                else {
                    log_warn(
                        "Not updating %s because the current branch should be changed and the "
                        "current update mode doesn't allow that.",
                        pkg_for_log(pkg_name).c_str());
                    break;
                }
            }

            // try if the target SHA can be found locally
            bool sha_is_valid = git_is_existing_commit(clone_dir, target_git_sha);
            if (!sha_is_valid) {
                // try to fetch
                exec_git({"fetch"}, clone_dir, nullptr, nullptr, log_git_command_always);
                sha_is_valid = git_is_existing_commit(clone_dir, gt);
                if (!sha_is_valid) {
                    string s;
                    if (!target_git_branch.empty())
                        s = stringf("branch '%s' resolved to %s", gt.c_str(),
                                    target_git_sha.c_str());
                    else if (!target_git_tag.empty())
                        s = stringf("tag '%s' resolved to %s", gt.c_str(), target_git_sha.c_str());
                    else
                        s = stringf("SHA is %s", gt.c_str());
                    throwf(
                        "Can't update %s because the requested %s but it's an unknown reference "
                        "even after 'git fetch'",
                        pkg_for_log(pkg_name).c_str(), s.c_str());
                }
            }

            if (!target_git_branch.empty()) {
                // updating to branch
                auto is_local_branch =
                    git_is_existing_commit(clone_dir, "refs/heads/" + target_git_branch);
                if (is_local_branch) {
                    // if it's a local branch
                    if (current_branch != target_git_branch)
                        // if it's not the current local branch
                        if (local_changes) {
                            LOG_WARN("%s: abandoning local changes in index or workspace.",
                                     pkg_for_log(pkg_name).c_str());
                            git_checkout({"-f", target_git_branch}, clone_dir);
                        } else {
                            git_checkout({target_git_branch}, clone_dir);
                        }
                } else {
                    // create new local branch
                    string sw;
                    if (local_changes) {
                        LOG_WARN("%s: abandoning local changes in index or workspace.",
                                 pkg_for_log(pkg_name).c_str());
                        sw = "-fb";
                    } else
                        sw = "-b";
                    git_checkout({sw, target_git_branch, "--track", "origin/" + target_git_branch},
                                 clone_dir);
                }
                int r =
                    exec_git({"merge-base", "--is-ancestor", "HEAD", "origin/" + target_git_branch},
                             clone_dir, nullptr, nullptr, log_git_command_never);
                if (!r) {
                    // no fast forward
                    if (wsp.update_can_reset) {
                        LOG_WARN(
                            "%s: abandoning commits on branch '%s' because it can't be updated "
                            "with fast-forward merge.",
                            pkg_for_log(pkg_name).c_str(), target_git_branch.c_str());
                        r = exec_git({"reset", "--hard", "origin/" + target_git_branch}, clone_dir,
                                     nullptr, nullptr, log_git_command_always);
                    } else
                        throwf(
                            "Can't update %s because the requested branch '%s' can't be reached "
                            "via a fast-forward merge from the current local HEAD '%s'. Update "
                            "manually or use '--update=force'.",
                            pkg_for_log(pkg_name).c_str(), target_git_branch.c_str(),
                            cloned_sha.c_str());
                } else {
                    r = exec_git({"merge", "--ff-only", "origin/" + target_git_branch}, clone_dir,
                                 nullptr, nullptr, log_git_command_always);
                }
                if (r)
                    exit(r);
            } else {
                vector<string> args;
                if (local_changes) {
                    LOG_WARN("%s: abandoning local changes in index or workspace.",
                             pkg_for_log(pkg_name).c_str());
                    args.emplace_back("-f");
                }
                if (!target_git_tag.empty())
                    args.emplace_back("refs/tags/" + target_git_tag);
                else {
                    CHECK(!target_git_sha.empty());
                    args.emplace_back(target_git_sha);
                }
                int r = git_checkout(args, clone_dir);
                if (r)
                    exit(r);
            }
            clone_helper.update_clone_status_vars();
        } while (false);  // scope for break
    }                     // if cloned and should update

    auto clone_this_at_sha = [&pkg, &clone_helper](string sha) {
        auto prc = pkg.request.c;
        if (!sha.empty())
            prc.git_tag = sha;
        clone_helper.clone(prc, pkg.request.git_shallow);
        pkg.just_cloned = true;
    };

    auto clone_this = [&clone_this_at_sha]() { clone_this_at_sha({}); };

    string pkg_source_dir = cfg.pkg_clone_dir(pkg_name);
    if (!pkg.request.b.source_dir.empty())
        pkg_source_dir += "/" + pkg.request.b.source_dir;

    if (pkg.request.name_only() && pkg.found_on_prefix_path.empty())
        throwf("No definition found for package %s", pkg_for_log(pkg_name).c_str());

    auto per_config_final_cmake_args = [&pkg]() {
        std::map<config_name_t, final_cmake_args_t> r;
        for (auto& c : pkg.request.b.configs())
            r[c] = pkg.pcd.at(c).tentative_final_cmake_args;
        return r;
    };

    // determine installed status
    auto installed_result = installdb.evaluate_pkg_request_build_pars(
        pkg_name, pkg.request.b.source_dir, per_config_final_cmake_args(),
        pkg.found_on_prefix_path);
    CHECK(installed_result.size() == pkg.request.b.configs().size());

    // if it's not found on a prefix path
    if (pkg.found_on_prefix_path.empty()) {
// Because of the install-prebuilt-package-from-server feature is not yet implemented
// I comment out this branch. Effect: always clone the dependency if it's not found
// on a prefix path.
// Even when that feature will be implemented, it has to be investigated what's the
// correct way of this.
#if 0
        // if any of the requested configs is not satisfied we know we need the clone right now
        // this is partly a shortcut, partly later (when traversing the dependencies) we
        // exploit the fact that this package is either installed (say, from server) or cloned
        bool one_config_is_not_satisfied = false;
        for (auto& c : pkg.request.b.configs()) {
            if (installed_result.at(c).status != pkg_request_satisfied) {
                one_config_is_not_satisfied = true;
                break;
            }
        }
        if (one_config_is_not_satisfied && !cloned)
            clone_this();
#else
        if (!cloned)
            clone_this();
#endif
    } else {
        // at this point, if we found it on prefix path, we've already overwritten the requested
        // config with the installed one. Verify this.
        auto rcs = pkg.request.b.configs();
        std::sort(BEGINEND(rcs));
        CHECK(rcs == keys_of_map(installed_result));
        CHECK(rcs == keys_of_map(pkg.pcd));

        // if it's found on a prefix_path it must be good as it is, we can't change those
        // also, it must not be cloned
        if (cloned) {
            throwf(
                "%s found on the prefix path %s but and it's also checked out in %s. Remove "
                "either "
                "from the prefix path or from the directory.",
                pkg_for_log(pkg_name).c_str(), path_for_log(pkg.found_on_prefix_path).c_str(),
                path_for_log(cfg.pkg_clone_dir(pkg_name)).c_str());
        }
        // if it's name-only then we accept it always
        if (pkg.request.name_only()) {
            // overwrite with actual installed descs
            // the installed descs must be uniform, that is, same settings for each installed
            // config

            auto& first_desc = installed_result.begin()->second.installed_config_desc;
            const char* different_option = nullptr;
            for (auto& kv : installed_result) {
                auto& desc = kv.second.installed_config_desc;
                CHECK(desc.pkg_name == pkg_name &&
                      desc.config == kv.first);  // the installdb should have verified this

                pkg.request.c.git_url = desc.git_url;
                if (desc.git_url != first_desc.git_url) {
                    different_option = "GIT_URL";
                    break;
                }
                pkg.request.c.git_tag = desc.git_sha;
                if (desc.git_sha != first_desc.git_sha) {
                    different_option = "GIT_SHA";
                    break;
                }

                pkg.request.b.source_dir = desc.source_dir;
                if (desc.source_dir != first_desc.source_dir) {
                    different_option = "SOURCE_DIR";
                    break;
                }

                auto& c = kv.first;
                pkg.pcd[c].tentative_final_cmake_args = desc.final_cmake_args;

                auto ds = keys_of_map(desc.deps_shas);
                pkg.request.depends.insert(BEGINEND(ds));

                if (ds != keys_of_map(first_desc.deps_shas)) {
                    different_option = "DEPENDS";
                    break;
                }
            }

            if (different_option) {
                log_warn(
                    "Package %s found on the prefix path %s and its configurations (%s) have "
                    "been built with different build options. Offending option: %s.",
                    pkg_for_log(pkg_name).c_str(), path_for_log(pkg.found_on_prefix_path).c_str(),
                    join(get_prefer_NoConfig(keys_of_map(installed_result)), ", ").c_str(),
                    different_option);
            }

            // update installed_result
            installed_result = installdb.evaluate_pkg_request_build_pars(
                pkg_name, pkg.request.b.source_dir, per_config_final_cmake_args(),
                pkg.found_on_prefix_path);

            for (auto& kv : installed_result)
                CHECK(kv.second.status == pkg_request_satisfied);

        } else {
            for (auto& c : pkg.request.b.configs()) {
                auto& itc = installed_result.at(c);
                if (itc.status == pkg_request_different) {
                    log_warn(
                        "Package %s found on the prefix path %s and some build options differ"
                        " from the current ones. The offending CMAKE_ARGS: [%s]",
                        pkg_for_log(pkg_name).c_str(),
                        path_for_log(pkg.found_on_prefix_path).c_str(),
                        itc.incompatible_cmake_args_any.c_str());
                } else if (itc.status != pkg_request_different_but_satisfied) {
                    // it can't be not installed
                    throwf(
                        "Internal error: itc.status should be pkg_request_different_but_satisfied "
                        "but it's: %d, incompatible args for local build: %s, incompatible args "
                        "for any build: %s",
                        (int)itc.status, itc.incompatible_cmake_args_local.c_str(),
                        itc.incompatible_cmake_args_any.c_str());
                }
            }
        }
    }

    std::map<config_name_t, vector<string>> build_reasons;

    idpo_recursion_result_t rr;

    LOG_TRACE("%s", pkg_for_log(pkg_name).c_str());
    for (auto& kv : installed_result) {
        LOG_TRACE("Installed: %s -> %s", kv.first.get_prefer_NoConfig().c_str(),
                  to_string(kv.second.status).c_str());
    }

    // if first attempt finds reason to build and this package is not cloned, a second attempt
    // will follow after a clone
    for (int attempts = 1;; ++attempts) {
        LOG_TRACE("Attempt#%d, %s", attempts, cloned ? "cloned" : "not cloned");

        CHECK(attempts <= 2);  // before second iteration there will be a cloning so the second
                               // iteration must finish
        build_reasons.clear();
        rr.clear();
        bool for_clone_use_installed_sha = false;
        bool restore_wsp_before_second_attempt = false;

        using wsp_t = std::remove_reference<decltype(wsp)>::type;
        wsp_t saved_wsp;

        // process_deps
        if (cloned) {
            wsp.requester_stack.emplace_back(pkg_name);

            tie(rr, pkg.dependencies_from_script) = install_deps_phase_one(
                binary_dir, pkg_source_dir, to_vector(pkg.request.depends), command_line_cmake_args,
                command_line_configs, wsp, cmakex_cache, "");

            CHECK(wsp.requester_stack.back() == pkg_name);
            wsp.requester_stack.pop_back();
            pkg.request.depends.insert(BEGINEND(rr.pkgs_encountered));
        } else {
            if (!pkg.found_on_prefix_path.empty()) {
                rr = install_deps_phase_one_request_deps(
                    binary_dir, keys_of_set(pkg.request.depends), command_line_cmake_args,
                    command_line_configs, wsp, cmakex_cache);
            } else {
                // We should not come here because if it's not on prefix path it should have been
                // cloned.
                // Anyway, give a better error message
                LOG_FATAL(
                    "Internal error: %s is neither cloned nor found on a prefix path. Try to "
                    "remove the deps-install directory.",
                    pkg_for_log(pkg_name).c_str());

                // enumerate dependencies from description of installed package
                LOG_FATAL("This branch is not implemented");

                // todo implement this branch:
                // this package is installed but not cloned. So this has been remote built and
                // installed as headers+binary
                // the description that came with the binary should contain the detailed
                // descriptions (requests) of its all dependencies. It's like a deps.cmake file
                // 'materialized' that is, processed into a similar file we process the
                // deps.cmake
                // and also git_tags resolved to concrete SHAs.
                // The dependencies will be either built locally or downloaded based on that
                // description

                saved_wsp = wsp;

                restore_wsp_before_second_attempt = true;
                for_clone_use_installed_sha = true;

#if 0
            rr = install_deps_phase_one_remote_build(pkg_name, command_line_cmake_args, command_line_cmake_args, wsp,
                                                     cmakex_cache);
#endif
            }
        }  // else branch of if cloned

        // if it has been found on a prefix path, we don't need a build reason, we're done, exit the
        // attempts loop
        if (!pkg.found_on_prefix_path.empty())
            break;

        // if we're building a dependency but otherwise we're satisfied, the primary reason is that
        // the dependency will be rebuilt
        if (rr.building_some_pkg) {
            for (auto& c : pkg.request.b.configs()) {
                auto status = installed_result.at(c).status;
                if (status == pkg_request_satisfied)
                    build_reasons[c].assign(1, "a dependency has been rebuilt");
                // if not satisfied we expect the code below find another reason
            }
        }

        for (auto& kv : installed_result) {
            const auto& config = kv.first;
            if (build_reasons.count(config) > 0)
                break;  // need only one reason
            const auto& current_install_details = kv.second;
            const auto& current_install_desc = current_install_details.installed_config_desc;
            switch (kv.second.status) {
                case pkg_request_not_installed:
                    build_reasons[config] = {"initial build"};
                    break;
                case pkg_request_different_but_satisfied:
                case pkg_request_different: {
                    auto& br = build_reasons[config];
                    br = {stringf("build options changed")};
                    br.emplace_back(stringf(
                        "CMAKE_ARGS of the currently installed build: %s",
                        join(normalize_cmake_args(current_install_desc.final_cmake_args.args), " ")
                            .c_str()));
                    br.emplace_back(stringf(
                        "Requested CMAKE_ARGS: %s",
                        join(pkg.pcd.at(config).tentative_final_cmake_args.args, " ").c_str()));
                    br.emplace_back(
                        stringf("Incompatible CMAKE_ARGS: %s",
                                current_install_details.incompatible_cmake_args_local.c_str()));
                } break;
                case pkg_request_satisfied: {
                    // test for new commits or uncommited changes
                    if (cloned) {
                        if (cloned_sha == k_sha_uncommitted) {
                            build_reasons[config] = {"workspace contains uncommited changes"};
                            break;
                        } else if (cloned_sha != current_install_desc.git_sha) {
                            build_reasons[config] = {
                                "workspace is at a new commit",
                                stringf("Currently installed from commit: %s",
                                        current_install_desc.git_sha.c_str()),
                                stringf("Current commit in workspace: %s", cloned_sha.c_str())};
                            break;
                        }
                    }
                    // examine each dependency
                    // collect all dependencies

                    auto deps =
                        concat(keys_of_map(current_install_desc.deps_shas), pkg.request.depends);
                    std::sort(BEGINEND(deps));
                    nosx::unique_trunc(deps);

                    for (auto& d : deps) {
                        // we can stop at first reason to build
                        if (build_reasons.count(config) > 0)
                            break;
                        if (pkg.request.depends.count(d) == 0) {
                            // this dependency is not requested now (but was needed when this
                            // package was installed)
                            build_reasons[config] = {
                                stringf("dependency '%s' is not needed any more", d.c_str())};
                            break;
                        }

                        // this dependency is requested now

                        // the configs of this dependency that was needed when this package was
                        // build
                        // and installed
                        auto it_previnst_dep_configs = current_install_desc.deps_shas.find(d);
                        if (it_previnst_dep_configs == current_install_desc.deps_shas.end()) {
                            // this dep is requested but was not present at the previous
                            // installation
                            build_reasons[config] = {stringf("new dependency: '%s'", d.c_str())};
                            break;
                        }

                        // this dep is requested, was present at the previous installation
                        // check if it's installed now
                        auto dep_current_install =
                            installdb.try_get_installed_pkg_all_configs(d, prefix_paths);
                        if (dep_current_install.empty()) {
                            // it's not installed now but it's requested. This should have
                            // triggered
                            // building that dependency and then building this package in turn.
                            // Since we're here it did not happened so this is an internal
                            // error.
                            LOG_FATAL(
                                "Package '%s', dependency '%s' is not installed but requested "
                                "but "
                                "still it didn't trigger building this package.",
                                pkg_name.c_str(), d.c_str());
                            break;
                        }

                        // this dependency is installed now
                        bool this_dep_config_was_installed =
                            it_previnst_dep_configs->second.count(config) > 0;
                        bool this_dep_config_is_installed =
                            dep_current_install.config_descs.count(config) > 0;

                        if (this_dep_config_is_installed != this_dep_config_was_installed) {
                            build_reasons[config] = {
                                stringf("dependency '%s' / config '%s' has been changed since "
                                        "last install",
                                        d.c_str(), config.get_prefer_NoConfig().c_str())};
                            break;
                        }

                        if (this_dep_config_was_installed) {
                            // this config of the dependency was installed and is
                            // installed
                            // check if it has been changed
                            string dep_current_sha =
                                calc_sha(dep_current_install.config_descs.at(config));
                            string dep_previnst_sha = it_previnst_dep_configs->second.at(config);
                            if (dep_current_sha != dep_previnst_sha) {
                                build_reasons[config] = {
                                    stringf("dependency '%s' has been changed since "
                                            "last install",
                                            d.c_str())};
                                break;
                            }
                        } else {
                            // this dep config was not installed and is not installed
                            // now (but other configs of this dependency were and are)
                            // that means, all those configs must be unchanged to prevent
                            // build
                            auto& map1 = dep_current_install.config_descs;
                            auto& map2 = it_previnst_dep_configs->second;
                            vector<config_name_t> configs =
                                concat(keys_of_map(map1), keys_of_map(map2));
                            std::sort(BEGINEND(configs));
                            nosx::unique_trunc(configs);
                            config_name_t* changed_config = nullptr;
                            for (auto& c : configs) {
                                auto it1 = map1.find(c);
                                auto it2 = map2.find(c);
                                if ((it1 == map1.end()) != (it2 == map2.end())) {
                                    changed_config = &c;
                                    break;
                                }
                                CHECK(it1 != map1.end() && it2 != map2.end());
                                if (calc_sha(it1->second) != it2->second) {
                                    changed_config = &c;
                                    break;
                                }
                            }
                            if (changed_config) {
                                build_reasons[config] = {
                                    stringf("dependency '%s' / config '%s' has been changed "
                                            "since last install",
                                            d.c_str(), config.get_prefer_NoConfig().c_str())};
                                break;
                            }
                        }
                    }
                } break;
                case invalid_status:
                default:
                    CHECK(false);
            }  // switch on the evaluation result between request config and installed config
        }      // for all requested configs

        if (cloned && wsp.force_build) {
            for (auto& c : pkg.request.b.configs()) {
                if (build_reasons.count(c) == 0)
                    build_reasons[c].assign(1, "'--force-build' specified.");
            }
        }

        if (build_reasons.empty() || cloned)
            break;
        if (restore_wsp_before_second_attempt)
            wsp = move(saved_wsp);
        if (for_clone_use_installed_sha) {
            // if it's installed at some commit, clone that commit
            string installed_sha;
            for (auto& kv : installed_result) {
                if (kv.second.status == pkg_request_not_installed)
                    continue;
                string cs = kv.second.installed_config_desc.git_sha;
                if (!sha_like(cs))
                    throwf(
                        "About to clone the already installed %s (need to be rebuilt). Can't "
                        "decide which commit to clone since it has been installed from an "
                        "invalid commit. Probably the workspace contaniner local changes. The "
                        "offending SHA: %s",
                        pkg_for_log(pkg_name).c_str(), cs.c_str());
                if (installed_sha.empty())
                    installed_sha = cs;
                else if (installed_sha != cs) {
                    throwf(
                        "About to clone the already installed %s (needs to be rebuilt). Can't "
                        "decide which commit to clone since different configurations has been "
                        "installed from different commits. Two offending commit SHAs: %s and "
                        "%s",
                        pkg_for_log(pkg_name).c_str(), cloned_sha.c_str(), cs.c_str());
                }
            }
            CHECK(!installed_sha.empty());  // it can't be empty since if it's not installed
            // then we should not get here
            clone_this_at_sha(installed_sha);
        } else
            clone_this();
    }  // for attempts

    string clone_dir = cfg.pkg_clone_dir(pkg_name);

    // for now the first request will determine which commit to build
    // and second, different request will be an error
    // If that's not good, relax and implement some heuristics

    if (build_reasons.empty()) {
        vector<string> v;
        for (auto kv : installed_result)
            append_inplace(v, kv.second.installed_config_desc.hijack_modules_needed);
        std::sort(BEGINEND(v));
        nosx::unique_trunc(v);
        for (auto& x : v)
            write_hijack_module(x, binary_dir);
    } else {
        CHECK(cloned);
        wsp.build_order.push_back(pkg_name.str());
        pkg.resolved_git_tag = cloned_sha;
        for (auto& kv : build_reasons)
            pkg.pcd.at(kv.first).build_reasons = kv.second;
        rr.building_some_pkg = true;
        pkg.building_now = true;
    }
    rr.add_pkg(pkg_name);
    return rr;
}
}  // namespace cmakex
