#include "MainComponent.h"

//==============================================================================
// UI Initialization and Layout
//==============================================================================

void MainComponent::initialiseUI()
{
    // Defer audio setup until message loop is running
    auto safeThis = juce::Component::SafePointer<MainComponent> (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis != nullptr)
            safeThis->setAudioChannels (2, 2);
    });

    // Create properties file for caching
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;

    appProperties = std::make_unique<juce::PropertiesFile> (
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("MyApp")
            .getChildFile ("PluginCache.properties"),
        options);

    // Create UI components via UIController
    uiController.createComponents (pluginManager.getFormatManager(),
                                   pluginManager.getKnownPluginList(),
                                   deadMansPedalFile,
                                   appProperties.get());

    addAndMakeVisible (uiController.getPluginListComponent());
    uiController.getPluginListComponent()->getTableListBox().addMouseListener (this, true);

    // Set up button callbacks
    uiController.getScanButton()->onClick  = [this] { scanForPluginsAsync(); };
    uiController.getUnloadButton()->onClick = [this] { unloadPlugin(); };

    uiController.getOpenAudioFileButton()->onClick = [this] { openAudioFile(); };
    uiController.getPlayButton()->onClick  = [this] { playAudioFile(); };
    uiController.getStopButton()->onClick  = [this] { stopAudioFile(); };

    // Maqam selector setup
    uiController.getMaqamSelector()->addItem ("Bayati",  1);
    uiController.getMaqamSelector()->addItem ("Rast",    2);
    uiController.getMaqamSelector()->addItem ("Hijaz",   3);
    uiController.getMaqamSelector()->addItem ("Sika",    4);
    uiController.getMaqamSelector()->setSelectedId (1, juce::dontSendNotification);
    
    uiController.getMaqamSelector()->onChange = [this]
    {
        using myapp::music::MaqamPreset;
        const auto presets = std::array<MaqamPreset, 4> {
            MaqamPreset::bayati, MaqamPreset::rast,
            MaqamPreset::hijaz,  MaqamPreset::sika
        };
        const int idx = uiController.getMaqamSelector()->getSelectedId() - 1;
        if (idx >= 0 && idx < (int) presets.size())
        {
            maqamManager.setMaqam (presets[(size_t) idx]);

            if (appProperties != nullptr)
            {
                appProperties->setValue ("lastMaqam", idx);
                appProperties->saveIfNeeded();
            }
        }
    };

    // Plugin filter selector
    uiController.getPluginFilterSelector()->addItem ("All", 1);
    uiController.getPluginFilterSelector()->addItem ("Loadable", 2);
    uiController.getPluginFilterSelector()->addItem ("Failed", 3);
    uiController.getPluginFilterSelector()->setSelectedId (1, juce::dontSendNotification);
    
    uiController.getPluginFilterSelector()->onChange = [this]
    {
        updatePluginListUI();
    };

    // Add all UI components
    uiController.addComponentsTo (*this);
    addAndMakeVisible (pluginSubWindowContainer);
    pluginSubWindowContainer.setVisible (false);

    // When the worker process crashes, the IPC disconnect is detected and fires this callback.
    // We then clear the embedded window reference safely on the message thread.
    bridgeManager.onWorkerDisconnected = [this]
    {
        DEBUG_LOG ("MainComponent: onWorkerDisconnected callback fired");
        pluginSubWindowContainer.clearEmbeddedWindow();
        pluginSubWindowContainer.setVisible (false);
        bridgePluginLoaded = false;
        uiController.setStatusMessage ("VST2 plugin connection lost. Reload to retry.");
    };

    // Listen for plugin list changes
    pluginManager.getKnownPluginList().addChangeListener (this);

    // Load cached plugin list + restore last maqam
    loadPluginCache();
    restoreMaqamPreset();
}

void MainComponent::resized()
{
    uiController.resized (*this, &pluginSubWindowContainer);
}

void MainComponent::updatePluginListUI()
{
    const int filterType = uiController.getPluginFilterSelector()->getSelectedId();
    
    uiController.updatePluginList (pluginManager.getKnownPluginList(),
                                   *uiController.getPluginSelector(),
                                   pluginSelectorToKnownIndex,
                                   filterType);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF1E1E1E));
}
