#pragma once

#include <array>
#include <memory>
#include <optional>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "Library.h"
#include "Processor.h"
#include "ui/Widgets.h"

/*
    The panel (D264): a column of knobs per drum, the strip, the row of sixteen step keys with the transport, and the
    lanes, one row per drum, opening below it; the step inspector; Under the Lid as the second view. Drawn at a base
    size and scaled to the window. The mockup in CoolSynth's repository (docs/design/mockups/coolsynth-808.html) is
    the reference for its look.

    Lanes are counted as the panel shows them: 0 the accent, 1 to 11 the drums.
*/
namespace cs808
{
    class LidView;
    class LibraryPanel;
    class Controls;
    class ControllerPanel;

    class Editor final : public juce::AudioProcessorEditor,
                         public juce::DragAndDropContainer,
                         private juce::Timer
    {
    public:
        explicit Editor(Processor& p);
        ~Editor() override;

        void paint(juce::Graphics& g) override;
        void resized() override;
        bool keyPressed(const juce::KeyPress& key) override;
        void mouseDrag(const juce::MouseEvent& e) override;

        static constexpr int baseWidth = 1240;

        // ---- What the panel edits (used by its parts) ---------------------------------------------------------
        Processor& processor;
        int selectedLane = 1;   // 0 the accent
        int editPattern = 0;    // -1 the fill
        bool editB = false;
        int selectedStep = -1;
        bool patternMode = false;
        bool lanesOpen = true;
        bool lidView = false;
        int firstChainKey = -1; // the pattern key held for a chain
        std::array<double, 12> ledUntil {}; // per panel lane: when its light goes out (ms)

        [[nodiscard]] const SequencerPattern& patternShown() const;
        [[nodiscard]] const SequencerLane& laneShown(int panelLane) const;
        void editLane(int panelLane, const std::function<void(SequencerLane&)>& edit, bool undoable = true);
        [[nodiscard]] int playheadStep(int panelLane) const;
        void refresh();
        void layoutForLanes();
        [[nodiscard]] LidView& lidForTesting() noexcept { return *lid; }
        /** A pattern key pressed: that pattern plays next and is edited; `extend` makes a chain from the key held. */
        void selectPatternKey(int index, bool extend);
        [[nodiscard]] Controls& controlsForTesting() noexcept { return *controls; }
        /** The controller setup over the panel (the header's Controller). */
        void openControllerSetup();

    private:
        void timerCallback() override;
        void buildHeader();
        void buildSound();
        void buildTransport();
        void buildInspector();
        void showFileMenu();
        void showThemeMenu();
        void showBankMenu();
        void openLibrary();

        void newProject();
        void openProject();
        void saveProject(bool askName);
        void savePatternToLibrary();
        void loadLibraryEntry(const LibraryEntry& e);
        void startMidiDrag();

        class Content;
        class StepKeys;
        class LaneRows;
        std::unique_ptr<Content> content;
        std::unique_ptr<StepKeys> stepKeys;
        std::unique_ptr<LaneRows> laneRows;
        std::unique_ptr<LidView> lid;
        std::unique_ptr<LibraryPanel> library;
        std::unique_ptr<Controls> controls; // a MIDI controller's knobs, faders and pads (D268)
        std::unique_ptr<ControllerPanel> controllerPanel;

        juce::OwnedArray<juce::Component> parts; // every widget, owned here
        std::optional<SequencerPattern> clipboard;
        std::unique_ptr<juce::FileChooser> chooser;
        std::unique_ptr<juce::PropertiesFile> settings;
        int themeIndex = 0;
        bool midiDragging = false;

        template <typename T> T& add(juce::Component& parent)
        {
            auto* c = new T();
            parts.add(c);
            parent.addAndMakeVisible(c);
            return *c;
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
    };
}
