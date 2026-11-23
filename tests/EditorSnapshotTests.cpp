#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "Editor.h"
#include "ui/LibraryShelf.h"
#include "ui/LidView.h"
#include "Library.h"
#include "Processor.h"

/*
    The panel drawn into PNG files, to look at without a screen: every theme, the lanes open and closed, Under the
    Lid, and the library. Only when COOLSYNTH808_SNAPSHOTS names a folder; otherwise it checks that the panel builds
    and lays out at every width.
*/
using namespace cs808;

class EditorSnapshotTests final : public juce::UnitTest
{
public:
    EditorSnapshotTests() : juce::UnitTest("Editor snapshots", "CoolSynth808") {}

    void runTest() override
    {
        const auto folderName = juce::SystemStats::getEnvironmentVariable("COOLSYNTH808_SNAPSHOTS", {});

        beginTest("the_panel_builds_and_scales");
        {
            auto p = std::make_unique<Processor>();
            p->setPlayConfigDetails(0, 2, 48000.0, 512);
            p->prepareToPlay(48000.0, 512);
            std::unique_ptr<juce::AudioProcessorEditor> e(p->createEditor());
            auto* editor = dynamic_cast<Editor*>(e.get());
            expect(editor != nullptr);
            for (const int width : { 620, 1240, 1860 })
            {
                editor->setSize(width, 600);
                editor->layoutForLanes();
                expectEquals(editor->getWidth(), width);
                expectGreaterThan(editor->getHeight(), 200);
            }
        }

        beginTest("each_complete_drum_matches_its_netlist");
        {
            auto p = std::make_unique<Processor>();
            std::unique_ptr<juce::AudioProcessorEditor> e(p->createEditor());
            const auto problems = dynamic_cast<Editor*>(e.get())->lidForTesting().completeDrumProblems();
            expect(problems.isEmpty(), problems.joinIntoString("; "));
            // D280: every wire a strike lights on the whole board is driven by its drum's recording.
            expectEquals(dynamic_cast<Editor*>(e.get())->lidForTesting().boardFlowsUnbound(), 0);
        }

        if (folderName.isEmpty())
            return;
        const juce::File folder(folderName);
        (void) folder.createDirectory();

        beginTest("snapshots");
        {
            auto p = std::make_unique<Processor>();
            p->setPlayConfigDetails(0, 2, 48000.0, 512);
            p->prepareToPlay(48000.0, 512);
            // A library beat in pattern 1, so the lanes have something to show.
            const auto library = builtInLibrary();
            if (! library.empty())
            {
                const auto& beat = library.front();
                p->editBank([&](SequencerBank& b) { b.pattern[0] = beat.pattern.pattern; }, false);
                p->editPlay([&](PlaySettings& s) { s = beat.pattern.play; });
            }
            std::unique_ptr<juce::AudioProcessorEditor> e(p->createEditor());
            auto* editor = dynamic_cast<Editor*>(e.get());
            editor->setSize(1240, 600);
            const auto shoot = [&](const juce::String& name)
            {
                editor->layoutForLanes();
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream out(folder.getChildFile(name + ".png"));
                out.setPosition(0);
                out.truncate();
                juce::PNGImageFormat().writeImageToStream(image, out);
            };
            for (int t = 0; t < 4; ++t)
            {
                setTheme(t);
                editor->lanesOpen = true;
                editor->lidView = false;
                shoot(juce::String("panel-") + themes()[static_cast<std::size_t>(t)].id);
            }
            setTheme(0);
            editor->lanesOpen = false;
            shoot("panel-lanes-closed");
            editor->lanesOpen = true;
            editor->lidView = true;
            shoot("lid");
            // Every drum struck, 40 ms on: the board lit from the recordings (D280).
            editor->lidForTesting().tick(1000.0);
            editor->lidForTesting().struck(0x7ffu);
            editor->lidForTesting().tick(1040.0);
            shoot("lid-struck");
            editor->lidForTesting().tick(1400.0);
            shoot("lid-struck-400ms");
            editor->lidForTesting().showSheet(1);
            shoot("lid-bass-drum");
            editor->lidForTesting().showSheet(2);
            shoot("lid-snare");
            editor->lidForTesting().showSheet(3);
            shoot("lid-low-tom");
            editor->lidForTesting().showSheet(4);
            shoot("lid-rim-shot");
            editor->lidForTesting().showSheet(5);
            shoot("lid-hand-clap");
            editor->lidForTesting().showSheet(6);
            shoot("lid-cowbell");
            editor->lidForTesting().showSheet(7);
            shoot("lid-metal-oscillators");
            editor->lidForTesting().showSheet(8);
            shoot("lid-cymbal");
            editor->lidForTesting().showSheet(9);
            shoot("lid-hi-hats");
            editor->lidForTesting().showSheet(0);
            editor->lidView = false;
            editor->patternMode = true;
            shoot("pattern-mode");
            editor->patternMode = false;
            editor->setSize(620, 300);
            shoot("panel-narrow");
            editor->setSize(1240, 600);
            editor->openControllerSetup();
            shoot("controller");
            // The library's shelf (D273), a beat of two halves chosen, and drawn smaller in a narrow window.
            editor->openLibrary();
            if (auto* shelf = editor->libraryForTesting())
            {
                shelf->showForTesting("House and acid");
                shelf->selectForTesting(1);
                shoot("library");
                editor->setSize(620, 300);
                shoot("library-narrow");
                editor->setSize(1240, 600);
            }
            expect(folder.getChildFile("lid.png").existsAsFile());
        }
    }
};

static EditorSnapshotTests editorSnapshotTests;
