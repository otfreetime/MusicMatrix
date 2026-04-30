#include "CustomLookAndFeel.h"

namespace myapp::ui
{
CustomLookAndFeel::CustomLookAndFeel()
{
    // Window / general background
    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (colBackground));

    // Labels
    setColour (juce::Label::textColourId,            juce::Colour (colText));
    setColour (juce::Label::backgroundColourId,      juce::Colours::transparentBlack);

    // Text buttons
    setColour (juce::TextButton::buttonColourId,     juce::Colour (colSurface));
    setColour (juce::TextButton::buttonOnColourId,   juce::Colour (colAmber));
    setColour (juce::TextButton::textColourOffId,    juce::Colour (colText));
    setColour (juce::TextButton::textColourOnId,     juce::Colour (colBackground));

    // ComboBox
    setColour (juce::ComboBox::backgroundColourId,   juce::Colour (colSurface));
    setColour (juce::ComboBox::textColourId,         juce::Colour (colText));
    setColour (juce::ComboBox::outlineColourId,      juce::Colour (colBorder));
    setColour (juce::ComboBox::arrowColourId,        juce::Colour (colAmber));
    setColour (juce::ComboBox::focusedOutlineColourId, juce::Colour (colAmber));

    // PopupMenu
    setColour (juce::PopupMenu::backgroundColourId,           juce::Colour (colSurface));
    setColour (juce::PopupMenu::textColourId,                 juce::Colour (colText));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (colAmber));
    setColour (juce::PopupMenu::highlightedTextColourId,      juce::Colour (colBackground));
}

void CustomLookAndFeel::drawLinearSlider (juce::Graphics& g,
                                          int x,
                                          int y,
                                          int width,
                                          int height,
                                          float sliderPos,
                                          float minSliderPos,
                                          float maxSliderPos,
                                          const juce::Slider::SliderStyle style,
                                          juce::Slider& slider)
{
    juce::ignoreUnused (minSliderPos, maxSliderPos, style, slider);

    const auto track = juce::Rectangle<float> ((float) x,
                                               (float) y + (float) height * 0.45f,
                                               (float) width,
                                               (float) height * 0.10f);
    g.setColour (juce::Colour (0xFF2A2A2A));
    g.fillRoundedRectangle (track, 3.0f);

    g.setColour (juce::Colour (0xFFFFBF00));
    g.fillRoundedRectangle (track.withWidth (juce::jlimit (0.0f, (float) width, sliderPos - (float) x)), 3.0f);

    g.setColour (juce::Colours::white);
    g.fillEllipse (sliderPos - 5.0f, (float) y + (float) height * 0.5f - 5.0f, 10.0f, 10.0f);
}

void CustomLookAndFeel::drawToggleButton (juce::Graphics& g,
                                          juce::ToggleButton& button,
                                          bool shouldDrawButtonAsHighlighted,
                                          bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);
    auto active = button.getToggleState();

    g.setColour (active ? juce::Colour (0xFFFFBF00) : juce::Colour (0xFF3A3A3A));
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);

    const auto knobSize = bounds.getHeight() - 4.0f;
    const auto knobX = active ? bounds.getRight() - knobSize - 2.0f : bounds.getX() + 2.0f;
    g.setColour (juce::Colours::white);
    g.fillEllipse (knobX, bounds.getY() + 2.0f, knobSize, knobSize);
}

void CustomLookAndFeel::drawButtonBackground (juce::Graphics& g,
                                              juce::Button& button,
                                              const juce::Colour& /*backgroundColour*/,
                                              bool isHighlighted,
                                              bool isDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();

    juce::Colour fill = juce::Colour (colSurface);
    if (on)        fill = juce::Colour (colAmber);
    if (isHighlighted) fill = fill.brighter (0.12f);
    if (isDown)        fill = fill.darker  (0.15f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (on ? juce::Colour (colAmberDim) : juce::Colour (colBorder));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
}

void CustomLookAndFeel::drawButtonText (juce::Graphics& g,
                                        juce::TextButton& button,
                                        bool /*isHighlighted*/,
                                        bool /*isDown*/)
{
    const bool on = button.getToggleState();
    g.setColour (on ? juce::Colour (colBackground) : juce::Colour (colText));
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::plain)));
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().reduced (6, 2),
                      juce::Justification::centred, 1);
}

void CustomLookAndFeel::drawComboBox (juce::Graphics& g,
                                      int width, int height,
                                      bool isButtonDown,
                                      int buttonX, int buttonY,
                                      int buttonW, int buttonH,
                                      juce::ComboBox& /*box*/)
{
    juce::ignoreUnused (buttonX, buttonY, buttonW, buttonH);
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (juce::Colour (colSurface));
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (isButtonDown ? juce::Colour (colAmber) : juce::Colour (colBorder));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    // Arrow chevron
    const float arrowCx = (float) width - 14.0f;
    const float arrowCy = (float) height * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (arrowCx - 4.0f, arrowCy - 2.5f,
                       arrowCx + 4.0f, arrowCy - 2.5f,
                       arrowCx,        arrowCy + 3.5f);
    g.setColour (juce::Colour (colAmber));
    g.fillPath (arrow);
}

void CustomLookAndFeel::drawPopupMenuItem (juce::Graphics& g,
                                           const juce::Rectangle<int>& area,
                                           bool isSeparator, bool isActive,
                                           bool isHighlighted, bool isTicked,
                                           bool /*hasSubMenu*/,
                                           const juce::String& text,
                                           const juce::String& /*shortcutKeyText*/,
                                           const juce::Drawable* /*icon*/,
                                           const juce::Colour* /*textColour*/)
{
    if (isSeparator)
    {
        g.setColour (juce::Colour (colBorder));
        g.fillRect (area.toFloat().reduced (8.0f, (float) area.getHeight() * 0.45f));
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (juce::Colour (colAmber));
        g.fillRoundedRectangle (area.toFloat().reduced (2.0f, 1.0f), 4.0f);
    }

    const juce::Colour textCol = (isHighlighted && isActive)
        ? juce::Colour (colBackground)
        : (isActive ? juce::Colour (colText) : juce::Colours::grey);

    g.setColour (textCol);
    g.setFont (juce::Font (juce::FontOptions (13.5f)));

    auto textArea = area.reduced (isTicked ? 22 : 8, 0);
    g.drawFittedText (text, textArea, juce::Justification::centredLeft, 1);

    if (isTicked)
    {
        g.setColour (juce::Colour (colAmber));
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (juce::CharPointer_UTF8 ("\xe2\x80\xa2"),
                    area.reduced (4, 0),
                    juce::Justification::centredLeft, false);
    }
}

void CustomLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.fillAll (label.findColour (juce::Label::backgroundColourId));

    if (! label.isBeingEdited())
    {
        g.setColour (label.findColour (juce::Label::textColourId));
        g.setFont (label.getFont());
        g.drawFittedText (label.getText(),
                          label.getLocalBounds().reduced (4, 1),
                          label.getJustificationType(),
                          juce::jmax (1, (int) ((float) label.getHeight() / label.getFont().getHeight())));
    }
}
} // namespace myapp::ui
