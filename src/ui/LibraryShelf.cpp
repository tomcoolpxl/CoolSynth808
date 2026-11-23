#include "LibraryShelf.h"

#include <algorithm>

#include "Kit.h"
#include "ui/Theme.h"

namespace cs808
{
    namespace
    {
        constexpr float sideWidth = 200.0f;
        constexpr float listMaxWidth = 380.0f;
        constexpr int rowHeight = 40;
        constexpr float chipHeight = 26.0f;

        float textWidth(const juce::String& text, float height)
        {
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText(uiFont(height), text, 0.0f, 0.0f);
            return glyphs.getBoundingBox(0, -1, true).getWidth();
        }

        void styleChip(Chip& chip, const juce::String& text)
        {
            chip.text = text;
            chip.fontHeight = 12.0f;
            chip.filledWhenOn = true;
        }
    }

    LibraryShelf::LibraryShelf()
    {
        setWantsKeyboardFocus(true);
        const auto& th = theme();
        search.setTextToShowWhenEmpty("Search names and records", th.dim);
        search.setFont(uiFont(12.5f));
        search.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        search.setColour(juce::TextEditor::textColourId, th.ink);
        search.setColour(juce::TextEditor::outlineColourId, th.line);
        search.setColour(juce::TextEditor::focusedOutlineColourId, th.dim);
        search.setColour(juce::TextEditor::highlightColourId, th.ink.withAlpha(0.25f));
        search.setColour(juce::CaretComponent::caretColourId, th.ink);
        search.setIndents(8, 5);
        search.onTextChange = [this] { refilter(); repaint(); };
        search.onEscapeKey = [this] { if (onClose) onClose(); };
        search.onReturnKey = [this] { list.grabKeyboardFocus(); };
        addAndMakeVisible(search);

        list.setModel(this);
        list.setRowHeight(rowHeight);
        list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setWantsKeyboardFocus(true);
        addAndMakeVisible(list);

        styleChip(audition, "Audition");
        audition.isOn = [this] { return isAuditioning(); };
        audition.onClick = [this](const juce::MouseEvent&) { toggleAudition(); };
        styleChip(load, "Load");
        load.isOn = [] { return true; };
        load.onClick = [this](const juce::MouseEvent&)
        {
            if (const auto* e = selectedEntry(); e != nullptr && onLoad)
                onLoad(*e);
        };
        styleChip(keep, "Keep a copy");
        keep.onClick = [this](const juce::MouseEvent&) { keepCopy(); };
        styleChip(rename, "Rename...");
        rename.onClick = [this](const juce::MouseEvent&) { renameSelected(); };
        styleChip(recycle, "Recycle Bin");
        recycle.onClick = [this](const juce::MouseEvent&) { recycleSelected(); };
        for (auto* chip : { &audition, &load, &keep, &rename, &recycle })
            addChildComponent(*chip);

        reload();
    }

    LibraryShelf::~LibraryShelf() { list.setModel(nullptr); }

    // ---- What is on the shelves -----------------------------------------------------------------------------------

    void LibraryShelf::reload()
    {
        all = builtInLibrary();
        for (auto& e : userLibrary())
            all.push_back(std::move(e));
        decades.clear();
        for (const auto& e : all)
            if (e.builtIn && e.year > 0)
                if (const int d = e.year / 10 * 10; std::find(decades.begin(), decades.end(), d) == decades.end())
                    decades.push_back(d);
        std::sort(decades.begin(), decades.end());
        buildSide();
        refilter();
        if (selectedEntry() == nullptr)
        {
            selectedKey = {};
            if (! shown.empty())
                select(libraryKey(all[static_cast<std::size_t>(shown.front())]));
        }
        layoutDetail();
        repaint();
    }

    void LibraryShelf::buildSide()
    {
        side.clear();
        const auto count = [this](const std::function<bool(const LibraryEntry&)>& f)
        {
            return static_cast<int>(std::count_if(all.begin(), all.end(), f));
        };
        side.push_back({ {}, "My patterns", 0, true, {} });
        side.push_back({ "mine", "All of mine", count([](const LibraryEntry& e) { return ! e.builtIn; }), false, {} });
        side.push_back({ {}, "Classics", 0, true, {} });
        side.push_back({ "all", "Every classic", count([](const LibraryEntry& e) { return e.builtIn; }), false, {} });
        for (const auto& family : libraryFamilies())
            if (const int n = count([&](const LibraryEntry& e) { return e.builtIn && familyOf(e.pattern.style) == family; }); n > 0)
                side.push_back({ family, family, n, false, {} });
        side.push_back({ {}, "Decade", 0, true, {} });
    }

    bool LibraryShelf::matches(const LibraryEntry& e) const
    {
        const auto text = search.getText().trim();
        if (text.isNotEmpty() && ! (e.pattern.name + " " + e.pattern.after).containsIgnoreCase(text))
            return false;
        if (decade != 0 && e.year / 10 * 10 != decade)
            return false;
        if (shelf == "mine")
            return ! e.builtIn;
        if (shelf == "all")
            return e.builtIn;
        return e.builtIn && familyOf(e.pattern.style) == shelf;
    }

    void LibraryShelf::refilter()
    {
        shown.clear();
        for (std::size_t i = 0; i < all.size(); ++i)
            if (matches(all[i]))
                shown.push_back(static_cast<int>(i));
        // The classics by year, yours by name (as userLibrary gives them).
        std::stable_sort(shown.begin(), shown.end(), [this](int a, int b)
        {
            const auto& x = all[static_cast<std::size_t>(a)];
            const auto& y = all[static_cast<std::size_t>(b)];
            if (x.builtIn != y.builtIn)
                return ! x.builtIn;
            return x.builtIn && x.year != y.year ? x.year < y.year : false;
        });
        list.updateContent();
        list.deselectAllRows();
        for (std::size_t r = 0; r < shown.size(); ++r)
            if (libraryKey(all[static_cast<std::size_t>(shown[r])]) == selectedKey)
            {
                list.selectRow(static_cast<int>(r), false, true);
                break;
            }
        list.repaint();
    }

    const LibraryEntry* LibraryShelf::selectedEntry() const noexcept
    {
        for (const auto& e : all)
            if (libraryKey(e) == selectedKey)
                return &e;
        return nullptr;
    }

    void LibraryShelf::select(const juce::String& key)
    {
        if (key == selectedKey)
            return;
        selectedKey = key;
        partShown = 0;
        bShown = false;
        status = {};
        // The list shows the choice however it was made (a click, the keys, a reload).
        for (std::size_t r = 0; r < shown.size(); ++r)
            if (libraryKey(all[static_cast<std::size_t>(shown[r])]) == key && list.getSelectedRow() != static_cast<int>(r))
            {
                list.selectRow(static_cast<int>(r), false, true);
                break;
            }
        // Listening follows the choice, as a browser's preview does.
        if (isAuditioning())
            if (const auto* e = selectedEntry(); e != nullptr && onAudition)
            {
                auditioningKey = key;
                onAudition(e);
            }
        layoutDetail();
        repaint();
    }

    const SequencerPattern& LibraryShelf::partPattern(const LibraryEntry& e, int part) const
    {
        if (part > 0 && part <= static_cast<int>(e.pattern.moreParts.size()))
            return e.pattern.moreParts[static_cast<std::size_t>(part - 1)].pattern;
        return e.pattern.pattern;
    }

    bool LibraryShelf::partUsesB(const LibraryEntry& e, int part) const
    {
        if (part > 0 && part <= static_cast<int>(e.pattern.moreParts.size()))
            return e.pattern.moreParts[static_cast<std::size_t>(part - 1)].usesB;
        return e.pattern.usesB;
    }

    // ---- Doing things with a beat ---------------------------------------------------------------------------------

    void LibraryShelf::toggleAudition()
    {
        if (isAuditioning())
        {
            auditioningKey = {};
            if (onAudition)
                onAudition(nullptr);
        }
        else if (const auto* e = selectedEntry(); e != nullptr && onAudition)
        {
            auditioningKey = selectedKey;
            onAudition(e);
        }
        layoutDetail();
        repaint();
    }

    void LibraryShelf::auditionEnded()
    {
        auditioningKey = {};
        layoutDetail();
        repaint();
    }

    void LibraryShelf::keepCopy()
    {
        const auto* e = selectedEntry();
        if (e == nullptr)
            return;
        const auto file = saveUserPattern(e->pattern);
        if (file == juce::File())
        {
            status = "Cannot write to " + userPatternsFolder().getFullPathName();
            repaint();
            return;
        }
        const auto name = e->pattern.name;
        reload();
        status = "Kept in your patterns as " + name + ".";
        repaint();
    }

    void LibraryShelf::renameSelected()
    {
        const auto* e = selectedEntry();
        if (e == nullptr || e->builtIn)
            return;
        auto* window = new juce::AlertWindow("Rename the pattern", "A new name for it:", juce::MessageBoxIconType::NoIcon);
        window->addTextEditor("name", e->pattern.name);
        window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
        window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        const auto file = e->file;
        window->enterModalState(true, juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<LibraryShelf>(this), window, file](int r)
            {
                if (r != 1 || safe == nullptr)
                    return;
                const auto renamed = renameUserPattern(file, window->getTextEditorContents("name"));
                if (renamed == juce::File())
                {
                    safe->status = "Cannot rename it: is the name empty, or the file open elsewhere?";
                    safe->repaint();
                    return;
                }
                safe->selectedKey = "mine:" + renamed.getFileNameWithoutExtension();
                safe->reload();
                safe->status = "Renamed.";
                safe->repaint();
            }), true);
    }

    void LibraryShelf::recycleSelected()
    {
        const auto* e = selectedEntry();
        if (e == nullptr || e->builtIn)
            return;
        const auto name = e->pattern.name;
        if (isAuditioning())
            toggleAudition();
        if (! e->file.moveToTrash())
        {
            status = "Cannot move " + name + " to the Recycle Bin.";
            repaint();
            return;
        }
        selectedKey = {};
        reload();
        status = name + " is in the Recycle Bin.";
        repaint();
    }

    // ---- Tests ----------------------------------------------------------------------------------------------------

    void LibraryShelf::showForTesting(const juce::String& shelfKey, const juce::String& searchText, int decadeShown)
    {
        shelf = shelfKey;
        decade = decadeShown;
        search.setText(searchText, false);
        refilter();
    }

    void LibraryShelf::selectForTesting(int row)
    {
        if (row >= 0 && row < static_cast<int>(shown.size()))
            select(libraryKey(all[static_cast<std::size_t>(shown[static_cast<std::size_t>(row)])]));
    }

    // ---- Layout and drawing ---------------------------------------------------------------------------------------

    juce::Rectangle<float> LibraryShelf::card() const { return getLocalBounds().toFloat().reduced(40.0f, 28.0f); }

    juce::Rectangle<float> LibraryShelf::sideArea() const
    {
        return card().reduced(20.0f, 14.0f).withTrimmedTop(58.0f).removeFromLeft(sideWidth);
    }

    juce::Rectangle<float> LibraryShelf::detailArea() const
    {
        auto body = card().reduced(20.0f, 14.0f).withTrimmedTop(58.0f);
        body.removeFromLeft(sideWidth + 12.0f);
        body.removeFromLeft(std::min(listMaxWidth, body.getWidth() * 0.45f) + 18.0f);
        return body;
    }

    void LibraryShelf::resized()
    {
        auto body = card().reduced(20.0f, 14.0f).withTrimmedTop(58.0f);
        auto sideBox = body.removeFromLeft(sideWidth);
        body.removeFromLeft(12.0f);
        list.setBounds(body.removeFromLeft(std::min(listMaxWidth, body.getWidth() * 0.45f)).toNearestInt());
        search.setBounds(sideBox.removeFromTop(28.0f).toNearestInt());

        // The shelves under the search, then the decades as small chips.
        float y = sideBox.getY() + 10.0f;
        for (auto& row : side)
        {
            const float h = row.heading ? 24.0f : 22.0f;
            row.area = { sideBox.getX(), y, sideBox.getWidth(), h };
            y += h;
        }
        decadeChips.clear();
        float x = sideBox.getX() + 4.0f;
        for (const int d : decades)
        {
            if (x + 40.0f > sideBox.getRight())
            {
                x = sideBox.getX() + 4.0f;
                y += 26.0f;
            }
            decadeChips.push_back({ d, { x, y + 2.0f, 40.0f, 20.0f } });
            x += 44.0f;
        }
        layoutDetail();
    }

    void LibraryShelf::layoutDetail()
    {
        const auto* e = selectedEntry();
        for (auto* chip : { &audition, &load, &keep, &rename, &recycle })
            chip->setVisible(false);
        partChips.clear();
        if (e == nullptr || getWidth() <= 0)
            return;

        audition.text = isAuditioning() ? "Stop audition" : "Audition";
        load.text = "Load into " + loadTarget;
        std::vector<Chip*> actions { &audition, &load };
        if (e->builtIn)
            actions.push_back(&keep);
        else
        {
            actions.push_back(&rename);
            actions.push_back(&recycle);
        }

        const auto area = detailArea();
        float x = area.getX(), y = area.getY() + 92.0f;
        for (auto* chip : actions)
        {
            const float w = textWidth(chip->text, chip->fontHeight) + 26.0f;
            if (x + w > area.getRight() && x > area.getX())
            {
                x = area.getX();
                y += chipHeight + 6.0f;
            }
            chip->setBounds(juce::Rectangle<float>(x, y, w, chipHeight).toNearestInt());
            chip->setVisible(true);
            x += w + 6.0f;
        }
        y += chipHeight + 10.0f;

        // A beat of several parts: which part the grid shows; a part with a B: which half.
        x = area.getX();
        const int parts = 1 + static_cast<int>(e->pattern.moreParts.size());
        const auto addChip = [&](const juce::String& text, std::function<bool()> on, std::function<void()> act)
        {
            auto chip = std::make_unique<Chip>();
            styleChip(*chip, text);
            chip->fontHeight = 11.0f;
            chip->filledWhenOn = false;
            chip->isOn = std::move(on);
            chip->onClick = [act = std::move(act)](const juce::MouseEvent&) { act(); };
            const float w = textWidth(text, 11.0f) + 20.0f;
            chip->setBounds(juce::Rectangle<float>(x, y, w, 22.0f).toNearestInt());
            x += w + 5.0f;
            addAndMakeVisible(*chip);
            partChips.push_back(std::move(chip));
        };
        if (parts > 1)
            for (int i = 0; i < parts; ++i)
                addChip("Part " + juce::String(i + 1), [this, i] { return partShown == i; },
                        [this, i] { partShown = i; bShown = false; layoutDetail(); repaint(); });
        if (partUsesB(*e, partShown))
        {
            if (parts > 1)
                x += 10.0f;
            addChip("A", [this] { return ! bShown; }, [this] { bShown = false; repaint(); });
            addChip("B", [this] { return bShown; }, [this] { bShown = true; repaint(); });
        }
    }

    void LibraryShelf::paint(juce::Graphics& g)
    {
        const auto& th = theme();
        g.setColour(th.desk.withAlpha(0.6f));
        g.fillAll();
        const auto box = card();
        g.setColour(th.panel);
        g.fillRoundedRectangle(box, 10.0f);
        g.setColour(th.ink);
        g.drawRoundedRectangle(box, 10.0f, 1.0f);

        auto head = box.reduced(20.0f, 14.0f);
        g.setFont(uiFont(18.0f, true));
        g.drawText("Library", head.removeFromTop(24.0f), juce::Justification::centredLeft);
        g.setColour(th.dim);
        g.setFont(uiFont(11.0f));
        g.drawText("Choose a shelf, then a beat. Audition plays it in place of your pattern until you load it or stop; "
                   "double-click or Enter loads it, Space auditions, Esc closes.",
                   head.removeFromTop(16.0f), juce::Justification::centredLeft);
        if (status.isNotEmpty())
        {
            g.setColour(th.ink);
            g.drawText(status, head.removeFromTop(16.0f), juce::Justification::centredLeft);
        }

        // The shelves.
        for (const auto& row : side)
        {
            if (row.heading)
            {
                g.setColour(th.dim);
                g.setFont(uiFont(10.0f, true));
                g.drawText(row.label.toUpperCase(), row.area.withTrimmedLeft(6.0f).withTrimmedTop(6.0f), juce::Justification::centredLeft);
                continue;
            }
            if (row.key == shelf)
            {
                g.setColour(th.ink.withAlpha(0.1f));
                g.fillRoundedRectangle(row.area.reduced(0.0f, 1.0f), 4.0f);
            }
            g.setColour(th.ink);
            g.setFont(uiFont(12.0f));
            g.drawText(row.label, row.area.reduced(8.0f, 0.0f), juce::Justification::centredLeft);
            g.setColour(th.dim);
            g.drawText(juce::String(row.count), row.area.reduced(8.0f, 0.0f), juce::Justification::centredRight);
        }
        for (const auto& [d, area] : decadeChips)
        {
            const bool on = d == decade;
            if (on)
            {
                g.setColour(th.ink);
                g.fillRoundedRectangle(area, 4.0f);
            }
            g.setColour(on ? th.ink : th.line);
            g.drawRoundedRectangle(area, 4.0f, 1.0f);
            g.setColour(on ? th.panel : th.dim);
            g.setFont(uiFont(11.0f));
            g.drawText(juce::String(d % 100).paddedLeft('0', 2) + "s", area, juce::Justification::centred);
        }

        // A line between the columns.
        g.setColour(th.line);
        const auto listBox = list.getBounds().toFloat();
        g.drawVerticalLine(juce::roundToInt(listBox.getX() - 7.0f), listBox.getY(), listBox.getBottom());
        g.drawVerticalLine(juce::roundToInt(listBox.getRight() + 8.0f), listBox.getY(), listBox.getBottom());
        if (shown.empty())
        {
            g.setColour(th.dim);
            g.setFont(uiFont(12.0f));
            g.drawText(shelf == "mine" && search.isEmpty() ? "None yet: Keep on the panel puts the pattern you are editing here."
                                                           : "Nothing matches.",
                       listBox.withHeight(40.0f).reduced(10.0f, 0.0f), juce::Justification::centredLeft);
        }

        // The chosen beat in full.
        const auto* e = selectedEntry();
        if (e == nullptr)
            return;
        auto area = detailArea();
        g.setColour(th.dim);
        g.setFont(uiFont(10.0f, true));
        g.drawText((e->builtIn ? familyOf(e->pattern.style) + " / " + juce::String(e->year) : juce::String("My patterns")).toUpperCase(),
                   area.removeFromTop(14.0f), juce::Justification::centredLeft);
        g.setColour(th.ink);
        g.setFont(uiFont(19.0f, true));
        g.drawFittedText(e->pattern.name, area.removeFromTop(26.0f).toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour(th.dim);
        g.setFont(uiFont(11.5f));
        if (e->pattern.after.isNotEmpty())
            g.drawFittedText("After " + e->pattern.after, area.removeFromTop(18.0f).toNearestInt(), juce::Justification::centredLeft, 1);
        else
            area.removeFromTop(18.0f);
        juce::String stats;
        stats << juce::String(e->pattern.play.tempo, 0) << " BPM, swing " << juce::roundToInt(e->pattern.play.swing * 100.0f) << " %";
        const int bars = (1 + static_cast<int>(e->pattern.moreParts.size())) * (e->pattern.usesB ? 2 : 1);
        stats << ", " << bars << (bars == 1 ? " bar" : " bars");
        if (e->confidence.isNotEmpty() && e->confidence != "style") // a starter beat follows no record
            stats << ". Transcription: " << e->confidence << " confidence";
        g.setColour(th.ink);
        g.drawFittedText(stats, area.removeFromTop(18.0f).toNearestInt(), juce::Justification::centredLeft, 1);

        // The grid: the accent and every drum, sixteen steps, the beat's four groups shaded.
        int below = 0;
        for (const auto* chip : { &audition, &load, &keep, &rename, &recycle })
            if (chip->isVisible())
                below = std::max(below, chip->getBottom());
        for (const auto& chip : partChips)
            below = std::max(below, chip->getBottom());
        const float top = static_cast<float>(below) + 12.0f;
        const auto grid = detailArea().withTop(top);
        const auto& pattern = partPattern(*e, partShown);
        const float labelW = 26.0f;
        const float cell = std::min(22.0f, (grid.getWidth() - labelW) / 16.0f);
        const float rowH = 13.0f, gap = 3.0f;
        for (int row = 0; row <= laneCount; ++row)
        {
            const float y = grid.getY() + static_cast<float>(row) * (rowH + gap);
            const auto& lane = row == 0 ? pattern.accent : pattern.lanes[static_cast<std::size_t>(row - 1)];
            juce::String code = "AC";
            if (row > 0)
            {
                const auto& inst = instruments[static_cast<std::size_t>(row - 1)];
                code = e->pattern.alt[static_cast<std::size_t>(row - 1)] && inst.altCode != nullptr ? inst.altCode : inst.code;
            }
            g.setColour(th.dim);
            g.setFont(uiFont(9.5f, true));
            g.drawText(code, juce::Rectangle<float>(grid.getX(), y, labelW, rowH), juce::Justification::centredLeft);
            const auto& steps = bShown ? lane.b : lane.a;
            for (int s = 0; s < 16; ++s)
            {
                const juce::Rectangle<float> c(grid.getX() + labelW + static_cast<float>(s) * cell, y, cell - 2.0f, rowH);
                const bool on = steps[static_cast<std::size_t>(s)].on && s < lane.length;
                g.setColour(on ? (row == 0 ? th.on : th.ink) : th.group[static_cast<std::size_t>(s / 4)]);
                g.fillRoundedRectangle(c, 2.5f);
            }
        }
        const float notesTop = grid.getY() + static_cast<float>(laneCount + 1) * (rowH + gap) + 10.0f;
        if (e->pattern.notes.isNotEmpty() && notesTop < grid.getBottom() - 14.0f)
        {
            g.setColour(th.dim);
            g.setFont(uiFont(11.0f));
            g.drawFittedText(e->pattern.notes, grid.withTop(notesTop).toNearestInt(), juce::Justification::topLeft, 12);
        }
    }

    // ---- The list -------------------------------------------------------------------------------------------------

    int LibraryShelf::getNumRows() { return static_cast<int>(shown.size()); }

    void LibraryShelf::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (row < 0 || row >= static_cast<int>(shown.size()))
            return;
        const auto& th = theme();
        const auto& e = all[static_cast<std::size_t>(shown[static_cast<std::size_t>(row)])];
        if (selected)
        {
            g.setColour(th.ink.withAlpha(0.11f));
            g.fillRoundedRectangle(0.0f, 1.0f, static_cast<float>(width), static_cast<float>(height - 2), 4.0f);
        }
        if (libraryKey(e) == auditioningKey)
        {
            g.setColour(th.on);
            g.fillRoundedRectangle(2.0f, 8.0f, 3.0f, static_cast<float>(height - 16), 1.5f);
        }
        const int thumbW = 16 * 5, right = 58;
        const int textW = width - thumbW - right - 24;
        g.setColour(th.ink);
        g.setFont(uiFont(12.5f, true));
        g.drawText(e.pattern.name, 10, 3, textW, 18, juce::Justification::centredLeft);
        g.setColour(th.dim);
        g.setFont(uiFont(10.5f));
        g.drawText(e.builtIn ? e.pattern.after.upToFirstOccurrenceOf(",", false, false) : juce::String("My patterns"),
                   10, 20, textW, 16, juce::Justification::centredLeft);

        // A small picture of the beat: every drum's sixteen steps of its first bar.
        const int tx = width - thumbW - right, ty = (height - laneCount * 3) / 2;
        for (int lane = 0; lane < laneCount; ++lane)
        {
            const auto& l = e.pattern.pattern.lanes[static_cast<std::size_t>(lane)];
            for (int s = 0; s < 16; ++s)
            {
                g.setColour(l.a[static_cast<std::size_t>(s)].on ? th.ink : th.ink.withAlpha(0.14f));
                g.fillRect(tx + s * 5, ty + lane * 3, 4, 2);
            }
        }
        g.setColour(th.dim);
        g.setFont(uiFont(10.5f));
        g.drawText(juce::String(e.pattern.play.tempo, 0) + " BPM", width - right, 0, right - 6, height, juce::Justification::centredRight);
    }

    void LibraryShelf::selectedRowsChanged(int lastRowSelected)
    {
        if (lastRowSelected >= 0 && lastRowSelected < static_cast<int>(shown.size()))
            select(libraryKey(all[static_cast<std::size_t>(shown[static_cast<std::size_t>(lastRowSelected)])]));
    }

    void LibraryShelf::listBoxItemClicked(int row, const juce::MouseEvent& e)
    {
        if (! e.mods.isPopupMenu() || row < 0 || row >= static_cast<int>(shown.size()))
            return;
        list.selectRow(row);
        if (all[static_cast<std::size_t>(shown[static_cast<std::size_t>(row)])].builtIn)
            return;
        juce::PopupMenu m;
        m.addItem(1, "Rename...");
        m.addItem(2, "Move to the Recycle Bin");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                        [safe = juce::Component::SafePointer<LibraryShelf>(this)](int r)
                        {
                            if (safe == nullptr)
                                return;
                            if (r == 1)
                                safe->renameSelected();
                            else if (r == 2)
                                safe->recycleSelected();
                        });
    }

    void LibraryShelf::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
    {
        if (row >= 0 && row < static_cast<int>(shown.size()) && onLoad)
            onLoad(all[static_cast<std::size_t>(shown[static_cast<std::size_t>(row)])]);
    }

    void LibraryShelf::returnKeyPressed(int lastRowSelected)
    {
        if (lastRowSelected >= 0 && lastRowSelected < static_cast<int>(shown.size()) && onLoad)
            onLoad(all[static_cast<std::size_t>(shown[static_cast<std::size_t>(lastRowSelected)])]);
    }

    // ---- Mouse and keys -------------------------------------------------------------------------------------------

    void LibraryShelf::mouseUp(const juce::MouseEvent& e)
    {
        if (! card().contains(e.position))
        {
            if (onClose)
                onClose();
            return;
        }
        for (const auto& row : side)
            if (! row.heading && row.area.contains(e.position))
            {
                shelf = row.key;
                refilter();
                repaint();
                return;
            }
        for (const auto& [d, area] : decadeChips)
            if (area.contains(e.position))
            {
                decade = decade == d ? 0 : d;
                refilter();
                repaint();
                return;
            }
    }

    bool LibraryShelf::keyPressed(const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey && onClose)
        {
            onClose();
            return true;
        }
        if (k == juce::KeyPress::spaceKey)
        {
            toggleAudition();
            return true;
        }
        if (k == juce::KeyPress::returnKey)
        {
            if (const auto* e = selectedEntry(); e != nullptr && onLoad)
                onLoad(*e);
            return true;
        }
        return false;
    }
}
