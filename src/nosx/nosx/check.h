#pragma once

#include <cassert>

#define CHECK(x, ...) assert(x&&##__VA_ARGS__)
