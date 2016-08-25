#ifndef CMAKEX_TYPES_29034023
#define CMAKEX_TYPES_29034023

#include <map>

#include "misc_utils.h"
#include "using-decls.h"

namespace cmakex {

static const char* k_deps_script_filename = "deps.cmake";
static const char* k_log_extension = ".log";
static const char* k_cmakex_cache_filename = "cmakex_cache.json";
static const char* k_cmake_cache_tracker_filename = "cmakex_cache_tracker.json";
static const char* k_cmake_cache_tracker_ref_filename = "cmakex_cache_tracker_ref.json";

enum git_tag_kind_t
{
    // order is important
    git_tag_not_checked,
    git_tag_is_not_sha,    // it's not sha-like
    git_tag_could_be_sha,  // it's sha-like but not checked
    git_tag_must_be_sha,   // it's sha-like and server returned not found
    git_tag_is_sha,        // we initialized it with a known sha
};

bool sha_like(string_par x);

inline git_tag_kind_t initial_git_tag_kind(string git_tag)
{
    return sha_like(git_tag) ? git_tag_could_be_sha : git_tag_is_not_sha;
}

struct pkg_clone_pars_t
{
    string git_url;
    string git_tag;
};

struct config_name_t
{
    config_name_t() = default;  // needed because cereal needs it

    config_name_t(string_par s) : value(s.c_str()) { normalize(); }
    const string& get_prefer_empty() const { return value; }
    const string& get_prefer_NoConfig() const
    {
        static const string NoConfig{"NoConfig"};
        return value.empty() ? NoConfig : value;
    }
    string get_lowercase_prefer_noconfig() const
    {
        static const string noconfig{"noconfig"};
        return value.empty() ? noconfig : cmakex::tolower(value);
    }

    bool operator==(const config_name_t& x) const { return value == x.value; }
    bool operator<(const config_name_t& x) const { return value < x.value; }
private:
    void normalize();

    string value;
};

vector<string> get_prefer_NoConfig(const vector<config_name_t>& x);

struct pkg_build_pars_t
{
    string source_dir;  // (relative) directory containing CMakeLists.txt
    // cmake_args:
    // - never contains source and binary dir flags or paths
    // - contains CMAKE_INSTALL_PREFIX, CMAKE_PREFIX_PATH, CMAKE_MODULE_PATH only when describes
    //   command line
    // - contains global args when used as package request / installed package description
    vector<string> cmake_args;      // all cmake args including global ones
    vector<config_name_t> configs;  // Debug, Release, etc..
};

// dependency_name -> (config -> dependency SHA)
using deps_shas_t = std::map<string, std::map<config_name_t, string>>;

struct pkg_desc_t
{
    string name;
    pkg_clone_pars_t c;
    pkg_build_pars_t b;
    deps_shas_t deps_shas;  // sha's of dependencies at the time of the build
};

struct installed_config_desc_t
{
    static installed_config_desc_t uninitialized_installed_config_desc()
    {
        return installed_config_desc_t();
    }
    installed_config_desc_t(string_par pkg_name, config_name_t config)
        : pkg_name(pkg_name.c_str()), config(config)
    {
    }

    string pkg_name;
    config_name_t config;
    pkg_clone_pars_t c;
    struct
    {
        string source_dir;          // (relative) directory containing CMakeLists.txt
        vector<string> cmake_args;  // all cmake args including global ones
    } b;
    deps_shas_t deps_shas;  // sha's of dependencies at the time of the build
private:
    installed_config_desc_t() = default;
};

struct installed_pkg_configs_t
{
    bool empty() const { return config_descs.empty(); }
    string sha() const;  // of this structure

    std::map<config_name_t, installed_config_desc_t> config_descs;
};

/*struct pkg_files_t {
    struct file_item_t {
        string path; // relative to install prefix
        string sha;

        static bool less_path(const pkg_files_t::file_item_t& x, const pkg_files_t::file_item_t& y)
        {
            return x.path < y.path;
        }
        static bool equal_path(const pkg_files_t::file_item_t& x, const pkg_files_t::file_item_t& y)
        {
            return x.path == y.path;
        }
    };
    vector<file_item_t> files;
};
*/

struct pkg_request_t : pkg_desc_t
{
    bool git_shallow = true;  // if false, clone only the requested branch at depth=1
};

enum deps_mode_t
{
    dm_invalid,
    dm_main_only,
    dm_deps_only,
    dm_deps_and_main
};

struct base_command_line_args_cmake_mode_t
{
    bool flag_c = false;
    bool flag_b = false;
    bool flag_t = false;
    vector<string> build_args;        //--clean-first and --use-stderr
    vector<string> native_tool_args;  // args after "--"
    vector<string> build_targets;
    vector<string> configs;  // empty if no config has been specified. Specifying -DCMAKE_BUILD_TYPE
    // does not count
    vector<string> cmake_args;  // two-word args like -D X will be joined into one: -DX
    deps_mode_t deps_mode = dm_main_only;
};

struct command_line_args_cmake_mode_t : base_command_line_args_cmake_mode_t
{
    vector<string> free_args;
    string arg_H;
    string arg_B;
};

struct cmakex_cache_t
{
    bool valid = false;
    string home_directory;
    bool multiconfig_generator = false;
    bool per_config_bin_dirs = false;  // this is an effective value, not the user setting: if the
    // user setting is yes but it's a multiconfig-generator, then
    // this value will be false
};

bool operator==(const cmakex_cache_t& x, const cmakex_cache_t& y);

inline bool operator!=(const cmakex_cache_t& x, const cmakex_cache_t& y)
{
    return !(x == y);
}

struct processed_command_line_args_cmake_mode_t : base_command_line_args_cmake_mode_t
{
    string source_dir;  // can be relative
    string binary_dir;  // can be relative
                        /*
                                                bool binary_dir_valid = false;
                                                bool config_args_besides_binary_dir = false;
                                                vector<string> add_pkgs;
                                            */
};

enum pkg_request_status_against_installed_config_t
{
    invalid_status,
    pkg_request_satisfied,      // the request is satisfied by what is installed
    pkg_request_not_installed,  // the requested package is not installed at all
    pkg_request_not_compatible  // the request package is installed with incompatible build
    // options
};

struct pkg_request_details_against_installed_config_t
{
    pkg_request_details_against_installed_config_t(
        const installed_config_desc_t& installed_config_desc)
        : installed_config_desc(installed_config_desc)
    {
    }
    pkg_request_status_against_installed_config_t status = invalid_status;
    string incompatible_cmake_args;
    installed_config_desc_t installed_config_desc;
};

// key is the config
using pkg_request_details_against_installed_t =
    std::map<config_name_t, pkg_request_details_against_installed_config_t>;
}

#endif
