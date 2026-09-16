#pragma once

#include <functional>
#include <istream>
#include <ostream>
#include <string>

#include <nlohmann/json.hpp>

namespace mosaic::app::mcp
{
// Invokes one MCP tool by name with its JSON arguments and returns its JSON result, or
// throws (any std::exception) to signal a tool-level failure -- caught here and reported
// as an MCP isError:true result, never propagated. Bound by app/run/mcp.cpp over the real
// World/ComponentReflection/CommandStack/scene path; a test can inject a stub.
using ToolDispatch = std::function<nlohmann::json(const std::string& tool_name, const nlohmann::json& arguments)>;

// Handles one JSON-RPC 2.0 request/notification line: routes "initialize", "tools/list",
// and "tools/call" (the latter through `dispatch`); an unknown method yields a JSON-RPC
// "method not found" error. Returns the response line to write (no trailing newline), or
// an empty string for a notification (no "id" field) or a blank input line -- a
// notification never produces a response, even for a recognized method. Never throws:
// malformed JSON on the line yields a JSON-RPC parse-error response instead of a crash.
std::string handle_request_line(const std::string& line, const ToolDispatch& dispatch);

// Reads newline-delimited JSON-RPC requests from `in` until EOF, writing each non-empty
// response line (terminated by '\n', flushed immediately) to `out`. Thin loop around
// handle_request_line -- the MCP stdio transport.
void run_stdio_loop(std::istream& in, std::ostream& out, const ToolDispatch& dispatch);
} // namespace mosaic::app::mcp
