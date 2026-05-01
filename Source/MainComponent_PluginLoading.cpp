#include "MainComponent.h"

//==============================================================================
// Plugin Loading Implementation
//==============================================================================

void MainComponent::loadPluginAsync (int pluginIndex)
{
    if (pluginIndex < 0 || pluginIndex >= pluginManager.getNumDiscoveredPlugins())
        return;

    auto desc = pluginManager.getPluginDescription (pluginIndex);

    if (desc.category.equalsIgnoreCase ("Failed to load"))
    {
        const bool isFailedVST2 = desc.pluginFormatName.equalsIgnoreCase ("VST2")
                                   || desc.pluginFormatName.equalsIgnoreCase ("VST");

        if (isFailedVST2)
        {
            juce::String bridgeError;
            if (! bridgeManager.ensureWorkerForPlugin (desc.fileOrIdentifier, bridgeError))
            {
                uiController.setStatusMessage (bridgeError);
                return;
            }

            unloadPlugin();

            uiController.setStatusMessage ("Failed VST2 path: " + desc.fileOrIdentifier);
            uiController.setStatusMessage ("Setting embedded mode for VST2...");
            bridgeManager.setUIMode (false);
            juce::Thread::sleep (50);

            const auto workerName = bridgeManager.getActiveWorkerExecutable().existsAsFile()
                                      ? bridgeManager.getActiveWorkerExecutable().getFileName()
                                      : juce::String ("unknown-worker");
            const auto workerArch = detectWindowsBinaryArch (bridgeManager.getActiveWorkerExecutable().getFullPathName());
            const juce::String workerArchText = workerArch == PluginBinaryArch::x86 ? "x86"
                                             : workerArch == PluginBinaryArch::x64 ? "x64"
                                                                                   : "unknown";

            uiController.setStatusMessage ("Retrying failed VST2 via bridge [" + workerArchText + "/" + workerName + "]: " + desc.name);
            const auto sent = bridgeManager.loadPlugin (desc.fileOrIdentifier, desc.name);

            uiController.setStatusMessage (sent ? "Bridge command sent for failed VST2: " + desc.name
                                                : "Failed to send VST2 retry command to bridge");
            return;
        }

        uiController.setStatusMessage ("Plugin failed during scan. If it works in FL, it may need a different bridge architecture: "
                             + desc.name);
        return;
    }

    if (pluginManager.getKnownPluginList().getBlacklistedFiles().contains (desc.fileOrIdentifier))
    {
        uiController.setStatusMessage ("Plugin is blacklisted after failed initialisation: " + desc.name);
        return;
    }

    uiController.setStatusMessage ("Loading: " + desc.name + "...");

    const auto isVST3 = desc.pluginFormatName.equalsIgnoreCase ("VST3");
    const auto isVST2 = desc.pluginFormatName.equalsIgnoreCase ("VST");

    if (isVST2)
    {
        juce::String bridgeError;
        if (! bridgeManager.ensureWorkerForPlugin (desc.fileOrIdentifier, bridgeError))
        {
            uiController.setStatusMessage (bridgeError);
            return;
        }

        unloadPlugin();

        pluginSubWindowContainer.setVisible (false);
        pluginSubWindowContainer.clearEmbeddedWindow();

        bridgeManager.setUIMode (false);

        const auto sent = bridgeManager.loadPlugin (desc.fileOrIdentifier, desc.name);

        const auto workerName = bridgeManager.getActiveWorkerExecutable().existsAsFile()
                                  ? bridgeManager.getActiveWorkerExecutable().getFileName()
                                  : juce::String ("unknown-worker");
        const auto workerArch = detectWindowsBinaryArch (bridgeManager.getActiveWorkerExecutable().getFullPathName());
        const juce::String workerArchText = workerArch == PluginBinaryArch::x86 ? "x86"
                                         : workerArch == PluginBinaryArch::x64 ? "x64"
                                                                               : "unknown";

        uiController.setStatusMessage (sent ? "Bridge loading VST2 [" + workerArchText + "/" + workerName + "]: " + desc.name
                                            : (bridgeManager.isWorkerRunning() ? "Failed to send VST2 command to bridge" : "Bridge not available for VST2"));
        return;
    }

    if (isVST3)
    {
        unloadPlugin();

        pluginManager.loadPluginAsync (pluginIndex, [this, desc] (juce::AudioPluginInstance* plugin, const juce::String& error)
        {
            if (plugin != nullptr)
            {
                auto* loadedPlugin = pluginManager.getLoadedPlugin();
                if (loadedPlugin != nullptr)
                {
                    loadedPlugin->prepareToPlay (currentSampleRate, blockSize);
                    processorPlayer.setProcessor (loadedPlugin);

                    if (loadedPlugin->hasEditor())
                    {
                        auto* editor = loadedPlugin->createEditorIfNeeded();
                        if (editor != nullptr)
                        {
                            pluginEditorWindow = std::make_unique<PluginEditorWindow> (editor);
                            pluginEditorWindow->centreWithSize (editor->getWidth(), editor->getHeight());
                        }
                    }

                    uiController.setStatusMessage ("Loaded VST3: " + desc.name);
                }
            }
            else
            {
                uiController.setStatusMessage ("Failed to load VST3: " + desc.name + " - " + error);
            }
        });
        return;
    }

    uiController.setStatusMessage ("Unknown plugin format: " + desc.pluginFormatName);
}

void MainComponent::onPluginLoadComplete (juce::AudioPluginInstance* plugin, const juce::String& error)
{
    if (plugin != nullptr)
    {
        auto* loadedPlugin = pluginManager.getLoadedPlugin();
        if (loadedPlugin != nullptr)
        {
            loadedPlugin->prepareToPlay (currentSampleRate, blockSize);
            processorPlayer.setProcessor (loadedPlugin);

            if (loadedPlugin->hasEditor())
            {
                auto* editor = loadedPlugin->createEditorIfNeeded();
                if (editor != nullptr)
                {
                    pluginEditorWindow = std::make_unique<PluginEditorWindow> (editor);
                    pluginEditorWindow->centreWithSize (editor->getWidth(), editor->getHeight());
                }
            }

            uiController.setStatusMessage ("Loaded: " + loadedPlugin->getName());
        }
    }
    else
    {
        uiController.setStatusMessage ("Failed to load plugin: " + error);
    }
}

void MainComponent::unloadPlugin()
{
    DEBUG_LOG ("MainComponent: unloadPlugin called");
    pluginManager.unloadPlugin();
    DEBUG_LOG ("MainComponent: pluginManager unloaded");
    bridgeManager.unloadPlugin();
    DEBUG_LOG ("MainComponent: bridgeManager unloaded");

    pluginEditorWindow.reset();
    DEBUG_LOG ("MainComponent: pluginEditorWindow reset");
    processorPlayer.setProcessor (nullptr);
    DEBUG_LOG ("MainComponent: processor set to null");

    pluginSubWindowContainer.clearEmbeddedWindow();
    DEBUG_LOG ("MainComponent: embedded window cleared");
    pluginSubWindowContainer.setVisible (false);
    DEBUG_LOG ("MainComponent: sub window hidden");

    updateProgramSelector();

    uiController.setStatusMessage ("Plugin unloaded");
    DEBUG_LOG ("MainComponent: unloadPlugin finished");
}

bool MainComponent::isPluginLoaded() const
{
    return pluginManager.isPluginLoaded();
}

juce::String MainComponent::getLoadedPluginName() const
{
    return pluginManager.getLoadedPluginName();
}

int MainComponent::getNumDiscoveredPlugins() const
{
    return pluginManager.getNumDiscoveredPlugins();
}

juce::PluginDescription MainComponent::getPluginDescription (int index) const
{
    return pluginManager.getPluginDescription (index);
}
