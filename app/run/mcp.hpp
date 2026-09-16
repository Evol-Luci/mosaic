#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{
// Handle `mosaic mcp <project-dir>`: resolve the project's manifest + default scene,
// build a resident World/ComponentReflection/CommandStack, and run an MCP server over
// stdin/stdout until EOF. See
// docs/superpowers/specs/2026-09-16-agent-api-mcp-server-design.md.
int run_mcp(const Options& options);
} // namespace mosaic::app
