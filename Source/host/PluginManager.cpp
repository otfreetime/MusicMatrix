#include "PluginManager.h"

namespace myapp::host
{

PluginManager::PluginManager (const juce::File& deadMansPedal)
    : deadMansPedalFile (deadMansPedal)
{
#if JUCE_PLUGINHOST_VST3
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
#endif

#if JUCE_PLUGINHOST_VST
    formatManager.addFormat (std::make_unique<juce::VSTPluginFormat>());
#endif
}

PluginManager::~PluginManager()
{
    unloadPlugin();
}

//==============================================================================
// Async Scanning
//==============================================================================
void PluginManager::scanForPluginsAsync (std::function<void()> onScanComplete)
{
    if (isScanningFlag)
        return;

    isScanningFlag = true;
    scanFormatIndex = 0;
    failedFilesFromLastScan.clear();
    failedFileFormatByPath.clear();
    onScanCompleteCallback = std::move (onScanComplete);

    startNextFormatScan();
}

void PluginManager::startNextFormatScan()
{
    if (scanFormatIndex >= formatManager.getNumFormats())
    {
        isScanningFlag = false;
        activeScanner.reset();
        activeScanFormatName.clear();
        syncFailedPluginsIntoList();
        if (onScanCompleteCallback)
        {
            auto callback = std::move (onScanCompleteCallback);
            juce::MessageManager::callAsync (std::move (callback));
        }
        return;
    }

    auto* format = formatManager.getFormat (scanFormatIndex);
    activeScanFormatName = format->getName();

    juce::FileSearchPath searchPaths;

    if (format->getName() == "VST3")
    {
        searchPaths.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("VST3"));
        searchPaths.add (juce::File ("C:\\Program Files\\Common Files\\VST3"));
        searchPaths.add (juce::File ("C:\\Program Files (x86)\\Common Files\\VST3"));
    }
    else if (format->getName() == "VST")
    {
        searchPaths.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("VSTPlugins"));
        searchPaths.add (juce::File ("C:\\Program Files\\VstPlugins"));
        searchPaths.add (juce::File ("C:\\Program Files (x86)\\VstPlugins"));
        searchPaths.add (juce::File ("C:\\Program Files\\Steinberg\\VstPlugins"));
        searchPaths.add (juce::File ("C:\\Program Files\\Common Files\\VST2"));
    }

    if (searchPaths.getNumPaths() == 0)
    {
        ++scanFormatIndex;
        startNextFormatScan();
        return;
    }

    activeScanner = std::make_unique<juce::PluginDirectoryScanner> (
        pluginList, *format, searchPaths, true, deadMansPedalFile);

    juce::Thread::launch ([this]
    {
        juce::String filenameBeingScanned;
        
        while (activeScanner->scanNextFile (false, filenameBeingScanned))
        {
        }

        juce::MessageManager::callAsync ([this]
        {
            if (destroyed) return;
            // In JUCE 8, we need to track failures differently
            // For now, just mark scan as complete
            ++scanFormatIndex;
            startNextFormatScan();
        });
    });
}

//==============================================================================
// Plugin Loading
//==============================================================================
void PluginManager::loadPluginAsync (int pluginIndex,
                                     std::function<void(juce::AudioPluginInstance*, const juce::String&)> onLoadComplete)
{
    if (pluginIndex < 0 || pluginIndex >= pluginList.getNumTypes())
    {
        if (onLoadComplete)
            onLoadComplete (nullptr, "Invalid plugin index");
        return;
    }

    auto desc = pluginList.getTypes()[pluginIndex];

    juce::AudioPluginFormat* format = nullptr;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        if (formatManager.getFormat (i)->getName() == desc.pluginFormatName)
        {
            format = formatManager.getFormat (i);
            break;
        }
    }

    if (format == nullptr)
    {
        if (onLoadComplete)
            onLoadComplete (nullptr, "Unknown plugin format");
        return;
    }

    // Use the public async version
    format->createPluginInstanceAsync (desc, 44100.0, 512,
        [this, onLoadComplete] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (instance != nullptr)
            {
                currentPlugin = std::move (instance);
                if (onLoadComplete)
                    onLoadComplete (currentPlugin.get(), {});
            }
            else
            {
                if (onLoadComplete)
                    onLoadComplete (nullptr, error);
            }
        });
}

void PluginManager::unloadPlugin()
{
    currentPlugin.reset();
    destroyed = true;
}

juce::String PluginManager::getLoadedPluginName() const
{
    return currentPlugin != nullptr ? currentPlugin->getName() : juce::String();
}

juce::PluginDescription PluginManager::getPluginDescription (int index) const
{
    if (index < 0 || index >= pluginList.getNumTypes())
        return juce::PluginDescription();

    return pluginList.getTypes()[index];
}

//==============================================================================
// Cache Management
//==============================================================================
void PluginManager::loadPluginCache (juce::PropertiesFile* properties)
{
    if (properties == nullptr)
        return;

    auto xml = std::unique_ptr<juce::XmlElement> (properties->getXmlValue ("pluginList"));
    if (xml == nullptr)
        return;

    pluginList.recreateFromXml (*xml);
}

void PluginManager::savePluginCache (juce::PropertiesFile* properties)
{
    if (properties == nullptr)
        return;

    auto xml = std::unique_ptr<juce::XmlElement> (pluginList.createXml());
    if (xml != nullptr)
    {
        properties->setValue ("pluginList", xml.get());
        properties->saveIfNeeded();
    }
}

//==============================================================================
// Failed Plugin Tracking
//==============================================================================
void PluginManager::syncFailedPluginsIntoList()
{
    for (int i = failedFilesFromLastScan.size(); --i >= 0;)
    {
        const auto& filePath = failedFilesFromLastScan[i];
        const auto formatName = failedFileFormatByPath [filePath];

        juce::PluginDescription desc;
        desc.name = "Failed to load";
        desc.descriptiveName = "Failed to load";
        desc.fileOrIdentifier = filePath;
        desc.pluginFormatName = formatName;
        desc.category = "Failed to load";
        desc.manufacturerName = "-";
        desc.numInputChannels = 2;
        desc.numOutputChannels = 2;

        bool found = false;
        for (const auto& existing : pluginList.getTypes())
        {
            if (existing.fileOrIdentifier == filePath)
            {
                found = true;
                break;
            }
        }

        if (! found)
            pluginList.addType (desc);
    }
}

} // namespace myapp::host
