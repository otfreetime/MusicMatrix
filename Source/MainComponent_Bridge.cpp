#include "MainComponent.h"

//==============================================================================
// Bridge Management Implementation
//==============================================================================

int MainComponent::getActualProgramIndexForDisplayIndex (int displayIndex) const
{
    if (displayIndex < 0)
        return -1;

    if (displayedProgramToActualIndex.isEmpty())
        return (displayIndex < numPrograms) ? displayIndex : -1;

    if (displayIndex >= displayedProgramToActualIndex.size())
        return -1;

    return displayedProgramToActualIndex[displayIndex];
}

int MainComponent::getDisplayProgramIndexForActualIndex (int actualIndex) const
{
    if (actualIndex < 0)
        return -1;

    if (displayedProgramToActualIndex.isEmpty())
        return (actualIndex < numPrograms) ? actualIndex : -1;

    return displayedProgramToActualIndex.indexOf (actualIndex);
}

void MainComponent::rebuildProgramSelectorsFromProgramNames()
{
    programSelector.clear();
    displayedProgramToActualIndex.clear();

    juce::StringArray displayedNames;
    bool addedEmptyEntry = false;

    for (int i = 0; i < numPrograms; ++i)
    {
        juce::String name = programNames[i].trim();
        const bool isEmptyLike = name.isEmpty() || name.equalsIgnoreCase ("Empty");

        if (isEmptyLike)
        {
            if (addedEmptyEntry)
                continue;

            addedEmptyEntry = true;
            name = "Empty";
        }

        displayedProgramToActualIndex.add (i);
        displayedNames.add (name);
        programSelector.addItem (name, programSelector.getNumItems() + 1);
    }

    const bool hasItems = programSelector.getNumItems() > 0;
    programSelector.setEnabled (hasItems);

    int selectedDisplayIndex = getDisplayProgramIndexForActualIndex (currentProgramIndex);
    if (selectedDisplayIndex < 0 && hasItems)
    {
        selectedDisplayIndex = 0;
        currentProgramIndex = displayedProgramToActualIndex[0];
    }

    if (selectedDisplayIndex >= 0)
        programSelector.setSelectedId (selectedDisplayIndex + 1, juce::dontSendNotification);

    pluginSubWindowContainer.setProgramList (displayedNames,
                                             juce::jmax (0, selectedDisplayIndex),
                                             hasItems);
}

void MainComponent::initialiseBridge()
{
    bridgeManager.setCommandCallback ([this] (const myapp::bridge::IPCCommand& command)
    {
        // Use SafePointer so the callAsync is a no-op if MainComponent was destroyed
        // before this message is dispatched (e.g. rapid exit during plugin load).
        auto safeThis = juce::Component::SafePointer<MainComponent> (this);
        juce::MessageManager::callAsync ([safeThis, command]
        {
            if (safeThis != nullptr)
                safeThis->handleBridgeCommand (command);
        });
    });

    auto safeThis = juce::Component::SafePointer<MainComponent> (this);
    juce::Thread::launch ([safeThis]
    {
        if (safeThis == nullptr)
            return;

        // BridgeManager handles finding and launching the worker
        safeThis->bridgeManager.initialise ([safeThis] (bool success)
        {
            if (safeThis == nullptr)
                return;

            if (! success)
            {
                safeThis->uiController.setStatusMessage (
                    "Bridge unavailable (local mode)");
            }
            else
            {
                safeThis->uiController.setStatusMessage (
                    "Bridge worker launched: " + 
                    safeThis->bridgeManager.getActiveWorkerExecutable().getFileName());
            }
        });
    });
}

void MainComponent::handleBridgeCommand (const myapp::bridge::IPCCommand& command)
{
    DEBUG_LOG ("MainComponent: handleBridgeCommand called - type=" + juce::String (static_cast<int> (command.type)) 
              + " payload=" + command.payload);
    
    switch (command.type)
    {
        case myapp::bridge::IPCCommandType::status:
        {
            const auto& payload = command.payload;
            
            DEBUG_LOG ("MainComponent: Received status payload: " + payload);
            
            // Log all status messages for debugging
            uiController.setStatusMessage ("Bridge status: " + payload);
            
            if (payload.startsWith ("editor_hwnd_ready"))
            {
                uiController.setStatusMessage ("Bridge editor HWND ready");
            }
            else if (payload.startsWith ("embed_sent:"))
            {
                // Extract HWND from hex string and embed
                const auto hwndHex = payload.fromFirstOccurrenceOf (":", false, false);
                const auto hwndValue = (juce::uint64) hwndHex.getHexValue64();
                if (hwndValue != 0)
                {
                    void* hwnd = reinterpret_cast<void*> (static_cast<uintptr_t> (hwndValue));
                    pluginSubWindowContainer.setEmbeddedWindowHandle (hwnd);
                    pluginSubWindowContainer.setVisible (true);
                    pluginSubWindowContainer.toFront (true);
                    uiController.setStatusMessage ("Embedded VST2 editor loaded");
                }
                else
                {
                    uiController.setStatusMessage ("Bridge: invalid editor HWND");
                }
            }
            else if (payload == "shared_memory_ready")
            {
                // Bridge audio shared memory is ready
                bridgePluginLoaded = true;
                
                // CRITICAL FIX: Send setupAudio command immediately so worker can start processing
                // This ensures isProcessing=true in worker even if prepareToPlay() hasn't been called yet
                if (currentSampleRate > 0 && blockSize > 0)
                {
                    juce::String setupAudioPayload = juce::String (currentSampleRate) + "," + juce::String (blockSize);
                    bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::setupAudio, setupAudioPayload });
                    DEBUG_LOG ("MainComponent: Sent setupAudio command to worker (sampleRate=" 
                              + juce::String (currentSampleRate) + ", blockSize=" + juce::String (blockSize) + ")");
                }
                else
                {
                    // Fallback: use default values if audio device hasn't started yet
                    DEBUG_LOG ("MainComponent: Audio device not started, sending default setupAudio (44100/512)");
                    bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::setupAudio, "44100,512" });
                }
            }
            else if (payload.startsWith ("shared_memory_paths:"))
            {
                const auto paths = payload.fromFirstOccurrenceOf (":", false, false);
                const auto inputPath = paths.upToFirstOccurrenceOf ("|", false, false);
                const auto outputPath = paths.fromFirstOccurrenceOf ("|", false, false);

                DBG ("Bridge: opening audio files:");
                DBG ("  input:  " + inputPath);
                DBG ("  output: " + outputPath);

                if (bridgeManager.openAudioFiles (inputPath, outputPath))
                {
                    uiController.setStatusMessage ("Bridge audio path connected");
                    DBG ("Bridge: audio shared memory opened OK");
                }
                else
                {
                    uiController.setStatusMessage ("Bridge: failed to open audio memory files");
                    DBG ("Bridge: FAILED to open audio memory files");
                    DBG ("  input exists: " + juce::String (juce::File (inputPath).existsAsFile() ? "yes" : "no"));
                    DBG ("  output exists: " + juce::String (juce::File (outputPath).existsAsFile() ? "yes" : "no"));
                }
            }
            else if (payload.startsWith ("programs:"))
            {
                const auto programs = payload.fromFirstOccurrenceOf (":", false, false);
                const auto tokens = juce::StringArray::fromTokens (programs, "|", "");

                programNames = tokens;
                numPrograms = tokens.size();
                currentProgramIndex = 0;

                rebuildProgramSelectorsFromProgramNames();
                refreshKeyboardProgramNoteLabels();

                DEBUG_LOG ("MainComponent: Received " + juce::String (numPrograms) + " programs from worker status");
            }
            else if (payload == "plugin_reset")
            {
                currentProgramIndex = 0;
                const int selectedDisplayIndex = getDisplayProgramIndexForActualIndex (currentProgramIndex);
                if (selectedDisplayIndex >= 0)
                {
                    programSelector.setSelectedId (selectedDisplayIndex + 1, juce::dontSendNotification);
                    pluginSubWindowContainer.setSelectedProgramIndex (selectedDisplayIndex);
                }

                uiController.setStatusMessage ("VST2 Rest complete");
            }
            else if (payload.startsWith ("warning:") || payload.startsWith ("error:"))
            {
                uiController.setStatusMessage ("Bridge: " + payload);
            }
            
            break;
        }

        case myapp::bridge::IPCCommandType::embedWindow:
        {
            // Handle window embedding
            DBG("MainComponent: Received embedWindow: " + command.payload);
            const auto hwndHex = command.payload;
            if (hwndHex.isNotEmpty())
            {
                const auto hwndValue = (juce::uint64) hwndHex.getHexValue64();
                if (hwndValue != 0)
                {
                    void* hwnd = reinterpret_cast<void*> (static_cast<uintptr_t> (hwndValue));
                    DBG("MainComponent: Embedding window handle: " + juce::String::toHexString((juce::int64)hwnd));
                    pluginSubWindowContainer.setEmbeddedWindowHandle (hwnd);
                    pluginSubWindowContainer.setVisible (true);
                    pluginSubWindowContainer.toFront (true);
                    uiController.setStatusMessage ("Embedded VST2 editor loaded");
                }
                else
                {
                    uiController.setStatusMessage ("Bridge: invalid editor HWND");
                }
            }
            break;
        }

        default:
            break;
    }
}

void MainComponent::setCurrentProgram (int programIndex)
{
    // CRITICAL: Stop all playing notes before changing program to prevent تداخل الأصوات
    // (sound interference/mixing) where old instrument's notes overlap with new instrument
    allNotesOff();
    
    if (numPrograms > 0)
        currentProgramIndex = juce::jlimit (0, numPrograms - 1, programIndex);
    else
        currentProgramIndex = programIndex;

    const int selectedDisplayIndex = getDisplayProgramIndexForActualIndex (currentProgramIndex);
    if (selectedDisplayIndex >= 0)
    {
        programSelector.setSelectedId (selectedDisplayIndex + 1, juce::dontSendNotification);
        pluginSubWindowContainer.setSelectedProgramIndex (selectedDisplayIndex);
    }
    
    if (auto* plugin = pluginManager.getLoadedPlugin())
    {
        plugin->setCurrentProgram (currentProgramIndex);
        DEBUG_LOG ("setCurrentProgram: VST3 plugin program changed to " + juce::String (programIndex));
    }
    else if (bridgePluginLoaded && bridgeManager.isAvailable())
    {
        // Send program change to VST2 via bridge
        bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::setProgram, juce::String (currentProgramIndex) });
        DEBUG_LOG ("setCurrentProgram: VST2 bridge program changed to " + juce::String (programIndex));
    }

    refreshKeyboardProgramNoteLabels();
}

void MainComponent::updateProgramSelector()
{
    programSelector.clear();
    programNames.clear();
    displayedProgramToActualIndex.clear();
    numPrograms = 0;
    currentProgramIndex = 0;
    
    if (auto* plugin = pluginManager.getLoadedPlugin())
    {
        numPrograms = plugin->getNumPrograms();
        for (int i = 0; i < numPrograms; ++i)
        {
            const juce::String programName = plugin->getProgramName (i);
            programNames.add (programName);
        }
        currentProgramIndex = plugin->getCurrentProgram();
        rebuildProgramSelectorsFromProgramNames();
        refreshKeyboardProgramNoteLabels();
    }
    else if (bridgePluginLoaded && bridgeManager.isAvailable())
    {
        // For VST2, programs will be populated from worker response
        programSelector.addItem ("Loading instruments...", 1);
        programSelector.setEnabled (false);
        refreshKeyboardProgramNoteLabels();
        displayedProgramToActualIndex.clear();
        pluginSubWindowContainer.clearProgramList ("Loading instruments...");
    }
    else
    {
        programSelector.addItem ("-- No Plugin Loaded --", 1);
        programSelector.setEnabled (false);
        displayedProgramToActualIndex.clear();
        pluginSubWindowContainer.clearProgramList ("-- No Plugin Loaded --");
    }
}

bool MainComponent::openBridgeAudioFiles (const juce::String& inputPath, const juce::String& outputPath)
{
    return bridgeManager.openAudioFiles (inputPath, outputPath);
}

void MainComponent::closeBridgeAudioFiles()
{
    bridgeManager.closeAudioFiles();
}

void MainComponent::resetBridgePluginState()
{
    bridgePluginLoaded = false;
}
