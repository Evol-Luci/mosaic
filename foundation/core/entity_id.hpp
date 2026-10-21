#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace mosaic::foundation
{

// A 64-bit generational handle: (generation << 32) | index.
// Index occupies the low 32 bits; generation the high 32. Generations start at
// 1, so a zero value is the reserved null/invalid handle.
class EntityID
{
public:
    constexpr EntityID() noexcept : value_(0) {}

    constexpr EntityID(std::uint32_t index, std::uint32_t generation) noexcept
        : value_((static_cast<std::uint64_t>(generation) << 32) | index) {}

    constexpr explicit EntityID(std::uint64_t raw) noexcept : value_(raw) {}

    constexpr std::uint32_t index() const noexcept
    {
        return static_cast<std::uint32_t>(value_ & 0xFFFFFFFFu);
    }

    constexpr std::uint32_t generation() const noexcept
    {
        return static_cast<std::uint32_t>(value_ >> 32);
    }

    constexpr std::uint64_t value() const noexcept { return value_; }

    constexpr explicit operator bool() const noexcept { return value_ != 0; }

    friend constexpr bool operator==(EntityID a, EntityID b) noexcept
    {
        return a.value_ == b.value_;
    }

    friend constexpr bool operator!=(EntityID a, EntityID b) noexcept
    {
        return a.value_ != b.value_;
    }

private:
    std::uint64_t value_;
};

} // namespace mosaic::foundation

template <>
struct std::hash<mosaic::foundation::EntityID>
{
    std::size_t operator()(mosaic::foundation::EntityID id) const noexcept
    {
        return std::hash<std::uint64_t>{}(id.value());
    }
};
inline constexpr int ccprobe_eid = 1;
