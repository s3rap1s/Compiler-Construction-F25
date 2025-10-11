#pragma once

template <typename... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

// empty class to pass type tags
template <typename>
struct Proxy {};
