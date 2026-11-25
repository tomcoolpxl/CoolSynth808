// Claude's MCP server switched in Options (CoolSynth's D363, as CoolSynth 303's): off by default, the switch on the
// MIDI page starts and stops it at once and the settings keep it, COOLSYNTH808_NO_MCP keeps it off. On a free port of
// its own, never the app's 47608 (a running CoolSynth 808 may hold it). A class of its own, its parts on the heap:
// MSVC's Debug frames overflow with many processors and editors in one runTest.

#include <cstdlib>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Editor.h"
#include "Processor.h"
#include "ui/OptionsWindow.h"
#include "ui/Widgets.h"

namespace
{
    using namespace cs808;

    int freePort()
    {
        juce::StreamingSocket probe;
        if (! probe.createListener(0, "127.0.0.1"))
            return 0;
        const int port = probe.getBoundPort();
        probe.close();
        return port;
    }

    bool reachable(int port)
    {
        juce::StreamingSocket client;
        return client.connect("127.0.0.1", port, 1000);
    }

    bool keptOn(const juce::File& settings) { return mcpChoiceFrom(juce::PropertiesFile(settings, {})); }

    void setEnvironment(const char* name, const char* value)
    {
       #if JUCE_WINDOWS
        _putenv_s(name, value); // "" removes it
       #else
        if (*value != 0)
            setenv(name, value, 1);
        else
            unsetenv(name);
       #endif
    }

    juce::Component* childWithId(juce::Component& root, const juce::String& id)
    {
        for (auto* child : root.getChildren())
            if (child->getComponentID() == id)
                return child;
        return nullptr;
    }

    /** A key's click, as its mouse-up runs it (off screen a key contains no point, so its own check would fail). */
    void click(Chip& c)
    {
        const auto centre = c.getLocalBounds().getCentre().toFloat();
        const auto when = juce::Time::getCurrentTime();
        const juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(), centre, juce::ModifierKeys(),
                                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                 juce::MouseInputSource::defaultTiltY, &c, &c, when, centre, when, 1, false);
        if (c.onClick)
            c.onClick(e);
    }

    class McpSwitchTests final : public juce::UnitTest
    {
    public:
        McpSwitchTests() : juce::UnitTest("Options MCP switch", "CoolSynth808") {}

        void runTest() override
        {
            const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("CoolSynth808McpSwitchTest");
            (void) folder.deleteRecursively();
            folder.createDirectory();
            const auto settings = folder.getChildFile("CoolSynth808.settings");
            const int port = freePort();

            beginTest("the_mcp_server_is_off_by_default");
            {
                expect(port > 0, "a free port");
                expect(! mcpChoiceFrom(juce::PropertySet()), "no setting: off");
                auto p = std::make_unique<Processor>();
                expect(! p->hasMcpSwitch() && ! p->mcpServerRunning(), "none outside the standalone");
                p->useMcpSwitchForTesting(port, settings);
                expect(p->hasMcpSwitch());
                expect(! p->mcpServerChosen() && ! p->mcpServerRunning(), "off with no settings file");
                expectEquals(p->mcpStatus(), juce::String("Off"));
                expect(! reachable(port), "nothing listens");
            }

            beginTest("the_switch_starts_and_stops_it_at_once");
            {
                auto p = std::make_unique<Processor>();
                p->useMcpSwitchForTesting(port, settings);
                p->setMcpServerOn(true);
                expect(p->mcpServerChosen() && p->mcpServerRunning(), "on at once");
                expectEquals(p->mcpStatus(), "On: 127.0.0.1:" + juce::String(port));
                expect(reachable(port), "Claude can connect");
                p->setMcpServerOn(false);
                expect(! p->mcpServerChosen() && ! p->mcpServerRunning(), "off at once");
                expect(! reachable(port), "its port let go");
            }

            beginTest("a_taken_port_or_coolsynth808_no_mcp_leaves_it_off");
            {
                auto p = std::make_unique<Processor>();
                p->useMcpSwitchForTesting(port, settings);
                {
                    juce::StreamingSocket holder;
                    expect(holder.createListener(port, "127.0.0.1"), "the port held elsewhere");
                    p->setMcpServerOn(true);
                    expect(p->mcpServerChosen() && ! p->mcpServerRunning(), "chosen, not running");
                    expectEquals(p->mcpStatus(), "Port " + juce::String(port) + " is taken");
                    p->setMcpServerOn(false);
                }
                setEnvironment("COOLSYNTH808_NO_MCP", "1");
                p->setMcpServerOn(true);
                expect(! p->mcpServerRunning(), "the environment's veto");
                expectEquals(p->mcpStatus(), juce::String("Off: COOLSYNTH808_NO_MCP"));
                expect(! reachable(port));
                setEnvironment("COOLSYNTH808_NO_MCP", "");
                p->setMcpServerOn(false);
            }

            beginTest("the_midi_page_s_switch_and_the_settings_keep_it");
            {
                Editor::useSettingsFileForTesting(settings);
                auto p = std::make_unique<Processor>();
                p->setPlayConfigDetails(0, 2, 48000.0, 480);
                p->prepareToPlay(48000.0, 480);
                std::unique_ptr<juce::AudioProcessorEditor> e(p->createEditor());
                auto* editor = dynamic_cast<Editor*>(e.get());
                auto devices = std::make_unique<juce::AudioDeviceManager>();
                {
                    // without the switch (a plugin, the tests' processor): no row
                    auto panel = makeOptionsPanelForTesting(editor->optionsHooks(), devices.get());
                    showOptionsPageForTesting(*panel, 1);
                    auto* key = childWithId(*panel, "mcpServer");
                    expect(key != nullptr && ! key->isVisible(), "no switch without a server to switch");
                }
                p->useMcpSwitchForTesting(port, settings);
                auto panel = makeOptionsPanelForTesting(editor->optionsHooks(), devices.get());
                showOptionsPageForTesting(*panel, 1); // Audio, MIDI, ...
                auto* key = dynamic_cast<Chip*>(childWithId(*panel, "mcpServer"));
                expect(key != nullptr && key->isVisible(), "the switch on the MIDI page");
                if (key != nullptr)
                {
                    expect(panel->getLocalBounds().contains(key->getBounds()), "inside the panel");
                    expect(key->getTooltip().isNotEmpty(), "with a hint on what it does");
                    expect(! key->isOn(), "off");
                    click(*key);
                    expect(p->mcpServerRunning() && reachable(port) && key->isOn(), "a click starts it");
                    expect(keptOn(settings), "kept on");
                    const auto snapshots = juce::SystemStats::getEnvironmentVariable("COOLSYNTH808_SNAPSHOTS", {});
                    if (snapshots.isNotEmpty())
                    {
                        juce::File(snapshots).createDirectory();
                        juce::FileOutputStream out(juce::File(snapshots).getChildFile("options-midi-mcp-on.png"));
                        out.setPosition(0);
                        out.truncate();
                        juce::PNGImageFormat().writeImageToStream(panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f), out);
                    }
                    click(*key);
                    expect(! p->mcpServerRunning() && ! reachable(port), "a second click stops it");
                    expect(! keptOn(settings), "kept off");
                    click(*key);
                }
                panel.reset();
                devices.reset();
                e.reset();
                p.reset(); // the app closed with it on: its port let go
                // the next start of the app finds it on
                auto again = std::make_unique<Processor>();
                again->useMcpSwitchForTesting(port, settings);
                expect(again->mcpServerChosen() && again->mcpServerRunning(), "on from the settings");
                again->setMcpServerOn(false);
                again.reset();
                Editor::useSettingsFileForTesting({});
            }

            (void) folder.deleteRecursively();
        }
    };

    static McpSwitchTests mcpSwitchTests;
}
