#include "MainComponent.h"

//==============================================================================
// Event Handlers Implementation
//==============================================================================

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &pluginManager.getKnownPluginList())
    {
        updatePluginListUI();
        return;
    }

    if (source == midiKeyboard.get())
    {
        clampKeyboardViewportToRange();
    }
}

void MainComponent::clampKeyboardViewportToRange()
{
    if (midiKeyboard == nullptr)
        return;

    constexpr int rangeStart = 12;
    constexpr int rangeEnd = 127;

    const float keyWidth = juce::jmax (1.0f, midiKeyboard->getKeyWidth());
    const float visibleWhiteKeys = juce::jmax (1.0f, (float) midiKeyboard->getWidth() / keyWidth);
    const int approxVisibleSemitones = juce::jmax (1, juce::roundToInt (visibleWhiteKeys * (12.0f / 7.0f)));
    const int maxLowestVisibleKey = juce::jmax (rangeStart, rangeEnd - approxVisibleSemitones + 1);

    const int currentLowest = midiKeyboard->getLowestVisibleKey();
    const int clampedLowest = juce::jlimit (rangeStart, maxLowestVisibleKey, currentLowest);

    if (clampedLowest != currentLowest)
        midiKeyboard->setLowestVisibleKey (clampedLowest);
}

void MainComponent::timerCallback()
{
    if (pluginManager.isScanning())
    {
        uiController.setStatusMessage ("Scanning " + pluginManager.getScanProgress() + "...");
    }
}

void MainComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (uiController.getPluginListComponent() == nullptr)
        return;

    auto& table = uiController.getPluginListComponent()->getTableListBox();
    auto* clicked = event.originalComponent;

    if (clicked == nullptr || (! table.isParentOf (clicked) && clicked != &table))
        return;

    const auto posInTable = table.getLocalPoint (clicked, event.getPosition());
    const int row = table.getRowContainingPosition (posInTable.x, posInTable.y);

    if (row < 0)
        return;

    if (row >= pluginManager.getNumDiscoveredPlugins())
    {
        uiController.setStatusMessage ("Selected entry is blacklisted and cannot be loaded");
        return;
    }

    loadPluginAsync (row);
}
