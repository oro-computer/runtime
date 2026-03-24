#ifndef ORO_CLI_MCP_H
#define ORO_CLI_MCP_H

#include "../runtime.hh"

namespace oro::cli::mcp {
  struct Options {
    runtime::Path workspaceRoot = ".";
    runtime::Path configPath;
    runtime::String cliExecutable;
    runtime::String cliDisplayName = "oroc";

    bool useHttp = false;
    runtime::String host = "127.0.0.1";
    int port = 0;
    runtime::String endpoint = "/mcp";
    runtime::String token;

    // File system access policy for MCP tools/resources.
    bool allowReadOutsideWorkspace = true;

    // Streamable HTTP transport policy.
    bool replaceSseStreamOnReconnect = false;
  };

  int run(const Options& options);
}

#endif
