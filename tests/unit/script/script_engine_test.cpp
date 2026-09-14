#include <string>

#include <catch2/catch_test_macros.hpp>

#include "foundation/serialization/value.hpp"
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

TEST_CASE("captured output is returned even when the script then errors", "[script]")
{
    auto engine = create_python_script_engine();
    const auto r = engine->run_string("print('before boom')\nraise RuntimeError('kaboom')");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error.find("RuntimeError") != std::string::npos);
    REQUIRE(r.output.find("before boom") != std::string::npos); // printed before the raise, captured
}

TEST_CASE("get_global returns a marshaled string value", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE(engine->run_string("x = 'hello'").ok);
    const auto v = engine->get_global("x");
    REQUIRE(v.has_value());
    REQUIRE(v->is<std::string>());
    REQUIRE(v->as<std::string>() == "hello");
}

TEST_CASE("get_global returns a marshaled table value", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE(engine->run_string("d = {'a': 'b'}").ok);
    const auto v = engine->get_global("d");
    REQUIRE(v.has_value());
    REQUIRE(v->is<mosaic::foundation::serialization::Table>());
    const auto& t = v->as<mosaic::foundation::serialization::Table>();
    const auto* entry = t.find("a");
    REQUIRE(entry != nullptr);
    REQUIRE(entry->as<std::string>() == "b");
}

TEST_CASE("get_global returns nullopt for an undefined name", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE_FALSE(engine->get_global("does_not_exist").has_value());
}

TEST_CASE("get_global returns nullopt for a function value", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE(engine->run_string("def f(): pass").ok);
    REQUIRE_FALSE(engine->get_global("f").has_value());
}

TEST_CASE("call_function runs a defined zero-arg function", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE(engine->run_string(
        "count = 0\ndef bump():\n    global count\n    count += 1\n    print(count)").ok);
    const auto r = engine->call_function("bump");
    REQUIRE(r.ok);
    REQUIRE(r.output.find("1") != std::string::npos);
}

TEST_CASE("call_function returns an error for an undefined name", "[script]")
{
    auto engine = create_python_script_engine();
    const auto r = engine->call_function("does_not_exist");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error.find("does_not_exist") != std::string::npos);
}

TEST_CASE("call_function returns an error for a non-callable name", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE(engine->run_string("x = 5").ok);
    const auto r = engine->call_function("x");
    REQUIRE_FALSE(r.ok);
}

TEST_CASE("call_function captures an error raised inside the function", "[script]")
{
    auto engine = create_python_script_engine();
    REQUIRE(engine->run_string("def boom():\n    raise ValueError('nope')").ok);
    const auto r = engine->call_function("boom");
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error.find("ValueError") != std::string::npos);
}
