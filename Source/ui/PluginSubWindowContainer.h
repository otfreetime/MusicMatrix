#pragma once

#include <JuceHeader.h>
#include <functional>

namespace myapp::ui
{
class PluginSubWindowContainer : public juce::Component
{
public:
    PluginSubWindowContainer();
    ~PluginSubWindowContainer() override;

    void setEmbeddedWindowHandle (void* nativeWindowHandle);
    void clearEmbeddedWindow();
    juce::String getLastAttachStatus() const;

    /** Called on the message thread when the embedded window is detected as destroyed. */
    std::function<void()> onEmbeddedWindowDied;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void attachEmbeddedWindowIfPossible();

    void* embeddedWindow { nullptr };
    juce::String lastAttachStatus;
};
} // namespace myapp::ui
