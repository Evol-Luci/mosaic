#include <string>

#include <catch2/catch_test_macros.hpp>

#include "modules/script_python/python_script_engine.hpp"

using mosaic::script::python::create_python_script_engine;

TEST_CASE("a Python script runs and its stdout is captured", "[script]")
{
    auto engine = create_python_script_engine();
    const auto r = engine->run_string("print('hello from python')");
    REQUIRE(r.ok);
    REQUIRE(r.error.empty());
    REQUIRE(r.output.find("hello from python") != std::string::npos);
}

TEST_CASE("a Python script really executes (computes 6*7)", "[script]")
{
    auto engine = create_python_script_engine();
    const auto r = engine->run_string("print(6 * 7)");
    REQUIRE(r.ok);
    REQUIRE(r.output.find("42") != std::string::npos);
}

TEST_CASE("state persists across runs on one engine, isolated between engines", "[script]")
{
    auto a = create_python_script_engine();
    REQUIRE(a->run_string("x = 5").ok);
    const auto seen = a->run_string("print(x)");
    REQUIRE(seen.ok);
    REQUIRE(seen.output.find("5") != std::string::npos);

    // A second engine has its own namespace: it does NOT see `x`.
    auto b = create_python_script_engine();
    const auto missing = b->run_string("print(x)");
    REQUIRE_FALSE(missing.ok);
    REQUIRE(missing.error.find("NameError") != std::string::npos);
}

TEST_CASE("a syntax error is captured, not thrown", "[script]")
{
    auto engine = create_python_script_engine();
    const auto r = engine->run_string("this is not valid python !!");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error.find("SyntaxError") != std::string::npos);
}

TEST_CASE("a runtime error is captured with its type and message", "[script]")
{
    auto engine = create_python_script_engine();
    const auto r = engine->run_string("raise ValueError('boom')");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error.find("ValueError") != std::string::npos);
    REQUIRE(r.error.find("boom") != std::string::npos);
}
