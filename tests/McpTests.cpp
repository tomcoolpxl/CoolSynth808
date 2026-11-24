// The MCP server's tools (D333, Claude's way into the standalone app): listed, read, set, and the capture the record
// tool writes from, without a socket or an audio device (the server's own HTTP handling is CoolSynth's, tested there).
// After CoolSynth 303's.

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Processor.h"
#include "mcp/Tools808.h"

namespace
{
    class McpTests final : public juce::UnitTest
    {
    public:
        McpTests() : juce::UnitTest("Mcp", "CoolSynth808") {}

        void runTest() override
        {
            auto processor = std::make_unique<cs808::Processor>();
            processor->setPlayConfigDetails(0, 2, 48000.0, 480);
            processor->prepareToPlay(48000.0, 480);
            const coolsynth::mcp::Server::Runner direct = [](std::function<void()> job) { job(); };
            coolsynth::mcp::Server server("CoolSynth 808", "test", cs808::tools808(*processor, direct), direct);
            int id = 0;
            const auto call = [&](const juce::String& method, const juce::var& params)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty("jsonrpc", "2.0");
                o->setProperty("id", ++id);
                o->setProperty("method", method);
                o->setProperty("params", params);
                return server.handleMessage(juce::var(o));
            };
            const auto tool = [&](const juce::String& toolName, const juce::String& argumentsJson)
            {
                auto* p = new juce::DynamicObject();
                p->setProperty("name", toolName);
                p->setProperty("arguments", juce::JSON::parse(argumentsJson));
                const auto answer = call("tools/call", juce::var(p));
                return answer["result"]["content"][0]["text"].toString();
            };

            beginTest("the_tools_are_listed");
            {
                const auto list = call("tools/list", juce::var(new juce::DynamicObject()));
                juce::StringArray names;
                if (const auto* tools = list["result"]["tools"].getArray())
                    for (const auto& t : *tools)
                        names.add(t["name"].toString());
                for (const char* wanted : { "get_state", "set_values", "transport", "strike", "record", "screenshot", "view" })
                    expect(names.contains(wanted), juce::String("listed: ") + wanted);
            }

            beginTest("get_state_reads_the_kit_and_set_values_changes_it");
            {
                const auto before = juce::JSON::parse(tool("get_state", "{}"));
                expect(before["knobs"].hasProperty("accent") && before["switches"].hasProperty("RS"), "the knobs and switches by name");
                const auto after = juce::JSON::parse(tool("set_values", "{\"values\": {\"accent\": 0.25, \"RS\": \"CL\", \"tempo\": 125}}"));
                expectWithinAbsoluteError(static_cast<double>(processor->project().kit.knob[0]), 0.25, 1.0e-6);
                expect(processor->project().kit.alt[5], "the rim shot's switch on claves");
                expectWithinAbsoluteError(processor->project().play.tempo, 125.0, 1.0e-9);
                expectEquals(after["switches"]["RS"].toString(), juce::String("CL"));
                expect(tool("set_values", "{\"values\": {\"nothing\": 1}}").startsWith("Not on the panel"), "an unknown name is refused");
            }

            beginTest("a_capture_keeps_the_output_while_armed");
            {
                processor->startCapture(0.2);
                processor->strike(0, true);
                juce::AudioBuffer<float> buffer(2, 480);
                juce::MidiBuffer midi;
                for (int b = 0; b < 30; ++b)
                {
                    buffer.clear();
                    processor->processBlock(buffer, midi);
                }
                expectEquals(processor->captured(), 9600, "0.2 s at 48 kHz, then no more");
                float peak = 0.0f;
                for (const float v : processor->capture())
                    peak = std::max(peak, std::abs(v));
                expect(peak > 0.01f, "the bass drum in it");
                expect(processor->takeLoudest() > 0.01f, "the loudest sample seen");
            }
        }
    };

    static McpTests mcpTests;
}
