#pragma once

#include <algorithm>

namespace nosx {
template <class Container>
void unique_trunc(Container& c)
{
    c.erase(std::unique(c.begin(), c.end()), c.end());
}

template <class Container, class BinaryPredicate>
void unique_trunc(Container& c, BinaryPredicate p)
{
    c.erase(std::unique(c.begin(), c.end(), p), c.end());
}
}  // namespace nosx
