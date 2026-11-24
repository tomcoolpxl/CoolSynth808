#pragma once

#include <array>
#include <memory>
#include <optional>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "Library.h"
#include "Processor.h"
#include "ui/EtchedBoard.h"
#include "ui/Widgets.h"

/*
    The panel, as the fifth layout of the mockups (D292, CoolSynth's docs/design/mockups/coolsynth-808-layouts.html,
    "Etched with glass"): the board with a column of knobs per drum, the transport on a strip of smoked glass, and the
    sequencer on a pane of smoked glass below: the patterns, the chain, a row of step lights and the twelve lanes, the
    selected lane's and the pattern's tools at the right. Drawn at 1280 by 800 and scaled to the window, keeping its
    shape.

    Lanes are counted as the panel shows them: 0 the accent, 1 to 11 the drums.
*/
namespace cs808
{
    class LidView;
    class LibraryShelf;
    class Controls;
    class ControllerPanel;
    class EtchedBoard;
    class SequencerGlass;
    class TransportStrip;
    class OptionsWindow;

    class Editor final : public juce::AudioProcessorEditor,
                         public juce::DragAndDropContainer,
                         private juce::Timer,
                         private juce::ComponentListener
    {
    public:
        explicit Editor(Processor& p);
        ~Editor() override;

        void paint(juce::Graphics& g) override;
        void resized() override;
        bool keyPressed(const juce::KeyPress& key) override;
        void mouseDrag(const juce::MouseEvent& e) override;

        static constexpr int baseWidth = 1280, baseHeight = 800;
        static constexpr int panelLanes = laneCount + 1;

        // ---- What the panel edits (used by its parts) ---------------------------------------------------------
        Processor& processor;
        int selectedLane = 1;   // 0 the accent
        int editPattern = 0;    // -1 the fill
        bool editB = false;
        int selectedStep = -1;  // the step a controller's pad turned last; the panel no longer marks one (D292)
        // Under the Lid over the whole window. The panel has no key for it (D292); the snapshot tests show it.
        bool lidView = false;
        std::optional<SequencerPattern> clipboard;

        [[nodiscard]] const SequencerPattern& patternShown() const;
        [[nodiscard]] const SequencerLane& laneShown(int panelLane) const;
        /** Edits a lane of the pattern being edited, or of `pattern` (-1 the fill) when given. */
        void editLane(int panelLane, const std::function<void(SequencerLane&)>& edit, bool undoable = true, int pattern = currentPattern);
        static constexpr int currentPattern = -1000;
        [[nodiscard]] int playheadStep(int panelLane) const;
        /** Draws the panel again. */
        void refresh();
        /** Draws the glass again (the patterns, chain and lanes) and the transport strip, not the board (D306). */
        void refreshGlass();
        /** Lays the panel out for the window and brings what it shows up to date (the tests call it before a shot). */
        void layOut();
        [[nodiscard]] LidView& lidForTesting() noexcept { return *lid; }
        [[nodiscard]] EtchedBoard& boardForTesting() noexcept { return *board; }
        /** A pattern key pressed: that pattern plays next and is edited; `extend` makes a chain from the key held. */
        void selectPatternKey(int index, bool extend);
        [[nodiscard]] Controls& controlsForTesting() noexcept { return *controls; }
        /** The controller setup over the panel (the File menu's Controller setup). */
        void openControllerSetup();
        /** The library or controller overlay closes after the callback that asked has returned. */
        void closeOverlaysLater();
        /** The library's shelf over the panel (the Library key), and while it is open, the shelf. */
        void openLibrary();
        /** Opens or closes a schematic sheet of Under the Lid over the whole window (D308). */
        void showLid(bool show);
        /** Lifts the lid or lowers it (D315): the board alone, its CPU board whole; the Lid key's work. */
        void liftLid(bool on);
        [[nodiscard]] bool isLidLifted() const noexcept { return lidLifted; }
        /** With the lid lifted: a column's schematic (0 the master, 1 to 11 the drums, -1 the CPU) over the board. */
        void openSheet(int column);
        /** Opens or closes the options window (D311), the Options key's work. */
        void toggleOptions();
        [[nodiscard]] bool optionsOpen() const noexcept;
        [[nodiscard]] OptionsWindow* optionsForTesting() noexcept { return optionsWindow.get(); }
        /** A theme chosen in the options (D307, D311): the board, the glass and the windows' bars drawn again in it. */
        void chooseTheme(int index);
        /** The window's size in percent of the panel's 1280 by 800 (D311), kept between runs. */
        void chooseZoom(int percent);
        [[nodiscard]] LibraryShelf* libraryForTesting() noexcept { return library.get(); }
        /** A library beat played in place of the pattern being edited, nothing written to undo (D273); stopAudition
            gives the project back as it was. Playing starts if it was stopped, and stops again with the audition. While
            it plays, a beat comes in and goes on the next bar line, from its start (D274).
            Load of the beat that plays keeps it as it plays, one undo step. */
        void startAudition(const LibraryEntry& e);
        void stopAudition();
        [[nodiscard]] bool isAuditioning() const noexcept { return auditionSaved != nullptr; }

        // ---- For the panel's keys (D292) -----------------------------------------------------------------------
        void showFileMenu();
        /** The F slot: the fill is edited. */
        void editFill();
        /** The Fill key: the fill plays in the next bar. */
        void queueFill();
        [[nodiscard]] bool fillQueued() const noexcept { return fillAsked; }
        /** A tap of Tap: the tempo from the last taps, begun again after two seconds without one. */
        void tapTempo();
        void selectBankTab(int index);
        void copyPattern(int pattern);
        void pastePattern(int pattern);
        void clearPattern(int pattern);
        void savePatternToLibrary(int pattern);
        void addToChain(int pattern);
        /** Where the chain plays: its entry (-1 none, or stopped), and the step in the bar (-1 stopped). */
        [[nodiscard]] int chainEntryPlaying() const noexcept { return chainPlace; }
        [[nodiscard]] int stepInBar() const;
        /** The pattern that comes in at the next bar line when one was picked while another plays (-1 none). */
        [[nodiscard]] int queuedPattern() const;
        /** The Drag MIDI key: dragging it out drags the pattern edited out as a MIDI file. */
        void makeMidiDragSource(juce::Component& key);

    private:
        void timerCallback() override;
        /** Follows where the chain plays, one entry per bar, from the pattern the audio thread reports. */
        void followTransport();
        void buildColumns();
        void buildTransport();

        void newProject();
        void openProject();
        void saveProject(bool askName);
        void loadLibraryEntry(const LibraryEntry& e, bool undoable = true);
        /** Writes the beat from the pattern `into` on (the fill when below 0), its chain with it. */
        void writeLibraryEntry(const LibraryEntry& e, bool undoable, int into);
        void loadFromShelf(const LibraryEntry& e);
        void startMidiDrag();
        void resetSelection();
        /** What the controls cover on the board, for the copper to keep clear of. */
        void updateKeepOuts();

        class LidOverlay;
        std::unique_ptr<EtchedBoard> board; // the panel at its base size, scaled to the window
        std::unique_ptr<SequencerGlass> sequencer;
        std::unique_ptr<TransportStrip> transport;
        std::unique_ptr<LidOverlay> lidOverlay;
        std::unique_ptr<LidView> lid;
        std::unique_ptr<LibraryShelf> library;
        // An audition (D273): the project and the chain as they were, and whether the audition started playing.
        std::unique_ptr<Project> auditionSaved;
        std::vector<int> auditionChain;
        bool auditionStartedPlaying = false;
        juce::String auditionKey;
        std::unique_ptr<Controls> controls; // a MIDI controller's knobs, faders and pads (D268)
        std::unique_ptr<ControllerPanel> controllerPanel;

        juce::OwnedArray<juce::Component> parts; // every widget, owned here
        std::vector<KeepOut> columnKeepOuts;     // the knobs' circles, the words and the switches
        std::unique_ptr<juce::FileChooser> chooser;
        std::unique_ptr<juce::PropertiesFile> settings;
        int themeIndex = 0;
        int zoomPercent = 100;
        bool lidLifted = false;
        bool circuitAnimated = true;
        int circuitShow = 1; // full, faint (the default, the user), hidden (D314)
        void applyCircuitLook();
        std::unique_ptr<OptionsWindow> optionsWindow;
        juce::Component* watchedWindow = nullptr; // the window the options stay above
        void componentBroughtToFront(juce::Component& c) override;
        bool midiDragging = false;
        std::uint64_t lastLook = 0; // what the panel showed when last drawn (timerCallback)
        std::uint64_t lastBeat = 0; // and what moves with the beat
        std::uint64_t lastGlassLook = 0; // and what only the glass shows (D306)
        int patternBeforeFill = 0;
        int firstChainKey = -1; // the pattern key held for a chain
        int pickedPattern = -1; // picked while playing, waiting for its bar line (D303)
        bool fillAsked = false;
        std::vector<double> taps; // Tap's last presses (ms)
        // Following the chain (followTransport).
        int chainPlace = -1;
        int lastPatternHeard = -2;
        int lastStep = -1;
        std::vector<int> lastChain;

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
