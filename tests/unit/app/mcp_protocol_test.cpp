#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "app/mcp/mcp_protocol.hpp"

using mosaic::app::mcp::handle_request_line;
using mosaic::app::mcp::run_stdio_loop;
using mosaic::app::mcp::ToolDispatch;

namespace
{
ToolDispatch echo_dispatch()
{
    return [](const std::string& tool_name, const nlohmann::json& arguments) -> nlohmann::json
    { return {{"called", tool_name}, {"arguments", arguments}}; };
}

ToolDispatch throwing_dispatch(const std::string& message)
{
    return [message](const std::string&, const nlohmann::json&) -> nlohmann::json
    { throw std::runtime_error(message); };
}
} // namespace

TEST_CASE("mcp_protocol: initialize returns server info", "[app][mcp]")
{
    const std::string response = handle_request_line(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("id").get<int>() == 1);
    REQUIRE(parsed.at("result").at("serverInfo").at("name").get<std::string>() == "mosaic-mcp");
}

TEST_CASE("mcp_protocol: notifications/initialized produces no response", "[app][mcp]")
{
    const std::string response =
        handle_request_line(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", echo_dispatch());
    REQUIRE(response.empty());
}

TEST_CASE("mcp_protocol: tools/list returns the full tool schema", "[app][mcp]")
{
    const std::string response =
        handle_request_line(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})", echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("result").at("tools").size() == 25);
}

TEST_CASE("mcp_protocol: tools/call dispatches and wraps a successful result", "[app][mcp]")
{
    const std::string response = handle_request_line(
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"create_entity","arguments":{}}})",
        echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("result").at("isError").get<bool>() == false);
    const nlohmann::json inner = nlohmann::json::parse(
        parsed.at("result").at("content").at(0).at("text").get<std::string>());
    REQUIRE(inner.at("called").get<std::string>() == "create_entity");
}

TEST_CASE("mcp_protocol: tools/call wraps a dispatch exception as isError:true, not a crash", "[app][mcp]")
{
    const std::string response = handle_request_line(
        R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"anything","arguments":{}}})",
        throwing_dispatch("unknown tool: anything"));
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("result").at("isError").get<bool>() == true);
    REQUIRE(parsed.at("result").at("content").at(0).at("text").get<std::string>() == "unknown tool: anything");
}

TEST_CASE("mcp_protocol: malformed JSON on the line yields a JSON-RPC parse error", "[app][mcp]")
{
    const std::string response = handle_request_line("{not json", echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("error").at("code").get<int>() == -32700);
}

TEST_CASE("mcp_protocol: syntactically valid but non-object JSON yields Invalid Request, not a crash", "[app][mcp]")
{
    const std::string response = handle_request_line("42", echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("error").at("code").get<int>() == -32600);
}

TEST_CASE("mcp_protocol: a non-object 'params' is treated as empty, not a crash", "[app][mcp]")
{
    const std::string response = handle_request_line(
        R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":42})", echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("result").at("isError").get<bool>() == false);
}

TEST_CASE("mcp_protocol: unknown method yields a JSON-RPC method-not-found error", "[app][mcp]")
{
    const std::string response =
        handle_request_line(R"({"jsonrpc":"2.0","id":5,"method":"not/a/real/method"})", echo_dispatch());
    const nlohmann::json parsed = nlohmann::json::parse(response);
    REQUIRE(parsed.at("error").at("code").get<int>() == -32601);
}

TEST_CASE("mcp_protocol: an unknown method with no id is a silent notification, not an error", "[app][mcp]")
{
    const std::string response =
        handle_request_line(R"({"jsonrpc":"2.0","method":"not/a/real/method"})", echo_dispatch());
    REQUIRE(response.empty());
}

TEST_CASE("mcp_protocol: run_stdio_loop processes multiple lines and skips notification responses", "[app][mcp]")
{
    std::istringstream in(
        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}\n");
    std::ostringstream out;
    run_stdio_loop(in, out, echo_dispatch());

    std::istringstream lines(out.str());
    std::string line;
    int count = 0;
    while (std::getline(lines, line))
    {
        REQUIRE_FALSE(line.empty());
        ++count;
    }
    REQUIRE(count == 2); // the notification produced no output line
}
