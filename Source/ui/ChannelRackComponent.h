#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>
#include <memory>

/**
 * ChannelRackComponent
 * Displays a vertical list of channels (tracks) with mute/solo controls,
 * instrument labels, and channel selection. Similar to FL Studio's Channel Rack.
 */
class ChannelRackComponent : public juce::Component,
                             public juce::ScrollBar::Listener
{
public:
    //==========================================================================
    // Channel Data Structure
    //==========================================================================
    struct Channel
    {
        int channelIndex = 0;          // 0-15 for 16 channels
        juce::String name;              // e.g., "Bay 1", "Channel #2"
        int midiChannel = 0;            // MIDI channel (0-15)
        bool isMuted = false;           // Mute state
        bool isSoloed = false;          // Solo state
        juce::String instrumentName = ""; // e.g., "Oud", "Qanun"
        juce::Colour color = juce::Colours::white; // Channel color for UI
    };

    //==========================================================================
    // Constructor / Destructor
    //==========================================================================
    ChannelRackComponent();
    ~ChannelRackComponent() override;

    //==========================================================================
    // Component Overrides
    //==========================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    //==========================================================================
    // Channel Management
    //==========================================================================
    /** Initialize with N channels (default 16) */
    void setNumChannels (int numChannels);
    int getNumChannels() const { return channels.size(); }

    /** Get a channel by index */
    Channel* getChannel (int index);
    const Channel* getChannel (int index) const;

    /** Update channel properties */
    void setChannelName (int index, const juce::String& name);
    void setChannelInstrument (int index, const juce::String& instrument);
    void setChannelMute (int index, bool shouldMute);
    void setChannelSolo (int index, bool shouldSolo);
    void setChannelColor (int index, juce::Colour color);

    /** Get the currently selected channel */
    int getSelectedChannelIndex() const { return selectedChannelIndex; }
    void setSelectedChannelIndex (int index);

    //==========================================================================
    // Callbacks
    //==========================================================================
    /** Called when user clicks on a channel */
    std::function<void (int channelIndex)> onChannelSelected = nullptr;

    /** Called when user toggles mute */
    std::function<void (int channelIndex, bool muted)> onChannelMuteToggled = nullptr;

    /** Called when user toggles solo */
    std::function<void (int channelIndex, bool soloed)> onChannelSoloToggled = nullptr;

    /** Called when user changes instrument */
    std::function<void (int channelIndex, const juce::String& instrument)> onChannelInstrumentChanged = nullptr;

    //==========================================================================
    // ScrollBar::Listener override
    //==========================================================================
    void scrollBarMoved (juce::ScrollBar* scrollBarThatMoved, double newRangeStart) override;

    //==========================================================================
    // Layout Properties
    //==========================================================================
    static constexpr int CHANNEL_HEIGHT = 60;  // Height of each channel row
    static constexpr int MUTE_BUTTON_WIDTH = 30;
    static constexpr int SOLO_BUTTON_WIDTH = 30;
    static constexpr int MARGIN = 8;

private:
    //==========================================================================
    // Internal Helpers
    //==========================================================================
    /** Get the visual bounds of a channel row (accounting for scroll) */
    juce::Rectangle<int> getChannelBounds (int index) const;

    /** Get bounds of mute button within a channel row */
    juce::Rectangle<int> getMuteButtonBounds (const juce::Rectangle<int>& channelBounds) const;

    /** Get bounds of solo button within a channel row */
    juce::Rectangle<int> getSoloButtonBounds (const juce::Rectangle<int>& channelBounds) const;

    /** Determine which channel was clicked */
    int getChannelAtPosition (juce::Point<int> pos) const;

    /** Update scroll bar range based on total content height */
    void updateScrollBarRange();

    //==========================================================================
    // Members
    //==========================================================================
    std::vector<Channel> channels;
    int selectedChannelIndex = 0;
    std::unique_ptr<juce::ScrollBar> scrollBar;
    double scrollPosition = 0.0;

    // For detecting button clicks
    enum class ClickTarget { MUTE, SOLO, CHANNEL_AREA };
    ClickTarget lastClickTarget = ClickTarget::CHANNEL_AREA;
    int lastClickedChannelIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackComponent)
};
