#include <animalaudio/ui/ChoicePillRow.h>

namespace animalaudio::ui
{
ChoicePillRow::ChoicePillRow(juce::AudioParameterChoice& parameter, const Palette& palette, const juce::String& tooltip)
    : choice(parameter)
{
    for (int i = 0; i < choice.choices.size(); ++i)
    {
        auto pill = std::make_unique<PillButton>(choice.choices[i], palette);
        pill->setTooltip(tooltip);
        pill->onClick = [this, i]
        {
            show(i);
            attachment->setValueAsCompleteGesture(static_cast<float>(i));
        };
        addAndMakeVisible(*pill);
        pills.push_back(std::move(pill));
    }

    attachment = std::make_unique<juce::ParameterAttachment>(choice, [this](float) { show(choice.getIndex()); });
    attachment->sendInitialUpdate();
}

void ChoicePillRow::show(int index)
{
    index = juce::jlimit(0, getNumPills() - 1, index);
    for (int i = 0; i < getNumPills(); ++i)
        pills[static_cast<std::size_t>(i)]->setToggleState(i == index, juce::dontSendNotification);
}

void ChoicePillRow::setColumns(int newColumns)
{
    columns = juce::jmax(0, newColumns);
    resized();
}

void ChoicePillRow::resized()
{
    const int perRow = columns > 0 ? columns : getNumPills();
    const int rows = juce::jmax(1, (getNumPills() + perRow - 1) / juce::jmax(1, perRow));
    const int pillH = (getHeight() - kGap * (rows - 1)) / rows;
    for (int i = 0; i < getNumPills(); ++i)
        pills[static_cast<std::size_t>(i)]->setBounds((i % perRow) * (kPillWidth + kGap),
                                                      (i / perRow) * (pillH + kGap), kPillWidth, pillH);
}
} // namespace animalaudio::ui
