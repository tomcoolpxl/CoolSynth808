#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Library.h"
#include "ui/Widgets.h"

/*
    The pattern library as a shelf (CoolSynth's D273, the user's choice of the three mockups): over the panel, a
    column of shelves (your own patterns, the classics by family, a decade), the list of what they hold with a small
    picture of each beat, and the chosen beat in full. Audition plays it in place of the pattern you are editing until
    you load it or stop; Load puts it in that pattern, one undo step. The built-in beats are kept as they are: Keep a
    copy puts one in your patterns, which you can rename or send to the Recycle Bin.
*/
namespace cs808
{
    class LibraryShelf final : public juce::Component, private juce::ListBoxModel
    {
    public:
        LibraryShelf();
        ~LibraryShelf() override;

        std::function<void(const LibraryEntry&)> onLoad;     // into the pattern being edited
        std::function<void(const LibraryEntry*)> onAudition; // in place of that pattern; null: back to yours
        std::function<void()> onClose;
        // The beat sounding in an audition, asked of the editor, which alone holds it (D354: the shelf's own copy fell
        // out of step with it); empty when none is.
        std::function<juce::String()> auditionedKey;
        std::unique_ptr<juce::Button> closeKey; // the cross at the top right
        juce::String loadTarget = "01"; // what Load fills, as its button says it: a pattern as the panel names it, or "the fill"

        /** Reads the library again, keeping the chosen beat when it is still there. */
        void reload();
        /** The audition has ended elsewhere (the editor took the pattern back). */
        void auditionEnded();

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseUp(const juce::MouseEvent& e) override;
        bool keyPressed(const juce::KeyPress& k) override;
        void focusGained(FocusChangeType) override { list.grabKeyboardFocus(); } // the arrows walk the list

        // ---- Tests --------------------------------------------------------------------------------------------
        /** A shelf ("all", "mine", a family's name), a search and a decade (0: any), as if clicked and typed. */
        void showForTesting(const juce::String& shelf, const juce::String& searchText = {}, int decadeShown = 0);
        void selectForTesting(int row);
        [[nodiscard]] int shownCount() const noexcept { return static_cast<int>(shown.size()); }
        [[nodiscard]] const LibraryEntry* selectedEntry() const noexcept;
        [[nodiscard]] juce::String auditioningKey() const { return auditionedKey ? auditionedKey() : juce::String(); }
        [[nodiscard]] bool isAuditioning() const { return auditioningKey().isNotEmpty(); }
        void toggleAudition();
        void keepCopy();

    private:
        struct SideRow
        {
            juce::String key, label;
            int count = 0;
            bool heading = false;
            juce::Rectangle<float> area;
        };

        [[nodiscard]] juce::Rectangle<float> card() const;
        [[nodiscard]] juce::Rectangle<float> sideArea() const;
        [[nodiscard]] juce::Rectangle<float> detailArea() const;
        [[nodiscard]] bool matches(const LibraryEntry& e) const;
        void refilter();
        void buildSide();
        void layoutDetail();
        void select(const juce::String& key);
        void renameSelected();
        void recycleSelected();
        [[nodiscard]] const SequencerPattern& partPattern(const LibraryEntry& e, int part) const;
        [[nodiscard]] bool partUsesB(const LibraryEntry& e, int part) const;

        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
        void selectedRowsChanged(int lastRowSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) override;
        void returnKeyPressed(int lastRowSelected) override;

        std::vector<LibraryEntry> all;
        std::vector<int> shown; // indices into all
        juce::String shelf = "all";
        int decade = 0;
        std::vector<int> decades;
        juce::String selectedKey, status;
        int partShown = 0;
        bool bShown = false;

        juce::TextEditor search;
        juce::ListBox list;
        Chip audition, load, keep, rename, recycle;
        std::vector<std::unique_ptr<Chip>> partChips;
        std::vector<SideRow> side;
        std::vector<std::pair<int, juce::Rectangle<float>>> decadeChips;
    };
}
