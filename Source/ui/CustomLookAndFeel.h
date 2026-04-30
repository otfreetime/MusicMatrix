#pragma once

#include <JuceHeader.h>

namespace myapp::ui
{
class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Palette
    static constexpr juce::uint32 colBackground = 0xFF121212;
    static constexpr juce::uint32 colSurface    = 0xFF1E1E1E;
    static constexpr juce::uint32 colAmber      = 0xFFFFBF00;
    static constexpr juce::uint32 colAmberDim   = 0xFF99720A;
    static constexpr juce::uint32 colText       = 0xFFE0E0E0;
    static constexpr juce::uint32 colBorder     = 0xFF333333;

    CustomLookAndFeel();

    // Slider
    void drawLinearSlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos,
                           float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style,
                           juce::Slider& slider) override;

    // Toggle
    void drawToggleButton (juce::Graphics& g,
                           juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    // Button
    void drawButtonBackground (juce::Graphics& g,
                               juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics& g,
                         juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    // ComboBox
    void drawComboBox (juce::Graphics& g,
                       int width, int height,
                       bool isButtonDown,
                       int buttonX, int buttonY,
                       int buttonW, int buttonH,
                       juce::ComboBox& box) override;

    void drawPopupMenuItem (juce::Graphics& g,
                            const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked,
                            bool hasSubMenu,
                            const juce::String& text,
                            const juce::String& shortcutKeyText,
                            const juce::Drawable* icon,
                            const juce::Colour* textColour) override;

    // Label
    void drawLabel (juce::Graphics& g, juce::Label& label) override;
};
} // namespace myapp::ui
