#include "MainComponent.h"

//==============================================================================
// Event Handlers Implementation
//==============================================================================

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &pluginManager.getKnownPluginList())
    {
        updatePluginListUI();
    }
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
