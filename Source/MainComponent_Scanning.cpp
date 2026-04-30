#include "MainComponent.h"

//==============================================================================
// Plugin Scanning and Cache Management
//==============================================================================

void MainComponent::scanForPluginsAsync()
{
    if (pluginManager.isScanning())
    {
        uiController.setStatusMessage ("Already scanning...");
        return;
    }

    uiController.setStatusMessage ("Starting plugin scan...");

    pluginManager.scanForPluginsAsync ([safeThis = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safeThis == nullptr) return;
        safeThis->uiController.setStatusMessage ("Scan complete. Found " + 
                             juce::String (safeThis->pluginManager.getNumDiscoveredPlugins()) + 
                             " plugins.");
        
        safeThis->updatePluginListUI();
        
        if (safeThis->bridgeManager.isAvailable())
        {
            safeThis->bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::unloadPlugin, {} });
        }
    });
}

void MainComponent::loadPluginCache()
{
    pluginManager.loadPluginCache (appProperties.get());
}

void MainComponent::savePluginCache()
{
    pluginManager.savePluginCache (appProperties.get());
}

void MainComponent::restoreMaqamPreset()
{
    if (appProperties != nullptr)
    {
        const auto lastMaqam = appProperties->getValue ("lastMaqam").getIntValue();
        if (lastMaqam >= 0 && lastMaqam <= 3)
        {
            using myapp::music::MaqamPreset;
            const auto presets = std::array<MaqamPreset, 4> {
                MaqamPreset::bayati, MaqamPreset::rast,
                MaqamPreset::hijaz,  MaqamPreset::sika
            };
            maqamManager.setMaqam (presets[(size_t) lastMaqam]);
            uiController.setMaqamSelection (lastMaqam + 1);
        }
    }
}

void MainComponent::syncFailedPluginsIntoList()
{
    pluginManager.syncFailedPluginsIntoList();
}
