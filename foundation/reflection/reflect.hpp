#pragma once

#include <string_view>

namespace mosaic::foundation::reflection
{

// Archetype visitor used only to detect whether a type opts into reflection.
struct ReflectProbe
{
    template <class M> void member(std::string_view, M&) {}
};

// A type is Reflectable when a `reflect(visitor, value)` overload is found by
// argument-dependent lookup. Each reflected type provides that free function in
// its own namespace.
template <class T>
concept Reflectable = requires(ReflectProbe& p, T& t) { reflect(p, t); };

} // namespace mosaic::foundation::reflection
