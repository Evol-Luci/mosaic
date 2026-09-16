#include "app/mcp/mcp_protocol.hpp"

#include <exception>
#include <string>

#include "app/mcp/mcp_tools.hpp"

namespace mosaic::app::mcp
{
namespace
{
nlohmann::json error_response(const nlohmann::json& id, int code, const std::string& message)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

nlohmann::json result_response(const nlohmann::json& id, const nlohmann::json& result)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}
} // namespace

std::string handle_request_line(const std::string& line, const ToolDispatch& dispatch)
{
    if (line.empty())
    {
        return "";
    }

    nlohmann::json request;
    try
    {
        request = nlohmann::json::parse(line);
    }
    catch (const nlohmann::json::parse_error&)
    {
        return error_response(nullptr, -32700, "Parse error").dump();
    }

    // Syntactically valid JSON that isn't a request object (a bare number, string, or
    // array) -- .value()/.at() below require an object and would otherwise throw an
    // uncaught type_error. contains() itself is safe on any type (false for non-objects),
    // so this check must come first.
    if (!request.is_object())
    {
        return error_response(nullptr, -32600, "Invalid Request").dump();
    }

    const bool has_id = request.contains("id");
    const nlohmann::json id = has_id ? request.at("id") : nlohmann::json(nullptr);
    const std::string method = request.value("method", std::string());
    // Same non-object guard as above, one level deeper: a syntactically valid but
    // wrongly-typed "params" (e.g. a bare number) must not make the tools/call branch's
    // params.value(...) calls throw.
    nlohmann::json params = request.value("params", nlohmann::json::object());
    if (!params.is_object())
    {
        params = nlohmann::json::object();
    }

    nlohmann::json result;
    if (method == "initialize")
    {
        result = {{"protocolVersion", "2024-11-05"},
                 {"capabilities", {{"tools", nlohmann::json::object()}}},
                 {"serverInfo", {{"name", "mosaic-mcp"}, {"version", "0.1.0"}}}};
    }
    else if (method == "notifications/initialized")
    {
        return "";
    }
    else if (method == "tools/list")
    {
        result = tools_list_schema();
    }
    else if (method == "tools/call")
    {
        const std::string tool_name = params.value("name", std::string());
        const nlohmann::json arguments = params.value("arguments", nlohmann::json::object());
        try
        {
            const nlohmann::json tool_result = dispatch(tool_name, arguments);
            result = {{"content", {{{"type", "text"}, {"text", tool_result.dump()}}}}, {"isError", false}};
        }
        catch (const std::exception& e)
        {
            result = {{"content", {{{"type", "text"}, {"text", std::string(e.what())}}}}, {"isError", true}};
        }
    }
    else
    {
        if (!has_id) { return ""; }
        return error_response(id, -32601, "Method not found: " + method).dump();
    }

    if (!has_id)
    {
        return "";
    }
    return result_response(id, result).dump();
}

void run_stdio_loop(std::istream& in, std::ostream& out, const ToolDispatch& dispatch)
{
    std::string line;
    while (std::getline(in, line))
    {
        const std::string response = handle_request_line(line, dispatch);
        if (!response.empty())
        {
            out << response << '\n';
            out.flush();
        }
    }
}
} // namespace mosaic::app::mcp
