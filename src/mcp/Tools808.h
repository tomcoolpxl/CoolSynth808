#pragma once

#include <vector>

#include "McpServer.h"

namespace cs808
{
    class Processor;

    /** The port the standalone app's MCP server listens on, on 127.0.0.1: CoolSynth's is 47613, the 303's 47633. */
    inline constexpr int mcpPort = 47608;

    /**
        CoolSynth 808's tools, so Claude can drive, hear and see the running app (D333, as CoolSynth's D225 and the
        303's): read and set the kit (every knob, the drums' switches, the volume) and the tempo and swing; run the
        transport; strike drums; record the output into a WAV file; picture the window; and open the views (the lifted
        lid, a sheet, Options). `runner` runs a job on the message thread; the tools that wait (strike, record) run on
        the server's thread and post their work through it.
    */
    [[nodiscard]] std::vector<coolsynth::mcp::Tool> tools808(Processor& processor, coolsynth::mcp::Server::Runner runner);
}
