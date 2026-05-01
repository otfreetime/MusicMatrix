#pragma once

#include <JuceHeader.h>
#include <functional>

namespace myapp::ui
{
class PluginSubWindowContainer : public juce::Component
{
public:
    using ProgramSelectionCallback = std::function<void(int)>;
    using ResetCallback = std::function<void()>;

    PluginSubWindowContainer();
    ~PluginSubWindowContainer() override;

    void setEmbeddedWindowHandle (void* nativeWindowHandle);
    void clearEmbeddedWindow();
    juce::String getLastAttachStatus() const;
    
    /** Reposition the embedded window to match current container bounds */
    void repositionEmbeddedWindow();

    /** Populate the title-bar preset selector. selectedIndex is zero-based. */
    void setProgramList (const juce::StringArray& programs, int selectedIndex, bool enabled);

    /** Clear/disable the title-bar preset selector with placeholder text. */
    void clearProgramList (const juce::String& placeholderText);

    /** Update selected program in the title-bar preset selector (zero-based). */
    void setSelectedProgramIndex (int selectedIndex);

    /** Set callback fired when user changes preset in the title-bar selector. */
    void setProgramSelectionCallback (ProgramSelectionCallback callback);

    /** Set callback fired when user clicks Rest in the title bar. */
    void setResetCallback (ResetCallback callback);

    /** Called on the message thread when the embedded window is detected as destroyed. */
    std::function<void()> onEmbeddedWindowDied;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::Rectangle<int> getEditorArea() const;
    void attachEmbeddedWindowIfPossible();

    void* embeddedWindow { nullptr };
    juce::String lastAttachStatus;

    juce::Label presetLabel;
    juce::ComboBox presetSelector;
    juce::TextButton resetButton { "Rest" };
    ProgramSelectionCallback onProgramSelectionChanged;
    ResetCallback onResetRequested;
    bool suppressPresetChangeCallback { false };
};
} // namespace myapp::ui
