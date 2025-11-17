#pragma once

#include <type_traits>

template <typename... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

// empty class to pass type tags
template <typename>
struct Proxy {};

template <bool Const, typename T>
using MaybeConst = std::conditional_t<Const, const T, T>;
