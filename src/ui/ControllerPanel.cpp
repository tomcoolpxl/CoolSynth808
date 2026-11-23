#include "ControllerPanel.h"

#include "Controls.h"
#include "ui/Theme.h"

namespace cs808
{
    namespace midi = coolsynth::midi;

    ControllerPanel::ControllerPanel(const void* o) : owner(o)
    {
        setWantsKeyboardFocus(true);
        addAndMakeVisible(list);
        list.setModel(this);
        list.setRowHeight(24);
        list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        const auto& setup = midi::ControllerSetup::instance();
        if (setup.noticeText().isNotEmpty())
            status = setup.noticeText();
        startTimerHz(10);
    }

    ControllerPanel::~ControllerPanel() { stopLearning(); }

    juce::Rectangle<float> ControllerPanel::card() const { return getLocalBounds().toFloat().reduced(60.0f, 30.0f); }

    juce::Rectangle<float> ControllerPanel::readyButton() const
    {
        const auto box = card().reduced(20.0f, 14.0f);
        return { box.getRight() - 290.0f, box.getY(), 170.0f, 26.0f };
    }

    juce::Rectangle<float> ControllerPanel::forgetButton() const
    {
        const auto box = card().reduced(20.0f, 14.0f);
        return { box.getRight() - 110.0f, box.getY(), 110.0f, 26.0f };
    }

    void ControllerPanel::paint(juce::Graphics& g)
    {
        const auto& th = theme();
        g.setColour(th.desk.withAlpha(0.6f));
        g.fillAll();
        const auto box = card();
        g.setColour(th.panel);
        g.fillRoundedRectangle(box, 10.0f);
        g.setColour(th.ink);
        g.drawRoundedRectangle(box, 10.0f, 1.0f);

        auto inner = box.reduced(20.0f, 14.0f);
        g.setFont(uiFont(18.0f, true));
        const auto& map = midi::ControllerSetup::instance().map();
        g.drawText(map.empty() ? juce::String("Controller") : "Controller: " + map.name, inner.removeFromTop(26.0f),
                   juce::Justification::centredLeft);
        g.setColour(th.dim);
        g.setFont(uiFont(11.0f));
        g.drawText("Click a row, then move that control on your controller. Right-click a row to forget it. Esc closes.",
                   inner.removeFromTop(18.0f), juce::Justification::centredLeft);
        g.setColour(th.ink);
        g.drawText(status, inner.removeFromTop(18.0f), juce::Justification::centredLeft);

        for (const auto& [area, text] : { std::pair { readyButton(), juce::String("Ready setups...") }, std::pair { forgetButton(), juce::String("Forget all") } })
        {
            g.setColour(th.ink.withAlpha(0.08f));
            g.fillRoundedRectangle(area, 6.0f);
            g.setColour(th.ink);
            g.drawRoundedRectangle(area, 6.0f, 1.0f);
            g.setFont(uiFont(12.0f));
            g.drawText(text, area, juce::Justification::centred);
        }
    }

    void ControllerPanel::resized() { list.setBounds(card().reduced(20.0f).withTrimmedTop(70.0f).toNearestInt()); }

    int ControllerPanel::getNumRows() { return midi::placeCount; }

    void ControllerPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (row < 0 || row >= midi::placeCount)
            return;
        const auto& th = theme();
        const auto place = static_cast<midi::Place>(row);
        if (selected || row == learningRow)
        {
            g.setColour(th.ink.withAlpha(row == learningRow ? 0.2f : 0.1f));
            g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 4.0f);
        }
        const int w1 = 150, w2 = width / 2 - 40;
        g.setColour(th.ink);
        g.setFont(uiFont(12.5f, true));
        g.drawText(placeTitle(place), 6, 0, w1 - 6, height, juce::Justification::centredLeft);
        g.setFont(uiFont(12.0f));
        g.drawText(placeUse(place), w1, 0, w2, height, juce::Justification::centredLeft);
        g.setColour(th.dim);
        juce::String linked = "Not set up";
        if (row == learningRow)
            linked = "Move it now...";
        else if (const auto* link = midi::ControllerSetup::instance().map().linkOf(place))
        {
            linked = link->control.describe();
            if (! midi::isButtonPlace(place))
                linked << ", " << midi::encodingName(link->encoding);
        }
        g.drawText(linked, w1 + w2 + 10, 0, width - w1 - w2 - 16, height, juce::Justification::centredLeft);
    }

    void ControllerPanel::listBoxItemClicked(int row, const juce::MouseEvent& e)
    {
        if (row < 0 || row >= midi::placeCount)
            return;
        if (e.mods.isPopupMenu())
        {
            auto& setup = midi::ControllerSetup::instance();
            auto map = setup.map();
            map.forget(static_cast<midi::Place>(row));
            if (map.name.isEmpty())
                map.name = "Your controller";
            setup.setMap(map);
            list.repaint();
            return;
        }
        learn(row);
    }

    void ControllerPanel::learn(int row)
    {
        stopLearning();
        auto& setup = midi::ControllerSetup::instance();
        const auto place = static_cast<midi::Place>(row);
        learningRow = row;
        status = midi::isButtonPlace(place) ? "Press the button or pad for " + placeTitle(place) + "."
                                            : "Turn the control for " + placeTitle(place) + " right, then left.";
        token = setup.learn(owner, midi::isButtonPlace(place) ? midi::ControllerSetup::Expect::press : midi::ControllerSetup::Expect::turn,
            [this, place](const midi::ControllerSetup::Heard& heard)
            {
                auto& s = midi::ControllerSetup::instance();
                auto map = s.map();
                if (map.name.isEmpty())
                    map.name = "Your controller";
                map.profileId = {};
                map.link(place, heard.control, heard.encoding);
                s.setMap(map);
                status = placeTitle(place) + ": " + heard.control.describe() + ".";
                learningRow = -1;
                list.repaint();
                repaint();
            },
            [this](const juce::String& text)
            {
                status = text;
                repaint();
            });
        list.repaint();
        repaint();
    }

    void ControllerPanel::stopLearning()
    {
        if (learningRow >= 0)
            midi::ControllerSetup::instance().stopLearning(token);
        learningRow = -1;
    }

    void ControllerPanel::timerCallback()
    {
        const auto& setup = midi::ControllerSetup::instance();
        if (learningRow >= 0 && ! setup.learningWith(token))
            learningRow = -1;
        if (setup.revision() != seenRevision)
        {
            seenRevision = setup.revision();
            list.repaint();
            repaint();
        }
    }

    void ControllerPanel::mouseUp(const juce::MouseEvent& e)
    {
        auto& setup = midi::ControllerSetup::instance();
        if (readyButton().contains(e.position))
        {
            juce::PopupMenu m;
            int id = 1;
            for (const auto& profile : midi::readyProfiles())
                m.addItem(id++, profile.name);
            m.showMenuAsync(juce::PopupMenu::Options(), [this](int r)
            {
                const auto& profiles = midi::readyProfiles();
                if (r < 1 || r > static_cast<int>(profiles.size()))
                    return;
                const auto& profile = profiles[static_cast<std::size_t>(r - 1)];
                midi::ControllerSetup::instance().setMap(profile.map);
                status = profile.note.isNotEmpty() ? profile.note : profile.name + " is set up.";
                repaint();
            });
            return;
        }
        if (forgetButton().contains(e.position))
        {
            stopLearning();
            setup.setMap({});
            status = "Nothing set up.";
            repaint();
            return;
        }
        if (! card().contains(e.position) && onClose)
            onClose();
    }

    bool ControllerPanel::keyPressed(const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey && onClose)
        {
            onClose();
            return true;
        }
        return false;
    }
}
