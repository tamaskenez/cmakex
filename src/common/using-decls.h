#ifndef USING_DECLS_092374029347
#define USING_DECLS_092374029347

#include <chrono>
#include <exception>
#include <initializer_list>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <nosx/array_view.h>
#include <nosx/log.h>
#include <nosx/maybe.h>
#include <nosx/string_par.h>
#include <nosx/stringf.h>

namespace cmakex {
using std::exception;
using std::unique_ptr;
using std::string;
using std::vector;
using std::initializer_list;
using std::move;
using std::chrono::system_clock;
using std::chrono::high_resolution_clock;
using dur_sec = std::chrono::duration<double>;
using std::tuple;
using std::runtime_error;
using std::pair;
using std::get;
using std::tie;
using std::make_tuple;
using pair_ss = std::pair<string, string>;

namespace sx = adasworks::sx;
namespace log = adasworks::log;

using sx::stringf;
using sx::string_par;
using sx::array_view;
using sx::maybe;
using sx::nothing;
using sx::just;
using sx::in_place;
}

#endif
