#pragma once

#include <JuceHeader.h>

namespace myapp::host
{

/**
    Handles plugin scanning, loading, and cache management.
    Extracted from MainComponent for better maintainability.
*/
class PluginManager
{
public:
    PluginManager (const juce::File& deadMansPedal);
    ~PluginManager();

    //==========================================================================
    // Plugin Format Management
    //==========================================================================
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
    juce::KnownPluginList& getKnownPluginList() { return pluginList; }
    const juce::KnownPluginList& getKnownPluginList() const { return pluginList; }
    juce::AudioPluginInstance* getLoadedPlugin() const { return currentPlugin.get(); }

    //==========================================================================
    // Async Scanning
    //==========================================================================
    void scanForPluginsAsync (std::function<void()> onScanComplete);
    bool isScanning() const { return isScanningFlag; }
    juce::String getScanProgress() const { return activeScanFormatName; }

    //==========================================================================
    // Plugin Loading
    //==========================================================================
    void loadPluginAsync (int pluginIndex,
                          std::function<void(juce::AudioPluginInstance*, const juce::String&)> onLoadComplete);
    void unloadPlugin();
    bool isPluginLoaded() const { return currentPlugin != nullptr; }
    juce::String getLoadedPluginName() const;
    int getNumDiscoveredPlugins() const { return pluginList.getNumTypes(); }
    juce::PluginDescription getPluginDescription (int index) const;

    //==========================================================================
    // Cache Management
    //==========================================================================
    void loadPluginCache (juce::PropertiesFile* properties);
    void savePluginCache (juce::PropertiesFile* properties);

    //==========================================================================
    // Failed Plugin Tracking
    //==========================================================================
    juce::StringArray getFailedFiles() const { return failedFilesFromLastScan; }
    juce::StringPairArray getFailedFileFormats() const { return failedFileFormatByPath; }
    void syncFailedPluginsIntoList();
    void startNextFormatScan();

private:
    //==========================================================================
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList pluginList;
    std::unique_ptr<juce::AudioPluginInstance> currentPlugin;

    bool isScanningFlag = false;
    int scanFormatIndex = 0;
    std::unique_ptr<juce::PluginDirectoryScanner> activeScanner;
    juce::String activeScanFormatName;
    juce::StringArray failedFilesFromLastScan;
    juce::StringPairArray failedFileFormatByPath;
    juce::File deadMansPedalFile;
    std::function<void()> onScanCompleteCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginManager)
};

} // namespace myapp::host
