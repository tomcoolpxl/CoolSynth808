#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "Editor.h"
#include "ui/LibraryShelf.h"
#include "ui/LidView.h"
#include "ui/OptionsWindow.h"
#include "ui/Text.h"
#include "ui/Theme.h"
#include "Library.h"
#include "Processor.h"

/*
    The panel drawn into PNG files, to look at without a screen: every theme while a beat plays, a pattern waiting
    for the bar line, stopped, Under the Lid, and the library. Only when COOLSYNTH808_SNAPSHOTS names a folder;
    otherwise it checks that the panel builds and lays out at every width.
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
            for (const int width : { 620, 1280, 1860 })
            {
                editor->setSize(width, 600);
                editor->layOut();
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

        beginTest("text_kept_shaped_measures_as_juce_measures_it");
        {
            // D306: the panel's words are shaped once and kept; they must measure as JUCE shapes them each time.
            for (const auto& font : { labelFont(10.0f), monoFont(11.0f), monoFont(9.0f, true), headFont(13.0f) })
                for (const juce::String text : { "BASS DRUM", "16", "01", "CLOSED HAT", "LOOP" })
                {
                    expectWithinAbsoluteError(shapedWidth(font, text), juce::GlyphArrangement::getStringWidth(font, text), 0.01f);
                    expectWithinAbsoluteError(shapedWidth(font, text), juce::GlyphArrangement::getStringWidth(font, text), 0.01f); // kept
                }
        }

        beginTest("a_pattern_key_picked_while_playing_lights_at_once_not_at_the_bar_line");
        {
            // D303: with Change at the bar, the pick reaches the processor only at the bar line; its key lit only then.
            auto p = std::make_unique<Processor>();
            p->setPlayConfigDetails(0, 2, 48000.0, 512);
            p->prepareToPlay(48000.0, 512);
            p->editPlay([](PlaySettings& s) { s.changeAtBar = true; });
            p->setChain({ 0 });
            std::unique_ptr<juce::AudioProcessorEditor> e(p->createEditor());
            auto* editor = dynamic_cast<Editor*>(e.get());
            juce::AudioBuffer<float> buffer(2, 512);
            juce::MidiBuffer midi;
            p->setPlaying(true);
            for (int block = 0; block < 2000 && ! (p->isPlaying() && p->playingSixteenth() >= 2); ++block)
            {
                buffer.clear();
                midi.clear();
                p->processBlock(buffer, midi);
            }
            expect(p->isPlaying());
            editor->selectPatternKey(3, false);
            expectEquals(p->playingPattern(), 0);
            expectEquals(editor->queuedPattern(), 3);
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
            // Library beats in patterns 1 and 2, chained 1, 1, 2, so the lanes and the chain have something to show.
            const auto library = builtInLibrary();
            if (library.size() > 1)
            {
                const auto& beat = library.front();
                p->editBank([&](SequencerBank& b)
                {
                    b.pattern[0] = beat.pattern.pattern;
                    b.pattern[1] = library[1].pattern.pattern;
                }, false);
                p->editPlay([&](PlaySettings& s) { s = beat.pattern.play; });
            }
            p->setChain({ 0, 0, 1 });
            std::unique_ptr<juce::AudioProcessorEditor> e(p->createEditor());
            auto* editor = dynamic_cast<Editor*>(e.get());
            editor->setSize(1280, 800);
            // Playing, on the fifth step of the bar (D292): the playing pattern, the chain's block and the steps lit.
            const auto runUntil = [&](const std::function<bool()>& done)
            {
                juce::AudioBuffer<float> buffer(2, 512);
                juce::MidiBuffer midi;
                for (int block = 0; block < 2000 && ! done(); ++block)
                {
                    buffer.clear();
                    midi.clear();
                    p->processBlock(buffer, midi);
                }
            };
            const auto playTo = [&](int sixteenth) { runUntil([&] { return p->isPlaying() && p->playingSixteenth() >= sixteenth; }); };
            p->setPlaying(true);
            playTo(4);
            expect(p->isPlaying());
            const auto shoot = [&](const juce::String& name)
            {
                editor->layOut();
                // twice the density, to judge the drawing as a screen shows it (D292)
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
                juce::FileOutputStream out(folder.getChildFile(name + ".png"));
                out.setPosition(0);
                out.truncate();
                juce::PNGImageFormat().writeImageToStream(image, out);
            };
            for (int t = 0; t < static_cast<int>(themes().size()); ++t)
            {
                setTheme(t);
                editor->boardForTesting().themeChanged();
                editor->lidView = false;
                shoot(juce::String("panel-") + themes()[static_cast<std::size_t>(t)].id);
                // The options window's inside in the theme (D311), with a device manager so its audio part shows.
                {
                    juce::AudioDeviceManager devices;
                    OptionsHooks hooks;
                    hooks.theme = [t] { return t; };
                    hooks.chooseTheme = [](int) {};
                    hooks.zoom = [] { return 100; };
                    hooks.chooseZoom = [](int) {};
                    hooks.followClock = [] { return true; };
                    hooks.sendClock = [] { return false; };
                    hooks.chooseFollowClock = hooks.chooseSendClock = [](bool) {};
                    hooks.controllerSetup = [] {};
                    hooks.circuitAnimated = [] { return true; };
                    hooks.chooseCircuitAnimated = [](bool) {};
                    hooks.circuitShow = [] { return 1; };
                    hooks.chooseCircuitShow = [](int) {};
                    auto options = makeOptionsPanelForTesting(std::move(hooks), &devices);
                    const auto image = options->createComponentSnapshot(options->getLocalBounds(), true, 2.0f);
                    juce::FileOutputStream out(folder.getChildFile(juce::String("options-") + themes()[static_cast<std::size_t>(t)].id + ".png"));
                    out.setPosition(0);
                    out.truncate();
                    juce::PNGImageFormat().writeImageToStream(image, out);
                }
            }
            setTheme(0);
            editor->boardForTesting().themeChanged();
            editor->lidView = true;
            shoot("lid");
            // Every drum struck, 40 ms on: the board lit from the recordings (D280).
            editor->lidForTesting().tick(1000.0);
            editor->lidForTesting().struck(0x7ffu);
            editor->lidForTesting().tick(1040.0);
            shoot("lid-struck");
            editor->lidForTesting().tick(1400.0);
            shoot("lid-struck-400ms");
            // With COOLSYNTH808_FRAMES set, a bar of a plain beat at 120 BPM, one frame every 20 ms, on the board
            // and on the hi-hats sheet: the animation as the app plays it, to watch without the app.
            if (juce::SystemStats::getEnvironmentVariable("COOLSYNTH808_FRAMES", {}).isNotEmpty())
            {
                const auto frames = folder.getChildFile("frames");
                frames.createDirectory();
                for (const int sheet : { 0, 9 })
                {
                    editor->lidForTesting().showSheet(sheet);
                    const double start = 10000.0 + sheet * 10000.0;
                    int nextStep = 0;
                    for (int f = 0; f < 100; ++f)
                    {
                        const double now = start + f * 20.0;
                        editor->lidForTesting().tick(now);
                        for (; nextStep < 16 && nextStep * 125.0 <= f * 20.0; ++nextStep)
                        {
                            unsigned lanes = 1u << 10;                              // the closed hat on every step
                            if (nextStep % 8 == 0 || nextStep == 10) lanes |= 1u << 0;   // the bass drum
                            if (nextStep % 8 == 4) lanes |= 1u << 1;                // the snare
                            if (nextStep == 14) lanes = (lanes & ~(1u << 10)) | (1u << 9);  // an open hat
                            editor->lidForTesting().struck(lanes);
                        }
                        editor->layOut();
                        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                        juce::FileOutputStream out(frames.getChildFile(juce::String(sheet) + "-" + juce::String(f).paddedLeft('0', 3) + ".png"));
                        out.setPosition(0);
                        out.truncate();
                        juce::PNGImageFormat().writeImageToStream(image, out);
                    }
                }
            }
            editor->lidForTesting().showSheet(1);
            shoot("lid-bass-drum");
            // The bass drum's sheet live (D317): shown, it has the engine capture the next bass drum's circuit as the
            // pattern plays; at 3 ms into that hit, the moment the resonator is struck.
            {
                auto& lid = editor->lidForTesting();
                editor->layOut();
                lid.tick(100000.0);
                auto& watch = p->circuitWatch();
                runUntil([&] { return watch.captureCount() > 0 && watch.framesCaptured() > 8 && watch.capturedTime(watch.framesCaptured() - 1) >= 299.0f; });
                lid.tick(100040.0);
                expect(lid.showMomentForTesting(3.0), "the sheet took the engine's capture");
                shoot("lid-bass-drum-live");
                lid.showMomentForTesting(0.0);
            }
            // Every other sheet live (D318): each drum struck on the bar's first step, each sheet shown in turn takes
            // the engine's capture of its drum's next hit, shown a few milliseconds into it. The bank is put back after.
            {
                auto& lid = editor->lidForTesting();
                auto& watch = p->circuitWatch();
                SequencerBank saved;
                p->editBank([&](SequencerBank& b)
                {
                    saved = b;
                    for (auto& pattern : b.pattern)
                        for (int lane = 0; lane <= 9; ++lane)
                            pattern.lanes[static_cast<std::size_t>(lane)].a[0].on = true;
                }, false);
                struct Live { int sheet; float length; double moment; const char* name; };
                const Live live[] { { 2, 300.0f, 3.0, "snare" }, { 3, 600.0f, 5.0, "low-tom" }, { 4, 150.0f, 1.0, "rim-shot" },
                                    { 5, 300.0f, 12.0, "hand-clap" }, { 6, 250.0f, 5.0, "cowbell" }, { 7, 30.0f, 10.0, "metal-oscillators" },
                                    { 8, 600.0f, 5.0, "cymbal" }, { 9, 400.0f, 5.0, "hi-hats" } };
                double now = 110000.0;
                for (const auto& l : live)
                {
                    lid.showSheet(l.sheet);
                    editor->layOut();
                    lid.tick(now);
                    const auto before = watch.captureCount();
                    runUntil([&] { return watch.captureCount() > before && watch.framesCaptured() > 8 && watch.capturedTime(watch.framesCaptured() - 1) >= l.length - 1.0f; });
                    lid.tick(now + 40.0);
                    expect(lid.showMomentForTesting(l.moment), juce::String(l.name) + ": the sheet took the engine's capture");
                    shoot(juce::String("lid-") + l.name + "-live");
                    lid.showMomentForTesting(0.0);
                    now += 10000.0;
                }
                p->editBank([&](SequencerBank& b) { b = saved; }, false);
            }
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
            // The panel's etched circuit lit (D298): the bass drum, the snare and the closed hat struck, 30 ms on.
            editor->boardForTesting().struck((1u << 0) | (1u << 1) | (1u << 10), 50000.0);
            editor->boardForTesting().tick(50030.0);
            shoot("panel-struck");
            // The CPU section under the glass lit (D312): the clock's tick on step 6 and an edit written to the memory.
            editor->boardForTesting().sequencerState(-1, 1, 50000.0);
            editor->boardForTesting().sequencerState(5, 2, 50010.0);
            editor->boardForTesting().tick(50030.0);
            shoot("panel-cpu");
            // The circuit faint, as the options can set it for whom it distracts (D314).
            editor->boardForTesting().setCircuitLook(true, EtchedBoard::CircuitShow::faint);
            shoot("panel-faint");
            // Lift the Lid (D315): the knobs and the glass gone, the CPU board whole and lit, a step playing.
            editor->liftLid(true);
            editor->boardForTesting().struck((1u << 0) | (1u << 1) | (1u << 10), 52000.0);
            editor->boardForTesting().sequencerState(-1, 3, 52000.0);
            editor->boardForTesting().sequencerState(6, 4, 52005.0);
            editor->boardForTesting().tick(52030.0);
            shoot("lid-lifted");
            editor->boardForTesting().sequencerState(-1, 4, 70000.0);
            editor->boardForTesting().tick(70000.0);
            shoot("lid-lifted-rest");
            // What opens (D319): each column framed faintly, the bass drum's bright under the mouse.
            editor->boardForTesting().hoverForTesting(1);
            shoot("lid-lifted-hover");
            editor->boardForTesting().hoverForTesting(-1);
            editor->liftLid(false);
            editor->boardForTesting().setCircuitLook(true, EtchedBoard::CircuitShow::full);
            setTheme(1); // the same in the Silver theme (D307)
            editor->boardForTesting().themeChanged();
            editor->boardForTesting().struck((1u << 0) | (1u << 1) | (1u << 10), 51000.0);
            editor->boardForTesting().tick(51030.0);
            shoot("panel-struck-silver");
            setTheme(0);
            editor->boardForTesting().themeChanged();
            editor->boardForTesting().tick(60000.0);
            // Pattern 3 picked while 1 plays: it waits for the bar line, and the status says when it comes.
            editor->selectPatternKey(2, false);
            editor->editPattern = 0;
            shoot("panel-queued");
            p->setPlaying(false);
            runUntil([&] { return ! p->isPlaying(); });
            p->setChain({ 0 });
            shoot("panel-stopped");
            editor->setSize(620, 300);
            shoot("panel-narrow");
            editor->setSize(1280, 800);
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
                editor->setSize(1280, 800);
            }
            expect(folder.getChildFile("lid.png").existsAsFile());
        }
    }
};

static EditorSnapshotTests editorSnapshotTests;
