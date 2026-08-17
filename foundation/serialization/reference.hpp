#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

#include "foundation/core/entity_id.hpp"

namespace mosaic::foundation::serialization
{

// Error type for all (de)serialization failures: parse errors, type mismatches,
// malformed ids. Foundation targets C++20, so failures surface as exceptions
// rather than std::expected (C++23).
class SerializationError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// 128-bit UUID: the globally stable cross-reference identity for assets
// (RFC 0001, Identity types).
class AssetID
{
public:
    constexpr AssetID() noexcept : bytes_{} {}
    explicit constexpr AssetID(std::array<std::uint8_t, 16> bytes) noexcept : bytes_(bytes) {}

    static AssetID from_string(const std::string& text);
    std::string to_string() const;

    bool is_nil() const noexcept
    {
        for (std::uint8_t b : bytes_)
        {
            if (b != 0)
            {
                return false;
            }
        }
        return true;
    }

    const std::array<std::uint8_t, 16>& bytes() const noexcept { return bytes_; }

    friend bool operator==(const AssetID& a, const AssetID& b) noexcept { return a.bytes_ == b.bytes_; }
    friend bool operator!=(const AssetID& a, const AssetID& b) noexcept { return !(a == b); }

private:
    std::array<std::uint8_t, 16> bytes_;
};

inline std::string AssetID::to_string() const
{
    static const char* const kHex = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < 16; ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10)
        {
            out.push_back('-');
        }
        out.push_back(kHex[bytes_[i] >> 4]);
        out.push_back(kHex[bytes_[i] & 0x0F]);
    }
    return out;
}

inline AssetID AssetID::from_string(const std::string& text)
{
    auto nibble = [](char c) -> int
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    if (text.size() != 36)
    {
        throw SerializationError("AssetID: expected 36-char UUID, got '" + text + "'");
    }

    std::array<std::uint8_t, 16> out{};
    std::size_t byte_index = 0;
    for (std::size_t i = 0; i < text.size();)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (text[i] != '-')
            {
                throw SerializationError("AssetID: expected '-' at position " + std::to_string(i));
            }
            ++i;
            continue;
        }
        int hi = nibble(text[i]);
        int lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0)
        {
            throw SerializationError("AssetID: invalid hex in '" + text + "'");
        }
        out[byte_index++] = static_cast<std::uint8_t>((hi << 4) | lo);
        i += 2;
    }
    return AssetID(out);
}

// Typed handle to a loaded Resource. Serializes as its AssetID; the runtime
// table index/generation are never serialized (RFC 0001, Asset & Resource).
template <class T>
class Handle
{
public:
    Handle() = default;
    explicit Handle(AssetID id) : asset_(id) {}

    const AssetID& asset_id() const noexcept { return asset_; }

    friend bool operator==(const Handle& a, const Handle& b) noexcept { return a.asset_ == b.asset_; }
    friend bool operator!=(const Handle& a, const Handle& b) noexcept { return !(a == b); }

private:
    AssetID asset_{};
    std::uint32_t index_ = 0;      // runtime-only; never serialized
    std::uint32_t generation_ = 0; // runtime-only; never serialized
};

// Re-resolution hook called on load for reference fields. The default is
// identity; the future asset server and World entity-remap supply real
// implementations without changing the serialization layer.
class ReferenceResolver
{
public:
    virtual ~ReferenceResolver() = default;

    virtual mosaic::foundation::EntityID resolve_entity(mosaic::foundation::EntityID serialized) const
    {
        return serialized;
    }

    virtual AssetID resolve_asset(AssetID serialized) const { return serialized; }
};

} // namespace mosaic::foundation::serialization

template <>
struct std::hash<mosaic::foundation::serialization::AssetID>
{
    std::size_t operator()(const mosaic::foundation::serialization::AssetID& id) const noexcept
    {
        // FNV-1a over the 16 identity bytes.
        std::size_t h = 1469598103934665603ull;
        for (std::uint8_t byte : id.bytes())
        {
            h ^= byte;
            h *= 1099511628211ull;
        }
        return h;
    }
};
