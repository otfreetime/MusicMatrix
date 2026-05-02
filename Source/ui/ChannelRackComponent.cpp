#include "ChannelRackComponent.h"
#include "../debug/DebugLogger.h"

//==============================================================================
// Constructor / Destructor
//==============================================================================

ChannelRackComponent::ChannelRackComponent()
{
    DEBUG_LOG ("ChannelRackComponent: Constructor started");
    
    // Create vertical scroll bar FIRST before setting up channels
    DEBUG_LOG ("ChannelRackComponent: Creating scroll bar...");
    scrollBar = std::make_unique<juce::ScrollBar> (false);  // false = vertical
    scrollBar->setRangeLimits (0, 1);
    scrollBar->addListener (this);
    addAndMakeVisible (scrollBar.get());
    
    DEBUG_LOG ("ChannelRackComponent: Setting up channels...");
    setNumChannels (16);  // Default to 16 channels like FL Studio
    
    DEBUG_LOG ("ChannelRackComponent: Constructor finished");
}

ChannelRackComponent::~ChannelRackComponent()
{
    if (scrollBar)
        scrollBar->removeListener (this);
}

//==============================================================================
// Component Overrides
//==============================================================================

void ChannelRackComponent::paint (juce::Graphics& g)
{
    // Background
    g.fillAll (juce::Colour (0xFF2A2A2A));

    // Draw each visible channel row
    const int numChannels = static_cast<int> (channels.size());
    const int visibleStart = static_cast<int> (scrollPosition / CHANNEL_HEIGHT);
    const int visibleEnd = visibleStart + (getHeight() / CHANNEL_HEIGHT) + 2;

    for (int i = visibleStart; i < juce::jmin (visibleEnd, numChannels); ++i)
    {
        const Channel& ch = channels[i];
        const auto channelBounds = getChannelBounds (i);

        if (!channelBounds.intersects (getLocalBounds()))
            continue;

        // Highlight selected channel
        if (i == selectedChannelIndex)
            g.setColour (juce::Colour (0xFF3A3A3A));
        else
            g.setColour (juce::Colour (0xFF2A2A2A));
        g.fillRect (channelBounds);

        // Draw border
        g.setColour (juce::Colour (0xFF555555));
        g.drawRect (channelBounds, 1);

        // Draw channel number/name
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (14.0f, juce::Font::bold));
        const juce::String channelLabel = juce::String (i + 1) + ": " + ch.name;
        g.drawText (channelLabel, channelBounds.reduced (MARGIN, 4), juce::Justification::left);

        // Draw instrument name (below channel label)
        g.setFont (juce::Font (12.0f));
        g.setColour (juce::Colours::lightgrey);
        g.drawText (ch.instrumentName, 
                    channelBounds.withTop (channelBounds.getY() + 20).reduced (MARGIN, 2),
                    juce::Justification::left);

        // Draw mute button
        const auto muteBounds = getMuteButtonBounds (channelBounds);
        g.setColour (ch.isMuted ? juce::Colours::red : juce::Colour (0xFF555555));
        g.fillRect (muteBounds);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (10.0f));
        g.drawText ("M", muteBounds, juce::Justification::centred);

        // Draw solo button
        const auto soloBounds = getSoloButtonBounds (channelBounds);
        g.setColour (ch.isSoloed ? juce::Colours::yellow : juce::Colour (0xFF555555));
        g.fillRect (soloBounds);
        g.setColour (juce::Colours::black);
        g.setFont (juce::Font (10.0f));
        g.drawText ("S", soloBounds, juce::Justification::centred);
    }
}

void ChannelRackComponent::resized()
{
    // Allocate space for scroll bar on the right
    const int scrollBarWidth = 12;
    scrollBar->setBounds (getWidth() - scrollBarWidth, 0, scrollBarWidth, getHeight());
    
    updateScrollBarRange();
}

void ChannelRackComponent::mouseDown (const juce::MouseEvent& e)
{
    const int clickedChannel = getChannelAtPosition (e.getPosition());
    if (clickedChannel < 0 || clickedChannel >= static_cast<int> (channels.size()))
        return;

    const auto channelBounds = getChannelBounds (clickedChannel);
    const auto muteBounds = getMuteButtonBounds (channelBounds);
    const auto soloBounds = getSoloButtonBounds (channelBounds);

    lastClickedChannelIndex = clickedChannel;

    if (muteBounds.contains (e.getPosition()))
    {
        lastClickTarget = ClickTarget::MUTE;
    }
    else if (soloBounds.contains (e.getPosition()))
    {
        lastClickTarget = ClickTarget::SOLO;
    }
    else
    {
        lastClickTarget = ClickTarget::CHANNEL_AREA;
        setSelectedChannelIndex (clickedChannel);
    }
}

void ChannelRackComponent::mouseDrag (const juce::MouseEvent& e)
{
    // Could implement drag-to-reorder channels here
}

void ChannelRackComponent::mouseUp (const juce::MouseEvent& e)
{
    if (lastClickedChannelIndex < 0 || lastClickedChannelIndex >= static_cast<int> (channels.size()))
        return;

    if (lastClickTarget == ClickTarget::MUTE)
    {
        channels[lastClickedChannelIndex].isMuted = !channels[lastClickedChannelIndex].isMuted;
        if (onChannelMuteToggled)
            onChannelMuteToggled (lastClickedChannelIndex, channels[lastClickedChannelIndex].isMuted);
        repaint();
    }
    else if (lastClickTarget == ClickTarget::SOLO)
    {
        channels[lastClickedChannelIndex].isSoloed = !channels[lastClickedChannelIndex].isSoloed;
        if (onChannelSoloToggled)
            onChannelSoloToggled (lastClickedChannelIndex, channels[lastClickedChannelIndex].isSoloed);
        repaint();
    }

    lastClickTarget = ClickTarget::CHANNEL_AREA;
    lastClickedChannelIndex = -1;
}

//==============================================================================
// Channel Management
//==============================================================================

void ChannelRackComponent::setNumChannels (int numChannels)
{
    channels.clear();
    for (int i = 0; i < numChannels; ++i)
    {
        Channel ch;
        ch.channelIndex = i;
        ch.name = juce::String (i + 1);
        ch.midiChannel = i % 16;
        ch.instrumentName = "---";
        ch.color = juce::Colours::white;
        channels.push_back (ch);
    }
    updateScrollBarRange();
    repaint();
}

ChannelRackComponent::Channel* ChannelRackComponent::getChannel (int index)
{
    if (index >= 0 && index < static_cast<int> (channels.size()))
        return &channels[index];
    return nullptr;
}

const ChannelRackComponent::Channel* ChannelRackComponent::getChannel (int index) const
{
    if (index >= 0 && index < static_cast<int> (channels.size()))
        return &channels[index];
    return nullptr;
}

void ChannelRackComponent::setChannelName (int index, const juce::String& name)
{
    if (auto* ch = getChannel (index))
    {
        ch->name = name;
        repaint();
    }
}

void ChannelRackComponent::setChannelInstrument (int index, const juce::String& instrument)
{
    if (auto* ch = getChannel (index))
    {
        ch->instrumentName = instrument;
        repaint();
    }
}

void ChannelRackComponent::setChannelMute (int index, bool shouldMute)
{
    if (auto* ch = getChannel (index))
    {
        ch->isMuted = shouldMute;
        repaint();
    }
}

void ChannelRackComponent::setChannelSolo (int index, bool shouldSolo)
{
    if (auto* ch = getChannel (index))
    {
        ch->isSoloed = shouldSolo;
        repaint();
    }
}

void ChannelRackComponent::setChannelColor (int index, juce::Colour color)
{
    if (auto* ch = getChannel (index))
    {
        ch->color = color;
        repaint();
    }
}

void ChannelRackComponent::setSelectedChannelIndex (int index)
{
    if (index >= 0 && index < static_cast<int> (channels.size()))
    {
        selectedChannelIndex = index;
        if (onChannelSelected)
            onChannelSelected (index);
        repaint();
    }
}

//==============================================================================
// ScrollBar::Listener
//==============================================================================

void ChannelRackComponent::scrollBarMoved (juce::ScrollBar* scrollBarThatMoved, double newRangeStart)
{
    if (scrollBarThatMoved == scrollBar.get())
    {
        scrollPosition = newRangeStart;
        repaint();
    }
}

//==============================================================================
// Internal Helpers
//==============================================================================

juce::Rectangle<int> ChannelRackComponent::getChannelBounds (int index) const
{
    const int y = index * CHANNEL_HEIGHT - static_cast<int> (scrollPosition);
    const int scrollBarWidth = 12;
    return juce::Rectangle<int> (0, y, getWidth() - scrollBarWidth, CHANNEL_HEIGHT);
}

juce::Rectangle<int> ChannelRackComponent::getMuteButtonBounds (const juce::Rectangle<int>& channelBounds) const
{
    auto bounds = channelBounds;
    return bounds.removeFromRight (SOLO_BUTTON_WIDTH + MUTE_BUTTON_WIDTH + MARGIN)
                 .removeFromLeft (MUTE_BUTTON_WIDTH)
                 .reduced (2);
}

juce::Rectangle<int> ChannelRackComponent::getSoloButtonBounds (const juce::Rectangle<int>& channelBounds) const
{
    auto bounds = channelBounds;
    return bounds.removeFromRight (SOLO_BUTTON_WIDTH + MARGIN)
                 .removeFromLeft (SOLO_BUTTON_WIDTH)
                 .reduced (2);
}

int ChannelRackComponent::getChannelAtPosition (juce::Point<int> pos) const
{
    const int index = static_cast<int> ((pos.getY() + scrollPosition) / CHANNEL_HEIGHT);
    if (index >= 0 && index < static_cast<int> (channels.size()))
        return index;
    return -1;
}

void ChannelRackComponent::updateScrollBarRange()
{
    const int totalHeight = static_cast<int> (channels.size()) * CHANNEL_HEIGHT;
    const int viewportHeight = getHeight();
    
    if (totalHeight > viewportHeight)
    {
        scrollBar->setRangeLimits (0, totalHeight - viewportHeight);
        scrollBar->setVisible (true);
    }
    else
    {
        scrollBar->setVisible (false);
        scrollPosition = 0.0;
    }
}
