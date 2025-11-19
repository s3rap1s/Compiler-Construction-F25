#pragma once

#include <type_traits>
#include <variant>

template <typename... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

// empty class to pass type tags
template <typename>
struct Proxy {};

template <bool Const, typename T>
using MaybeConst = std::conditional_t<Const, const T, T>;

template <typename T, typename V, std::size_t I>
struct ElementInVariantCheck : std::bool_constant<std::is_same_v<T, std::variant_alternative_t<I, V>> ||
                                                  ElementInVariantCheck<T, V, I - 1>::value> {};

template <typename T, typename V>
struct ElementInVariantCheck<T, V, 0> : std::bool_constant<std::is_same_v<T, std::variant_alternative_t<0, V>>> {};

template <typename T, typename V>
concept IsPartOfVariant = ElementInVariantCheck<T, V, std::variant_size_v<V> - 1>::value;

struct Unit {};
