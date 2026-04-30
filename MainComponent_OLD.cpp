#include "MainComponent.h"

namespace
{
enum class PluginBinaryArch
{
    unknown,
    x86,
    x64
};

PluginBinaryArch detectWindowsBinaryArch (const juce::String& filePath)
{
   #if JUCE_WINDOWS
    juce::File file (filePath);
    if (! file.existsAsFile())
        return PluginBinaryArch::unknown;

    juce::FileInputStream stream (file);
    if (! stream.openedOk())
        return PluginBinaryArch::unknown;

    if (! stream.setPosition (0x3C))
        return PluginBinaryArch::unknown;

    const auto peOffset = stream.readInt();
    if (peOffset <= 0 || ! stream.setPosition ((juce::int64) peOffset + 4))
        return PluginBinaryArch::unknown;

    const auto machine = (juce::uint16) stream.readShort();
    if (machine == 0x14c)
        return PluginBinaryArch::x86;
    if (machine == 0x8664)
        return PluginBinaryArch::x64;
   #endif

    return PluginBinaryArch::unknown;
}

}

//==============================================================================
// PluginEditorWindow Implementation
//==============================================================================
MainComponent::PluginEditorWindow::PluginEditorWindow (juce::AudioProcessorEditor* editor)
    : juce::DocumentWindow (editor->getName(), 
                            juce::Colours::lightgrey, 
                            juce::DocumentWindow::allButtons)
{
    setResizable (true, true);
    setContentOwned (editor, true);
    setUsingNativeTitleBar (true);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void MainComponent::PluginEditorWindow::closeButtonPressed()
{
    clearContentComponent();
}

//==============================================================================
// MainComponent Implementation
//==============================================================================
MainComponent::MainComponent()
{
    setLookAndFeel (&customLookAndFeel);

    // Defer audio setup until message loop is running so startup UI can't stall.
    auto safeThis = juce::Component::SafePointer<MainComponent> (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis != nullptr)
            safeThis->setAudioChannels (2, 2);
    });

    // Initialize plugin format manager with VST3 support
#if JUCE_PLUGINHOST_VST3
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
#endif

    // (Optional) Add VST2 support if available on this platform
#if JUCE_PLUGINHOST_VST
    formatManager.addFormat (std::make_unique<juce::VSTPluginFormat>());
#endif

    // Set up dead man's pedal file (for crashed plugin recovery)
    deadMansPedalFile = juce::File::getSpecialLocation (
        juce::File::tempDirectory).getChildFile ("MyApp_Plugins.deadmanspedalfile");

    // Create properties file for caching
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;

    appProperties = std::make_unique<juce::PropertiesFile> (
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("MyApp")
            .getChildFile ("PluginCache.properties"),
        options);

    // Create plugin list component (UI)
    pluginListComponent = std::make_unique<juce::PluginListComponent> (
        formatManager,
        pluginList,
        deadMansPedalFile,
        appProperties.get(),
        true);  // allowAsync

    addAndMakeVisible (pluginListComponent.get());
    pluginListComponent->getTableListBox().addMouseListener (this, true);

    // Set up UI buttons
    scanButton.onClick  = [this] { scanForPluginsAsync(); };
    loadButton.onClick  = [this]
    {
        const int selectedRow = pluginSelector.getSelectedId() - 1;
        if (selectedRow < 0 || selectedRow >= pluginSelectorToKnownIndex.size())
            return;

        const int knownIndex = pluginSelectorToKnownIndex.getReference (selectedRow);
        if (knownIndex < 0)
        {
            statusLabel.setText ("Selected entry failed scan and cannot be loaded",
                                 juce::dontSendNotification);
            return;
        }

        loadPluginAsync (knownIndex);
    };
    unloadButton.onClick = [this] { unloadPlugin(); };

    // Maqam selector
    maqamSelector.addItem ("Bayati",  1);
    maqamSelector.addItem ("Rast",    2);
    maqamSelector.addItem ("Hijaz",   3);
    maqamSelector.addItem ("Sika",    4);
    maqamSelector.setSelectedId (1, juce::dontSendNotification);
    maqamSelector.onChange = [this]
    {
        using myapp::music::MaqamPreset;
        const auto presets = std::array<MaqamPreset, 4> {
            MaqamPreset::bayati, MaqamPreset::rast,
            MaqamPreset::hijaz,  MaqamPreset::sika
        };
        const int idx = maqamSelector.getSelectedId() - 1;
        if (idx >= 0 && idx < (int) presets.size())
        {
            maqamManager.setMaqam (presets[(size_t) idx]);

            // Persist selection
            if (appProperties != nullptr)
            {
                appProperties->setValue ("lastMaqam", idx);
                appProperties->saveIfNeeded();
            }
        }
    };

    pluginFilterSelector.addItem ("All", 1);
    pluginFilterSelector.addItem ("Loadable", 2);
    pluginFilterSelector.addItem ("Failed", 3);
    pluginFilterSelector.setSelectedId (1, juce::dontSendNotification);
    pluginFilterSelector.onChange = [this]
    {
        updatePluginListUI();
    };

    // Waveform visualiser
    waveformVisualiser.setColours (juce::Colour (0xFF1E1E1E), juce::Colour (0xFFFFBF00));
    waveformVisualiser.setSamplesPerBlock (256);
    waveformVisualiser.setRepaintRate (30);

    // Status label style
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (juce::Font (juce::FontOptions (13.0f)));

    addAndMakeVisible (scanButton);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (unloadButton);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (maqamLabel);
    addAndMakeVisible (maqamSelector);
    addAndMakeVisible (pluginFilterSelector);
    addAndMakeVisible (pluginSelector);
    addAndMakeVisible (waveformVisualiser);
    addAndMakeVisible (pluginSubWindowContainer);

    // Defer bridge startup until the UI is already created and visible.
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis != nullptr)
            safeThis->initialiseBridge();
    });

    // Listen for plugin list changes
    pluginList.addChangeListener (this);

    // Load cached plugin list + restore last maqam
    loadPluginCache();
    restoreMaqamPreset();
    updatePluginListUI();

    setSize (800, 600);
}

MainComponent::~MainComponent()
{
    if (pluginListComponent != nullptr)
        pluginListComponent->getTableListBox().removeMouseListener (this);

    setLookAndFeel (nullptr);

    // Save plugin list before shutdown
    savePluginCache();
    
    // Clean up plugin
    unloadPlugin();

    bridgeMaster.shutdownWorker();
    
    // Shut down audio
    shutdownAudio();
}

//==============================================================================
// Audio Processing
//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    blockSize = samplesPerBlockExpected;

    // Prepare processor player (routes to current plugin)
    if (currentPlugin != nullptr)
    {
        processorPlayer.setProcessor (currentPlugin.get());
    }

    // Prepare dry/wet mixing DSP
    juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32> (samplesPerBlockExpected),
        2
    };
    dryGain.prepare (spec);
    dryGain.setGainLinear (1.0f - dryWetMix);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    const juce::ScopedLock lock (pluginLock);

    if (bridgePluginLoaded && bridgeAudioInputMemory != nullptr && bridgeAudioOutputMemory != nullptr)
    {
        const int numChannels = bufferToFill.buffer->getNumChannels();
        const int numSamples = bufferToFill.numSamples;
        const float* const* sourcePointers = bufferToFill.buffer->getArrayOfReadPointers();
        float* const* outputPointers = bufferToFill.buffer->getArrayOfWritePointers();

        // Copy input into shared memory; zero missing channels.
        auto* inputData = static_cast<float*> (bridgeAudioInputMemory->getData());
        for (int ch = 0; ch < 2; ++ch)
        {
            if (ch < numChannels)
                juce::FloatVectorOperations::copy (inputData + (ch * numSamples), sourcePointers[ch] + bufferToFill.startSample, numSamples);
            else
                juce::FloatVectorOperations::clear (inputData + (ch * numSamples), numSamples);
        }

        bridgeAudioProcessed.store (false);
        bridgeMaster.send ({ myapp::bridge::IPCCommandType::processAudio, juce::String (bufferToFill.numSamples) });

        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2.0;
        while (! bridgeAudioProcessed.load() && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::Thread::yield();

        auto* outputData = static_cast<float*> (bridgeAudioOutputMemory->getData());
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch < 2)
                juce::FloatVectorOperations::copy (outputPointers[ch] + bufferToFill.startSample,
                                                  outputData + (ch * numSamples),
                                                  numSamples);
            else
                bufferToFill.buffer->clear (ch, bufferToFill.startSample, numSamples);
        }

        // Mix dry/wet using the host dry buffer.
        juce::AudioBuffer<float> dryBuffer (numChannels, numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom (ch, 0, *bufferToFill.buffer, ch, bufferToFill.startSample, numSamples);
        processDryWet (dryBuffer, numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
            bufferToFill.buffer->addFrom (ch, bufferToFill.startSample, dryBuffer, ch, 0, numSamples, dryWetMix);

        waveformVisualiser.pushBuffer (*bufferToFill.buffer);
        return;
    }

    if (currentPlugin == nullptr)
    {
        // No plugin loaded: clear output to silence
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    // Preserve dry signal
    juce::AudioBuffer<float> dryBuffer (
        bufferToFill.buffer->getNumChannels(),
        bufferToFill.numSamples);
    
    for (int ch = 0; ch < bufferToFill.buffer->getNumChannels(); ++ch)
    {
        dryBuffer.copyFrom (ch, 0, 
            *bufferToFill.buffer, ch,
            bufferToFill.startSample,
            bufferToFill.numSamples);
    }
    
    // Process through plugin directly (normal mode)
    juce::MidiBuffer transformedMidi;
    maqamManager.processMidiBuffer (transformedMidi, 1);

    processorPlayer.audioDeviceIOCallbackWithContext (
        nullptr, 0,  // inputs (handled by hardware)
        bufferToFill.buffer->getArrayOfWritePointers(),
        bufferToFill.buffer->getNumChannels(),
        bufferToFill.numSamples,
        {});

    // Mix dry and wet
    processDryWet (dryBuffer, bufferToFill.numSamples);
    
    for (int ch = 0; ch < bufferToFill.buffer->getNumChannels(); ++ch)
    {
        bufferToFill.buffer->addFrom (
            ch, bufferToFill.startSample,
            dryBuffer, ch, 0,
            bufferToFill.numSamples,
            dryWetMix);
    }

    // Feed waveform visualiser (safe: AudioVisualiserComponent is thread-safe)
    waveformVisualiser.pushBuffer (*bufferToFill.buffer);
}

void MainComponent::releaseResources()
{
    processorPlayer.audioDeviceStopped();
}

//==============================================================================
// Audio DSP
//==============================================================================
void MainComponent::processDryWet (juce::AudioBuffer<float>& dryBuffer, int numSamples)
{
    juce::ignoreUnused (numSamples);

    // Apply dry gain to dryBuffer
    juce::dsp::AudioBlock<float> block (dryBuffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    dryGain.process (context);
}

//==============================================================================
// Plugin Management
//==============================================================================
void MainComponent::scanForPluginsAsync()
{
    if (isScanning)
        return;

    if (formatManager.getNumFormats() == 0)
    {
        statusLabel.setText ("No plugin formats available", juce::dontSendNotification);
        return;
    }

    isScanning = true;
    scanFormatIndex = 0;
    activeScanner.reset();
    activeScanFormatName.clear();
    scannerPluginBeingScanned.clear();
    failedFilesFromLastScan.clear();
    failedFileFormatByPath.clear();
    pluginList.clearBlacklistedFiles();
    syncFailedPluginsIntoList();
    statusLabel.setText ("Scanning for plugins...", juce::dontSendNotification);

    if (bridgeAvailable)
        bridgeMaster.send ({ myapp::bridge::IPCCommandType::heartbeat, "scan_start" });

    // Start first scan; remaining formats are scanned sequentially in timerCallback().
    startNextFormatScan();
    startTimer (250);
}

void MainComponent::startNextFormatScan()
{
    while (scanFormatIndex < formatManager.getNumFormats())
    {
        if (auto* format = formatManager.getFormat (scanFormatIndex++))
        {
            activeScanFormatName = format->getName();
            statusLabel.setText ("Scanning " + format->getName() + " plugins...",
                                 juce::dontSendNotification);

            auto searchPath = appProperties != nullptr
                                ? juce::PluginListComponent::getLastSearchPath (*appProperties, *format)
                                : format->getDefaultLocationsToSearch();

            if (searchPath.getNumPaths() == 0)
                searchPath = format->getDefaultLocationsToSearch();

            // No special additional plugin search paths are used.

            activeScanner = std::make_unique<juce::PluginDirectoryScanner> (
                pluginList,
                *format,
                searchPath,
                true,
                deadMansPedalFile,
                true);

            return;
        }
    }

    stopTimer();
    isScanning = false;
    activeScanner.reset();
    activeScanFormatName.clear();
    syncFailedPluginsIntoList();

    int numFailed = 0;
    for (const auto& desc : pluginList.getTypes())
        if (desc.category.equalsIgnoreCase ("Failed to load"))
            ++numFailed;

    const auto numPlugins = pluginList.getNumTypes() - numFailed;
    auto status = "Scan complete: " + juce::String (numPlugins) + " plugins found";

    if (numFailed > 0)
        status += ", " + juce::String (numFailed) + " failed";

    statusLabel.setText (status, juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    if (! isScanning)
    {
        stopTimer();
        return;
    }

    if (activeScanner == nullptr)
    {
        startNextFormatScan();
        return;
    }

    if (activeScanner->scanNextFile (true, scannerPluginBeingScanned))
    {
        if (scannerPluginBeingScanned.isNotEmpty())
        {
            statusLabel.setText ("Scanning: " + juce::File (scannerPluginBeingScanned).getFileName(),
                                 juce::dontSendNotification);
        }
        return;
    }

    for (const auto& failedPath : activeScanner->getFailedFiles())
    {
        if (! failedFilesFromLastScan.contains (failedPath))
            failedFilesFromLastScan.add (failedPath);

        if (! failedFileFormatByPath.containsKey (failedPath))
            failedFileFormatByPath.set (failedPath, activeScanFormatName);
    }

    activeScanner.reset();
    activeScanFormatName.clear();
    startNextFormatScan();
}

void MainComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (pluginListComponent == nullptr)
        return;

    auto& table = pluginListComponent->getTableListBox();
    auto* clicked = event.originalComponent;

    if (clicked == nullptr || (! table.isParentOf (clicked) && clicked != &table))
        return;

    const auto posInTable = table.getLocalPoint (clicked, event.getPosition());
    const int row = table.getRowContainingPosition (posInTable.x, posInTable.y);

    if (row < 0)
        return;

    if (row >= pluginList.getNumTypes())
    {
        statusLabel.setText ("Selected entry is blacklisted and cannot be loaded",
                             juce::dontSendNotification);
        return;
    }

    loadPluginAsync (row);
}

void MainComponent::loadPluginAsync (int pluginIndex)
{
    if (pluginIndex < 0 || pluginIndex >= pluginList.getNumTypes())
        return;

    auto desc = pluginList.getTypes()[pluginIndex];

    if (desc.category.equalsIgnoreCase ("Failed to load"))
    {
        const bool isFailedVST2 = desc.pluginFormatName.equalsIgnoreCase ("VST2")
                                   || desc.pluginFormatName.equalsIgnoreCase ("VST");

        if (isFailedVST2)
        {
            juce::String bridgeError;
            if (! ensureBridgeWorkerForPlugin (desc.fileOrIdentifier, bridgeError))
            {
                statusLabel.setText (bridgeError,
                                     juce::dontSendNotification);
                return;
            }

            unloadPlugin();

            // Use embedded UI mode for bridged VST2 plugins by default.
            // The bridge worker will publish its editor HWND back to the host.
            statusLabel.setText ("Setting embedded mode for VST2...", juce::dontSendNotification);
            bridgeMaster.send ({ myapp::bridge::IPCCommandType::setDetached, "false" });
            juce::Thread::sleep (50); // Small delay to ensure worker processes setDetached first

            statusLabel.setText ("Loading " + desc.name + " via bridge (detached)...", juce::dontSendNotification);
            const auto sent = bridgeMaster.send ({ myapp::bridge::IPCCommandType::loadPlugin,
                                                   desc.fileOrIdentifier });

            statusLabel.setText (sent ? "Retrying failed VST2 via bridge: " + desc.name
                                      : "Failed to send VST2 retry command to bridge",
                                 juce::dontSendNotification);
            return;
        }

        statusLabel.setText ("Plugin failed during scan. If it works in FL, it may need a different bridge architecture: "
                             + desc.name,
                             juce::dontSendNotification);
        return;
    }

    if (pluginList.getBlacklistedFiles().contains (desc.fileOrIdentifier))
    {
        statusLabel.setText ("Plugin is blacklisted after failed initialisation: " + desc.name,
                             juce::dontSendNotification);
        return;
    }

    statusLabel.setText ("Loading: " + desc.name + "...", juce::dontSendNotification);

    const auto isVST3 = desc.pluginFormatName.equalsIgnoreCase ("VST3");
    const auto isVST2 = desc.pluginFormatName.equalsIgnoreCase ("VST");

    if (isVST2)
    {
        juce::String bridgeError;
        if (! ensureBridgeWorkerForPlugin (desc.fileOrIdentifier, bridgeError))
        {
            statusLabel.setText (bridgeError, juce::dontSendNotification);
            return;
        }

        // VST2 is bridge-only: do not load it in the host process.
        unloadPlugin();

        bridgeMaster.send ({ myapp::bridge::IPCCommandType::setDetached, "false" });

        const auto sent = bridgeMaster.send ({ myapp::bridge::IPCCommandType::loadPlugin,
                                               desc.fileOrIdentifier });

        const auto workerName = activeBridgeWorkerExecutable.existsAsFile()
                                  ? activeBridgeWorkerExecutable.getFileName()
                                  : juce::String ("unknown-worker");
        const auto workerArch = detectWindowsBinaryArch (activeBridgeWorkerExecutable.getFullPathName());
        const juce::String workerArchText = workerArch == PluginBinaryArch::x86 ? "x86"
                                         : workerArch == PluginBinaryArch::x64 ? "x64"
                                                                               : "unknown";

        statusLabel.setText (sent ? "Bridge loading VST2 [" + workerArchText + "/" + workerName + "]: " + desc.name
                                  : "Failed to send VST2 load command to bridge",
                             juce::dontSendNotification);
        return;
    }

    // VST3 remains local/in-process.
    juce::ignoreUnused (isVST3);

    // Async load (non-blocking)
    formatManager.createPluginInstanceAsync (
        desc,
        currentSampleRate,
        blockSize,
        [this] (std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::String& error)
        {
            onPluginLoadComplete (plugin.release(), error);
        });
}

void MainComponent::onPluginLoadComplete (juce::AudioPluginInstance* plugin,
                                          const juce::String& error)
{
    const juce::ScopedLock lock (pluginLock);

    if (plugin == nullptr)
    {
        statusLabel.setText ("Error loading plugin: " + error, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon,
            "Plugin Load Error",
            error);
        return;
    }

    // Unload old plugin first
    unloadPlugin();

    currentPlugin.reset (plugin);
    processorPlayer.setProcessor (currentPlugin.get());

    // Prepare plugin if audio is running
    if (currentSampleRate > 0)
    {
        currentPlugin->prepareToPlay (currentSampleRate, blockSize);
    }

    // Create and show editor if available
    if (auto* editor = currentPlugin->createEditorIfNeeded())
    {
        pluginEditorWindow = std::make_unique<PluginEditorWindow> (editor);
    }

    statusLabel.setText ("Loaded: " + currentPlugin->getName(),
                        juce::dontSendNotification);
}

void MainComponent::unloadPlugin()
{
    if (bridgeAvailable)
        bridgeMaster.send ({ myapp::bridge::IPCCommandType::unloadPlugin, {} });

    bridgePluginLoaded = false;
    closeBridgeAudioFiles();

    // Need to reset the editor window on the message thread.
    // If not on message thread, post a message and wait (or return if async).
    // Here we are guaranteed to be on the message thread because this is called 
    // from a button click OR during shutdown.
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        pluginEditorWindow.reset();
    }
    else
    {
        juce::MessageManager::callAsync ([this] { pluginEditorWindow.reset(); });
    }

    {
        const juce::ScopedLock lock (pluginLock);

        if (currentPlugin != nullptr)
        {
            currentPlugin->suspendProcessing (true);
            processorPlayer.setProcessor (nullptr);
            currentPlugin->releaseResources();
            currentPlugin.reset();
        }
    }

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        statusLabel.setText ("No plugin loaded", juce::dontSendNotification);
    else
        juce::MessageManager::callAsync ([this] { statusLabel.setText ("No plugin loaded", juce::dontSendNotification); });
}

bool MainComponent::isPluginLoaded() const
{
    return currentPlugin != nullptr;
}

juce::String MainComponent::getLoadedPluginName() const
{
    if (currentPlugin != nullptr)
        return currentPlugin->getName();
    return {};
}

int MainComponent::getNumDiscoveredPlugins() const
{
    return pluginList.getNumTypes();
}

juce::PluginDescription MainComponent::getPluginDescription (int index) const
{
    if (index >= 0 && index < pluginList.getNumTypes())
        return pluginList.getTypes()[index];

    return {};
}

//==============================================================================
// Plugin Cache (XML Persistence)
//==============================================================================
void MainComponent::loadPluginCache()
{
    if (appProperties == nullptr)
        return;

    auto xml = appProperties->getXmlValue ("pluginList");
    if (xml != nullptr)
    {
        pluginList.recreateFromXml (*xml);

        // Validate all loaded descriptors; drop corrupt entries so
        // they can't crash updatePluginListUI() later.
        juce::Array<juce::PluginDescription> toRemove;
        for (const auto& desc : pluginList.getTypes())
            if (desc.name.isEmpty())
                toRemove.add (desc);
        for (const auto& desc : toRemove)
            pluginList.removeType (desc);
    }
}

void MainComponent::savePluginCache()
{
    if (appProperties == nullptr)
        return;

    auto xml = pluginList.createXml();
    if (xml != nullptr)
    {
        appProperties->setValue ("pluginList", xml.get());
        appProperties->saveIfNeeded();
    }
}

void MainComponent::restoreMaqamPreset()
{
    if (appProperties == nullptr)
        return;

    // Default to 0 (Bayati) if key was never written
    const int savedIdx = appProperties->getIntValue ("lastMaqam", 0);
    const int clampedIdx = juce::jlimit (0, 3, savedIdx);

    using myapp::music::MaqamPreset;
    const auto presets = std::array<MaqamPreset, 4> {
        MaqamPreset::bayati, MaqamPreset::rast,
        MaqamPreset::hijaz,  MaqamPreset::sika
    };

    maqamManager.setMaqam (presets[(size_t) clampedIdx]);
    maqamSelector.setSelectedId (clampedIdx + 1, juce::dontSendNotification);
}

void MainComponent::updatePluginListUI()
{
    if (pluginListComponent)
        pluginListComponent->repaint();

    pluginSelectorToKnownIndex.clear();
    pluginSelector.clear (juce::dontSendNotification);

    const int selectedFilter = pluginFilterSelector.getSelectedId();
    const bool includeLoadable = (selectedFilter == 1 || selectedFilter == 2);
    const bool includeFailed = (selectedFilter == 1 || selectedFilter == 3);

    const auto& types = pluginList.getTypes();
    for (int i = 0; i < types.size(); ++i)
    {
        // Guard against any partially-initialised cached descriptors.
        const auto& desc = types.getReference (i);
        if (desc.name.isEmpty())
            continue;

        const bool isFailed = desc.category.equalsIgnoreCase ("Failed to load");
        if ((isFailed && ! includeFailed) || (! isFailed && ! includeLoadable))
            continue;

        const auto label = desc.name + " [" + desc.pluginFormatName + "]";
        const auto itemId = pluginSelector.getNumItems() + 1;
        pluginSelector.addItem (label, itemId);
        pluginSelectorToKnownIndex.add (i);
    }

    if (pluginSelector.getNumItems() > 0 && pluginSelector.getSelectedId() <= 0)
        pluginSelector.setSelectedId (1, juce::dontSendNotification);
}

void MainComponent::syncFailedPluginsIntoList()
{
    juce::Array<juce::PluginDescription> staleFailed;
    for (const auto& desc : pluginList.getTypes())
    {
        if (desc.category.equalsIgnoreCase ("Failed to load"))
            staleFailed.add (desc);
    }

    for (const auto& desc : staleFailed)
        pluginList.removeType (desc);

    for (const auto& failedPath : failedFilesFromLastScan)
    {
        juce::PluginDescription failedDesc;
        failedDesc.fileOrIdentifier = failedPath;
        failedDesc.name = juce::File (failedPath).getFileNameWithoutExtension();
        if (failedDesc.name.isEmpty())
            failedDesc.name = failedPath;

        auto failedFormat = failedFileFormatByPath.getValue (failedPath, {});
        if (failedFormat.equalsIgnoreCase ("VST"))
            failedFormat = "VST2";
        if (failedFormat.isEmpty())
            failedFormat = "Unknown";

        failedDesc.pluginFormatName = failedFormat;
        failedDesc.category = "Failed to load";
        failedDesc.manufacturerName = "-";
        failedDesc.version = "-";

        pluginList.addType (failedDesc);
    }
}

//==============================================================================
// ChangeListener
//==============================================================================
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &pluginList)
    {
        updatePluginListUI();
        savePluginCache();

        if (! isScanning)
        {
            int numFailed = 0;
            for (const auto& desc : pluginList.getTypes())
                if (desc.category.equalsIgnoreCase ("Failed to load"))
                    ++numFailed;

            const auto numOk = pluginList.getNumTypes() - numFailed;
            auto status = "Scan complete: " + juce::String (numOk) + " plugins found";
            if (numFailed > 0)
                status += ", " + juce::String (numFailed) + " failed";

            statusLabel.setText (status, juce::dontSendNotification);
        }
    }
}

//==============================================================================
// GUI Layout
//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // ── Row 1: scan / load / unload / status ───────────────────────────────
    auto row1 = area.removeFromTop (40).reduced (5);
    scanButton  .setBounds (row1.removeFromLeft (150).reduced (2));
    loadButton  .setBounds (row1.removeFromLeft (140).reduced (2));
    unloadButton.setBounds (row1.removeFromLeft (150).reduced (2));
    statusLabel .setBounds (row1);

    // ── Row 2: maqam + plugin selector ─────────────────────────────────────
    auto row2 = area.removeFromTop (36).reduced (5);
    maqamLabel   .setBounds (row2.removeFromLeft (60).reduced (2));
    maqamSelector.setBounds (row2.removeFromLeft (160).reduced (2));
    row2.removeFromLeft (8);
    pluginFilterSelector.setBounds (row2.removeFromLeft (120).reduced (2));
    row2.removeFromLeft (8);
    pluginSelector.setBounds (row2.removeFromLeft (320).reduced (2));

    // ── Bottom: plugin sub-window + waveform ───────────────────────────────
    auto bottomArea    = area.removeFromBottom (200).reduced (4);
    auto waveformArea  = bottomArea.removeFromBottom (60);
    pluginSubWindowContainer.setBounds (bottomArea);
    waveformVisualiser      .setBounds (waveformArea);

    // ── Centre: plugin list ────────────────────────────────────────────────
    if (pluginListComponent)
        pluginListComponent->setBounds (area);
}

void MainComponent::initialiseBridge()
{
    auto executable = findBridgeWorkerExecutable();

    bridgeMaster.setCommandCallback ([this] (const myapp::bridge::IPCCommand& command)
    {
        juce::MessageManager::callAsync ([this, command]
        {
            handleBridgeCommand (command);
        });
    });

    // launchWorker blocks up to 5 s waiting for the pipe – run on a background
    // thread so the message loop (and therefore the window) stays responsive.
    auto safeThis = juce::Component::SafePointer<MainComponent> (this);
    juce::Thread::launch ([safeThis, executable]
    {
        if (safeThis == nullptr)
            return;

        const bool ok = safeThis->bridgeMaster.launchWorker (executable);

        juce::MessageManager::callAsync ([safeThis, ok, executable]
        {
            if (safeThis == nullptr)
                return;

            safeThis->bridgeAvailable = ok;

            if (! ok)
            {
                safeThis->statusLabel.setText (
                    "Bridge unavailable (local mode): " + executable.getFullPathName(),
                    juce::dontSendNotification);
            }
            else
            {
                safeThis->activeBridgeWorkerExecutable = executable;
                safeThis->statusLabel.setText (
                    "Bridge worker launched: " + executable.getFileName(),
                    juce::dontSendNotification);
            }
        });
    });
}

juce::File MainComponent::findBridgeWorkerExecutable() const
{
    return findBridgeWorkerExecutableForPlugin ({});
}

juce::File MainComponent::findBridgeWorkerExecutableForPlugin (const juce::String& pluginPath) const
{
    const auto pluginArch = detectWindowsBinaryArch (pluginPath);
    const bool wantsX86Worker = (pluginArch == PluginBinaryArch::x86);

    const auto archEnvPath = juce::SystemStats::getEnvironmentVariable (
        wantsX86Worker ? "MYAPP_BRIDGE_WORKER_X86_PATH" : "MYAPP_BRIDGE_WORKER_X64_PATH", {});
    if (archEnvPath.isNotEmpty())
    {
        juce::File candidate (archEnvPath);
        if (candidate.existsAsFile())
            return candidate;
    }

    const auto envPath = juce::SystemStats::getEnvironmentVariable ("MYAPP_BRIDGE_WORKER_PATH", {});
    if (envPath.isNotEmpty())
    {
        juce::File candidate (envPath);
        if (candidate.existsAsFile())
            return candidate;
    }

    auto hostExe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    auto siblingWorker = hostExe.getSiblingFile (wantsX86Worker ? "MyAppBridgeWorker_x86.exe"
                                                                : "MyAppBridgeWorker.exe");
    if (siblingWorker.existsAsFile())
        return siblingWorker;

    // CMake/JUCE default artefact layout:
    //   host:   <build>/MyApp_artefacts/<Config>/MyApp.exe
    //   worker: <build>/MyAppBridgeWorker_artefacts/<Config>/MyAppBridgeWorker.exe
    auto configDir = hostExe.getParentDirectory();               // .../<Config>
    auto artefactDir = configDir.getParentDirectory();           // .../MyApp_artefacts
    auto buildDir = artefactDir.getParentDirectory();            // .../build
    auto projectDir = buildDir.getParentDirectory();             // .../MyApp

    if (! wantsX86Worker && buildDir.isDirectory())
    {
        auto workerFromBuildLayout = buildDir
            .getChildFile ("MyAppBridgeWorker_artefacts")
            .getChildFile (configDir.getFileName())
            .getChildFile ("MyAppBridgeWorker.exe");

        if (workerFromBuildLayout.existsAsFile())
            return workerFromBuildLayout;
    }

    if (wantsX86Worker && projectDir.isDirectory())
    {
        const juce::StringArray x86BuildDirNames { "build-x86", "build_x86", "build32", "build-32" };

        for (const auto& dirName : x86BuildDirNames)
        {
            auto workerFromX86BuildLayout = projectDir
                .getChildFile (dirName)
                .getChildFile ("MyAppBridgeWorker_artefacts")
                .getChildFile (configDir.getFileName())
                .getChildFile ("MyAppBridgeWorker.exe");

            if (workerFromX86BuildLayout.existsAsFile())
                return workerFromX86BuildLayout;
        }
    }

    // No fallback to host exe: host should always remain a GUI app.
    return {};
}

bool MainComponent::ensureBridgeWorkerForPlugin (const juce::String& pluginPath, juce::String& errorMessage)
{
    const auto workerExecutable = findBridgeWorkerExecutableForPlugin (pluginPath);
    if (! workerExecutable.existsAsFile())
    {
        const auto pluginArch = detectWindowsBinaryArch (pluginPath);
        if (pluginArch == PluginBinaryArch::x86)
            errorMessage = "x86 bridge worker not found. Set MYAPP_BRIDGE_WORKER_X86_PATH or build build-x86/MyAppBridgeWorker.";
        else
            errorMessage = "Bridge worker executable not found.";

        bridgeAvailable = false;
        return false;
    }

    const bool needRelaunch = (! bridgeMaster.isWorkerRunning())
                              || (! activeBridgeWorkerExecutable.existsAsFile())
                              || (activeBridgeWorkerExecutable.getFullPathName() != workerExecutable.getFullPathName());

    if (needRelaunch)
    {
        bridgeMaster.shutdownWorker();
        bridgeAvailable = bridgeMaster.launchWorker (workerExecutable);

        if (! bridgeAvailable)
        {
            errorMessage = "Failed to launch bridge worker: " + workerExecutable.getFullPathName();
            return false;
        }

        activeBridgeWorkerExecutable = workerExecutable;

        const auto workerArch = detectWindowsBinaryArch (activeBridgeWorkerExecutable.getFullPathName());
        const juce::String workerArchText = workerArch == PluginBinaryArch::x86 ? "x86"
                                         : workerArch == PluginBinaryArch::x64 ? "x64"
                                                                               : "unknown";
        statusLabel.setText ("Bridge worker active: " + workerArchText + " ["
                             + activeBridgeWorkerExecutable.getFileName() + "]",
                             juce::dontSendNotification);
    }

    bridgeAvailable = true;
    return true;
}

void MainComponent::handleBridgeCommand (const myapp::bridge::IPCCommand& command)
{
    using myapp::bridge::IPCCommandType;

    switch (command.type)
    {
        case IPCCommandType::status:
        {
            juce::String statusText = "Bridge: " + command.payload;
            if (command.payload.startsWith ("loaded:"))
            {
                bridgePluginLoaded = true;
                statusLabel.setText ("Bridge plugin loaded: " + command.payload.fromFirstOccurrenceOf (":", false, false),
                                     juce::dontSendNotification);
            }
            else if (command.payload == "unloaded")
            {
                bridgePluginLoaded = false;
                closeBridgeAudioFiles();
                statusLabel.setText ("Bridge plugin unloaded", juce::dontSendNotification);
            }
            else if (command.payload.startsWith ("shared_memory_paths:"))
            {
                const auto paths = command.payload.fromFirstOccurrenceOf (":", false, false);
                const auto pair = juce::StringArray::fromTokens (paths, "|", "");
                if (pair.size() == 2)
                {
                    if (openBridgeAudioFiles (pair[0], pair[1]))
                        statusLabel.setText ("Bridge audio memory opened", juce::dontSendNotification);
                    else
                        statusLabel.setText ("Failed to open bridge audio memory", juce::dontSendNotification);
                }
                else
                {
                    statusLabel.setText ("Bridge audio path invalid", juce::dontSendNotification);
                }
            }
            else if (command.payload == "audio_done")
            {
                bridgeAudioProcessed.store (true);
            }
            else
            {
                statusLabel.setText (statusText, juce::dontSendNotification);
            }
            break;
        }

        case IPCCommandType::embedWindow:
        {
            // Payload is the plugin editor HWND encoded as hex
            const auto hwndValue = static_cast<juce::uint64> (
                command.payload.getHexValue64());

            if (hwndValue == 0)
            {
                statusLabel.setText ("Bridge reported invalid editor HWND",
                                     juce::dontSendNotification);
                break;
            }

            void* hwnd = reinterpret_cast<void*> (hwndValue);

            // Always touch UI on the message thread (we already are here)
            pluginSubWindowContainer.setEmbeddedWindowHandle (hwnd);
            const auto attachStatus = pluginSubWindowContainer.getLastAttachStatus();
            statusLabel.setText ("embed:" + attachStatus + " (HWND: " + command.payload + ")",
                                 juce::dontSendNotification);
            break;
        }

        default:
            break;
    }
}

bool MainComponent::openBridgeAudioFiles (const juce::String& inputPath, const juce::String& outputPath)
{
    closeBridgeAudioFiles();

    bridgeAudioInputFile = juce::File (inputPath);
    bridgeAudioOutputFile = juce::File (outputPath);

    if (! bridgeAudioInputFile.existsAsFile() || ! bridgeAudioOutputFile.existsAsFile())
        return false;

    bridgeAudioInputMemory.reset (new juce::MemoryMappedFile (bridgeAudioInputFile, juce::MemoryMappedFile::readWrite));
    bridgeAudioOutputMemory.reset (new juce::MemoryMappedFile (bridgeAudioOutputFile, juce::MemoryMappedFile::readWrite));

    return bridgeAudioInputMemory->getData() != nullptr && bridgeAudioOutputMemory->getData() != nullptr;
}

void MainComponent::closeBridgeAudioFiles()
{
    bridgeAudioInputMemory.reset();
    bridgeAudioOutputMemory.reset();
    bridgeAudioInputFile = {};
    bridgeAudioOutputFile = {};
}

void MainComponent::resetBridgePluginState()
{
    bridgePluginLoaded = false;
    bridgeAudioProcessed.store (false);
    closeBridgeAudioFiles();
}
