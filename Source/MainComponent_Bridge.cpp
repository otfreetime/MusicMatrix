#include "MainComponent.h"

//==============================================================================
// Bridge Management Implementation
//==============================================================================

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
                    juce::String payload = juce::String (currentSampleRate) + "," + juce::String (blockSize);
                    bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::setupAudio, payload });
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
            else if (command.payload.startsWith ("programs:"))
            {
                const auto programs = command.payload.fromFirstOccurrenceOf (":", false, false);
                const auto tokens = juce::StringArray::fromTokens (programs, "|", "");
                programNames = tokens;
                numPrograms = tokens.size();
                
                programSelector.clear();
                for (int i = 0; i < numPrograms; ++i)
                {
                    programSelector.addItem (programNames[i], i + 1);
                }
                programSelector.setEnabled (true);
                DEBUG_LOG ("MainComponent: Received " + juce::String (numPrograms) + " programs from VST2");
            }
            break;
        }

        default:
            break;
    }
}

void MainComponent::setCurrentProgram (int programIndex)
{
    currentProgramIndex = programIndex;
    
    if (auto* plugin = pluginManager.getLoadedPlugin())
    {
        plugin->setCurrentProgram (programIndex);
        DEBUG_LOG ("setCurrentProgram: VST3 plugin program changed to " + juce::String (programIndex));
    }
    else if (bridgePluginLoaded && bridgeManager.isAvailable())
    {
        // Send program change to VST2 via bridge
        bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::setProgram, juce::String (programIndex) });
        DEBUG_LOG ("setCurrentProgram: VST2 bridge program changed to " + juce::String (programIndex));
    }
}

void MainComponent::updateProgramSelector()
{
    programSelector.clear();
    programNames.clear();
    numPrograms = 0;
    currentProgramIndex = 0;
    
    if (auto* plugin = pluginManager.getLoadedPlugin())
    {
        numPrograms = plugin->getNumPrograms();
        for (int i = 0; i < numPrograms; ++i)
        {
            const juce::String programName = plugin->getProgramName (i);
            programNames.add (programName);
            programSelector.addItem (programName.isEmpty() ? ("Program " + juce::String (i + 1)) : programName, i + 1);
        }
        currentProgramIndex = plugin->getCurrentProgram();
        programSelector.setSelectedId (currentProgramIndex + 1, juce::dontSendNotification);
        programSelector.setEnabled (true);
    }
    else if (bridgePluginLoaded && bridgeManager.isAvailable())
    {
        // For VST2, programs will be populated from worker response
        programSelector.addItem ("Loading instruments...", 1);
        programSelector.setEnabled (false);
    }
    else
    {
        programSelector.addItem ("-- No Plugin Loaded --", 1);
        programSelector.setEnabled (false);
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
