#ifndef USING_DECLS_092374029347
#define USING_DECLS_092374029347

#include <chrono>
#include <exception>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <nosx/log.h>
#include <nosx/string_par.h>
#include <nosx/stringf.h>

namespace cmakex {
using std::chrono::high_resolution_clock;
using std::chrono::system_clock;
using std::exception;
using std::initializer_list;
using std::move;
using std::string;
using std::unique_ptr;
using std::vector;
using dur_sec = std::chrono::duration<double>;
using std::get;
using std::make_tuple;
using std::pair;
using std::runtime_error;
using std::tie;
using std::tuple;
using pair_ss = std::pair<string, string>;

using nosx::string_par;
using nosx::stringf;

template <class T>
using array_view = std::basic_string_view<T>;

template <class T>
using maybe = std::optional<T>;

using std::nullopt;

template <class T>
constexpr std::optional<std::decay_t<T>> just(T&& value)
{
    return std::make_optional(std::forward<T>(value));
}

}  // namespace cmakex

#endif
