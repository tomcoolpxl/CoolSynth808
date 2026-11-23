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
    class LibraryShelf;
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
        /** Edits a lane of the pattern being edited, or of `pattern` (-1 the fill) when given. */
        void editLane(int panelLane, const std::function<void(SequencerLane&)>& edit, bool undoable = true, int pattern = currentPattern);
        static constexpr int currentPattern = -1000;
        [[nodiscard]] int playheadStep(int panelLane) const;
        void refresh();
        void layoutForLanes();
        [[nodiscard]] LidView& lidForTesting() noexcept { return *lid; }
        /** A pattern key pressed: that pattern plays next and is edited; `extend` makes a chain from the key held. */
        void selectPatternKey(int index, bool extend);
        [[nodiscard]] Controls& controlsForTesting() noexcept { return *controls; }
        /** The controller setup over the panel (the header's Controller). */
        void openControllerSetup();
        /** The library or controller overlay closes after the callback that asked has returned. */
        void closeOverlaysLater();
        /** The library's shelf over the panel (the header's Library), and while it is open, the shelf. */
        void openLibrary();
        [[nodiscard]] LibraryShelf* libraryForTesting() noexcept { return library.get(); }
        /** A library beat played in place of the pattern being edited, nothing written to undo (D272); stopAudition
            gives the project back as it was. Playing starts if it was stopped, and stops again with the audition. */
        void startAudition(const LibraryEntry& e);
        void stopAudition();
        [[nodiscard]] bool isAuditioning() const noexcept { return auditionSaved != nullptr; }

    private:
        void timerCallback() override;
        void buildHeader();
        void buildSound();
        void buildTransport();
        void buildInspector();
        void showFileMenu();
        void showThemeMenu();
        void showBankMenu();

        void newProject();
        void openProject();
        void saveProject(bool askName);
        void savePatternToLibrary();
        void loadLibraryEntry(const LibraryEntry& e, bool undoable = true);
        void startMidiDrag();
        void resetSelection();

        class Content;
        class StepKeys;
        class LaneRows;
        std::unique_ptr<Content> content;
        std::unique_ptr<StepKeys> stepKeys;
        std::unique_ptr<LaneRows> laneRows;
        std::unique_ptr<LidView> lid;
        std::unique_ptr<LibraryShelf> library;
        // An audition (D272): the project and the chain as they were, and whether the audition started playing.
        std::unique_ptr<Project> auditionSaved;
        std::vector<int> auditionChain;
        bool auditionStartedPlaying = false;
        std::unique_ptr<Controls> controls; // a MIDI controller's knobs, faders and pads (D268)
        std::unique_ptr<ControllerPanel> controllerPanel;

        juce::OwnedArray<juce::Component> parts; // every widget, owned here
        std::optional<SequencerPattern> clipboard;
        std::unique_ptr<juce::FileChooser> chooser;
        std::unique_ptr<juce::PropertiesFile> settings;
        int themeIndex = 0;
        bool midiDragging = false;
        std::uint64_t lastLook = 0; // what the panel showed when last drawn (timerCallback)
        int patternBeforeFill = 0;

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
