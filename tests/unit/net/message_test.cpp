#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/net/message.hpp"

using mosaic::engine::net::decode;
using mosaic::engine::net::encode;
using mosaic::engine::net::get_u32;
using mosaic::engine::net::MessageType;
using mosaic::engine::net::put_u32;

TEST_CASE("encode/decode round-trips Hello and Welcome", "[net]")
{
    MessageType t{};
    std::string payload;

    REQUIRE(decode(encode(MessageType::Hello, ""), t, payload));
    REQUIRE(t == MessageType::Hello);
    REQUIRE(payload.empty());

    REQUIRE(decode(encode(MessageType::Welcome, put_u32(7u)), t, payload));
    REQUIRE(t == MessageType::Welcome);
    REQUIRE(get_u32(payload) == 7u);
}

TEST_CASE("decode rejects empty and unknown-type frames", "[net]")
{
    MessageType t{};
    std::string payload;
    REQUIRE_FALSE(decode("", t, payload));
    REQUIRE_FALSE(decode(std::string(1, static_cast<char>(0x99)), t, payload)); // unknown type
}

TEST_CASE("put_u32/get_u32 round-trip (little-endian)", "[net]")
{
    REQUIRE(get_u32(put_u32(0xDEADBEEFu)) == 0xDEADBEEFu);
    REQUIRE(get_u32(put_u32(0u)) == 0u);
    REQUIRE(get_u32("abc") == 0u); // too short -> 0, no crash
}
