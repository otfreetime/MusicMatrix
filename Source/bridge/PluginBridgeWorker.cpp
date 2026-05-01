#include "PluginBridgeWorker.h"
#include "AudioSharedMemory.h"
#include "../debug/DebugLogger.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

// Forward declaration for SEH helper - defined at end of file to avoid namespace issues
#if JUCE_WINDOWS
extern "C" void CallProcessBlockWithSEH (juce::AudioPluginInstance* instance,
                                         juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midi);
#endif

namespace
{
class SafeFallbackEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SafeFallbackEditor (juce::AudioProcessor& processor, bool isDetached)
        : juce::AudioProcessorEditor (processor), detachedMode (isDetached)
    {
        setSize (760, 520);

        if (isDetached)
        {
            // Build a simple parameter list for detached mode
            for (auto* param : processor.getParameters())
            {
                auto* slider = sliders.add (new juce::Slider());
                slider->setRange (0.0, 1.0);
                slider->setValue (param->getValue(), juce::dontSendNotification);
                slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 20);
                slider->onValueChange = [slider, param]
                {
                    param->setValueNotifyingHost ((float) slider->getValue());
                };
                addAndMakeVisible (slider);

                auto* label = labels.add (new juce::Label ({}, param->getName (32)));
                label->setFont (juce::Font (juce::FontOptions (12.0f)));
                label->setColour (juce::Label::textColourId, juce::Colours::lightgrey);
                addAndMakeVisible (label);
            }
        }
    }

    void resized() override
    {
        if (! detachedMode) return;
        auto area = getLocalBounds().reduced (12);
        area.removeFromTop (36);
        int rowH = 28;
        for (int i = 0; i < sliders.size(); ++i)
        {
            auto row = area.removeFromTop (rowH).reduced (2);
            labels[i]->setBounds (row.removeFromLeft (180));
            sliders[i]->setBounds (row);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF1E1E1E));
        g.setColour (juce::Colour (0xFFFFBF00));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (4.0f), 6.0f, 1.5f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));

        if (detachedMode)
        {
            g.drawText (getAudioProcessor()->getName() + "  [Bridge UI]",
                        getLocalBounds().removeFromTop (34).reduced (12, 0),
                        juce::Justification::centredLeft);
        }
        else
        {
            g.drawFittedText ("Plugin native UI crashed (isolated in bridge worker)",
                              getLocalBounds().reduced (24, 40),
                              juce::Justification::centredTop, 2);
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.setColour (juce::Colours::lightgrey);
            g.drawFittedText ("MyApp stays stable using a safe fallback view.",
                              getLocalBounds().reduced (24, 90),
                              juce::Justification::centredTop, 2);
        }
    }

private:
    bool detachedMode { false };
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label>  labels;
};
}

namespace myapp::bridge
{
PluginBridgeWorker::PluginBridgeWorker()
{
#if JUCE_PLUGINHOST_VST3
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
#endif
#if JUCE_PLUGINHOST_VST
    formatManager.addFormat (std::make_unique<juce::VSTPluginFormat>());
#endif
}

PluginBridgeWorker::~PluginBridgeWorker()
{
    stop();
}

bool PluginBridgeWorker::start (const juce::String& commandLine)
{
    juce::File logFile (juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                            .getChildFile ("MyApp_worker_log.txt"));
    juce::FileOutputStream stream (logFile);
    stream.writeText ("PluginBridgeWorker: Starting (ChildProcessWorker)\n", false, false, nullptr);

    // initialiseFromCommandLine parses the coordinator's handshake token from
    // the command line and establishes the pipe — no manual pipe name needed.
    if (! ipc.initialiseFromCommandLine (commandLine, "myapp-bridge-worker", 5000))
    {
        lastError = "Failed to connect to coordinator";
        stream.writeText ("PluginBridgeWorker: Failed to connect to coordinator\n", false, false, nullptr);
        return false;
    }

    stream.writeText ("PluginBridgeWorker: Connected to coordinator\n", false, false, nullptr);

    lastError.clear();
    running = true;
    sendStatus ("worker_connected:" + juce::String ((int) (sizeof (void*) * 8)) + "bit");
    stream.writeText ("PluginBridgeWorker: Sent worker_connected\n", false, false, nullptr);
    return true;
}

void PluginBridgeWorker::stop()
{
    hwndRetryTimer.reset();
    running = false;
    loadedPluginPath.clear();
    cleanupSharedMemory();
    isProcessing = false;
    // ipc (WorkerIPC/ChildProcessWorker) is destroyed with the object.
}

bool PluginBridgeWorker::loadPlugin (const juce::String& pluginPath)
{
    if (! running)
    {
        lastError = "Worker is not running";
        return false;
    }

    sendStatus ("loadPlugin_start: " + pluginPath);
    DBG ("PluginBridgeWorker: Starting plugin load: " + pluginPath);

    // Describe the plugin by path
    juce::OwnedArray<juce::PluginDescription> results;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        formatManager.getFormat (i)->findAllTypesForFile (results,
                                                          pluginPath);
        if (! results.isEmpty())
            break;
    }

    if (results.isEmpty())
    {
        lastError = "No plugin types found in: " + pluginPath;
        sendStatus ("error: no_plugin_types_found: " + pluginPath);
        return false;
    }

    sendStatus ("plugin_types_found: " + juce::String (results.size()));
    DBG ("PluginBridgeWorker: Plugin types found: " + juce::String (results.size()));

    juce::String errorMessage;
    pluginInstance = formatManager.createPluginInstance (
        *results[0], 44100.0, 512, errorMessage);

    if (pluginInstance == nullptr)
    {
        lastError = "Failed to create plugin instance: " + errorMessage;
        sendStatus ("error: create_instance_failed: " + errorMessage);
        return false;
    }

    sendStatus ("plugin_instance_created");
    DEBUG_LOG ("PluginBridgeWorker: Plugin instance created");

    // Use default values initially, but host will override via setupAudio command
    sampleRate = 44100;
    blockSize = 512;
    
    // Prepare audio buffer with default size (will be resized by setupAudio)
    audioBuffer.setSize (2, blockSize);
    audioBuffer.clear();
    midiBuffer.clear();
    
    DEBUG_LOG ("PluginBridgeWorker: Waiting for setupAudio command from host");
    
    // Setup shared memory for audio I/O
    if (setupSharedMemory())
    {
        DEBUG_LOG ("PluginBridgeWorker: Shared memory setup successful");
        
        // CRITICAL: Send status messages to host so it knows to send setupAudio command
        sendStatus ("shared_memory_ready");
        sendStatus ("shared_memory_paths:" + inputMemoryFile.getFullPathName() + "|" + outputMemoryFile.getFullPathName());
        
        DEBUG_LOG ("PluginBridgeWorker: Sent shared_memory_ready and paths to host");
        
        // Start a real-time thread to drive audio processing
        juce::MessageManager::callAsync ([this]
        {
            DEBUG_LOG ("PluginBridgeWorker: Starting real-time audio thread");
            if (! audioWorkerThread)
            {
                audioWorkerThread = std::make_unique<AudioWorkerThread> (*this);
                audioWorkerThread->startThread (juce::Thread::Priority::highest);
            }
        });
    }
    else
    {
        DEBUG_LOG ("PluginBridgeWorker: Shared memory setup FAILED");
        sendStatus ("warning:shared_memory_failed");
    }
    
    // Query plugin programs and send to host
    if (pluginInstance != nullptr)
    {
        const int numProgs = pluginInstance->getNumPrograms();
        DEBUG_LOG ("PluginBridgeWorker: Plugin has " + juce::String (numProgs) + " programs");
        
        juce::StringArray programList;
        for (int i = 0; i < numProgs; ++i)
        {
            const juce::String progName = pluginInstance->getProgramName (i);
            programList.add (progName.isEmpty() ? ("Program " + juce::String (i + 1)) : progName);
        }
        
        if (programList.size() > 0)
        {
            const juce::String programsJoined = programList.joinIntoString ("|");
            sendStatus ("programs:" + programsJoined);
            DEBUG_LOG ("PluginBridgeWorker: Sent " + juce::String (programList.size()) + " programs to host");
        }
    }
    
    sendStatus ("plugin_prepared");
    DBG ("PluginBridgeWorker: Plugin prepared, creating editor");

    loadedPluginPath = pluginPath;
    createPluginEditor();
    DBG ("PluginBridgeWorker: loadPlugin() returning true");
    return true;
}

void PluginBridgeWorker::createPluginEditor()
{
    if (pluginInstance == nullptr)
        return;

    // Editor must be created on message thread
    juce::MessageManager::callAsync ([this]
    {
        DBG ("PluginBridgeWorker: createPluginEditor() on message thread");
        sendStatus ("editor_create_begin");
        sendStatus ("editor_create_skip_hasEditor_check");

        juce::AudioProcessorEditor* rawEditor = nullptr;
        bool useFallback = nativeEditorCrashPluginPaths.contains (loadedPluginPath);
        bool sehCrashed = false;

        if (detached)
            sendStatus ("detached_mode_attempt_native_ui");
        else if (useFallback)
            sendStatus ("warning:editor_native_skipped_cached_crash");

        sendStatus ("editor_create_call_begin");

       #if JUCE_WINDOWS
        if (! useFallback)
        {
            rawEditor = pluginInstance->createEditor();
        }
       #else
        if (! useFallback)
            rawEditor = pluginInstance->createEditorIfNeeded();
       #endif

        sendStatus ("editor_create_call_end");

        if (useFallback)
        {
            if (sehCrashed)
                sendStatus ("warning:editor_seh_crash");
            sendStatus ("editor_fallback_generic_begin");
            rawEditor = new SafeFallbackEditor (*pluginInstance, detached);
            sendStatus ("editor_fallback_generic_end");
        }

        pluginEditor.reset (rawEditor);

        if (pluginEditor != nullptr)
        {
            if (detached)
            {
                sendStatus ("detached_creating_window");
                if (pluginEditor->getWidth() <= 0 || pluginEditor->getHeight() <= 0)
                    pluginEditor->setSize (800, 600);

               #if JUCE_WINDOWS
                // Create movable top-level window with title bar
                pluginEditor->addToDesktop (juce::ComponentPeer::windowHasTitleBar
                                            | juce::ComponentPeer::windowIsResizable
                                            | juce::ComponentPeer::windowHasCloseButton);
                if (auto* peer = pluginEditor->getPeer())
                {
                    if (auto hwnd = (HWND) peer->getNativeHandle())
                    {
                        SetWindowPos (hwnd, HWND_TOP, 0, 0, 800, 600, SWP_NOMOVE | SWP_SHOWWINDOW);
                        ShowWindow (hwnd, SW_SHOW);
                        SetForegroundWindow (hwnd);
                        sendStatus ("detached_native_ui_visible");
                    }
                }
               #else
                pluginEditor->setVisible (true);
                sendStatus ("detached_window_shown");
               #endif
            }
            else
            {
                if (pluginEditor->getWidth() <= 0 || pluginEditor->getHeight() <= 0)
                    pluginEditor->setSize (700, 500);

                pluginEditor->setVisible (true);
                pluginEditor->addToDesktop (juce::ComponentPeer::windowHasTitleBar
                                            | juce::ComponentPeer::windowIsResizable);
                pluginEditor->toFront (true);
                sendStatus (useFallback ? "editor_created_generic_fallback"
                                        : "editor_created");

                // Give the OS time to create the native window before querying HWND
                publishEditorWindowHandleWithRetry (50);
            }
        }
        else
        {
            sendStatus ("error:editor_creation_failed");
        }
    });
}

void PluginBridgeWorker::publishEditorWindowHandleWithRetry (int attemptsRemaining)
{
    // Try immediately first
    void* hwnd = nullptr;
   #if JUCE_WINDOWS
    if (pluginEditor != nullptr)
        if (auto* peer = pluginEditor->getPeer())
            hwnd = peer->getNativeHandle();
   #endif

    if (hwnd != nullptr)
    {
        sendStatus ("editor_hwnd_ready_immediate");
        sendEmbedWindow (hwnd);
        return;
    }

    // Schedule timer-based polling (100 ms interval) so we don't busy-spin
    sendStatus ("editor_hwnd_poll_start");
    hwndRetryTimer = std::make_unique<HWNDRetryTimer> (*this, attemptsRemaining);
    hwndRetryTimer->startTimer (100);
}

void PluginBridgeWorker::AudioWorkerThread::run()
{
    DEBUG_LOG ("AudioWorkerThread: Started running");
    
    while (! threadShouldExit())
    {
        if (ownerWorker.isProcessing && ownerWorker.pluginInstance != nullptr)
        {
            // Try to tick audio. If it didn't process anything (buffer full/empty), yield briefly.
            int processed = 0;
            if (ownerWorker.audioOutputMemory)
            {
                const int freeSpace = audioRingBufferGetFreeSpace(ownerWorker.audioOutputMemory->getData());
                if (freeSpace >= ownerWorker.blockSize)
                {
                    ownerWorker.tickAudio();
                    processed = 1;
                }
            }
            
            if (processed == 0)
                wait (1); // 1ms sleep prevents busy-spinning when buffer is full
        }
        else
        {
            wait (5); // idle state
        }
    }
    
    DEBUG_LOG ("AudioWorkerThread: Exiting");
}

void PluginBridgeWorker::tickAudio()
{
    applyPendingRealtimeControlCommands();

    // Only call processBlock when there is room in the ring buffer.
    // This naturally throttles production to match the host consumption rate
    // and prevents crashing plugins that misbehave under continuous rapid calls.
    if (audioOutputMemory)
    {
        const int freeSpace = myapp::bridge::audioRingBufferGetFreeSpace (audioOutputMemory->getData());
        if (freeSpace < blockSize)
            return; // Buffer is full enough; skip this tick
    }

    processAudioBlockInternal (audioBuffer, midiBuffer);
}

void PluginBridgeWorker::applyPendingRealtimeControlCommands()
{
    if (pluginInstance == nullptr)
        return;

    const bool shouldPanic = pendingPanic.exchange (false);
    const int requestedProgram = pendingProgramIndex.exchange (-1);

    if (! shouldPanic && requestedProgram < 0)
        return;

    if (shouldPanic)
    {
        midiBuffer.clear();

        for (int channel = 1; channel <= 16; ++channel)
        {
            for (int note = 0; note < 128; ++note)
                midiBuffer.addEvent (juce::MidiMessage::noteOff (channel, note), 0);

            midiBuffer.addEvent (juce::MidiMessage::controllerEvent (channel, 123, 0), 0);
            midiBuffer.addEvent (juce::MidiMessage::controllerEvent (channel, 120, 0), 0);
            midiBuffer.addEvent (juce::MidiMessage::pitchWheel (channel, 8192), 0);
        }

        pluginInstance->reset();
        sendStatus ("voices_cleared");
        DEBUG_LOG ("PluginBridgeWorker: Panic - voices cleared");
    }

    if (requestedProgram >= 0)
    {
        midiBuffer.clear();

        pluginInstance->setCurrentProgram (requestedProgram);
        pluginInstance->reset();

        sendStatus ("program_changed:" + juce::String (requestedProgram));
        DEBUG_LOG ("PluginBridgeWorker: Program changed to " + juce::String (requestedProgram));
    }
}

// Internal version that does the actual processing - called from SEH wrapper
void PluginBridgeWorker::processAudioBlockInternal (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (! pluginInstance || ! audioOutputMemory || ! isProcessing)
        return;

    // IMPORTANT: Ensure the buffer is the exact size required by the current blockSize.
    // If setupAudio or loadPlugin skipped resizing, buffer.getNumSamples() would be 0, causing complete silence.
    if (buffer.getNumSamples() != blockSize || buffer.getNumChannels() != 2)
        buffer.setSize (2, blockSize);

    // Clear input buffer (MIDI instrument — no audio input needed)
    buffer.clear();

    // Use the accumulated MIDI buffer (contains notes from processMidi IPC commands)
    // This is critical for keyboard sound - DO NOT use emptyMidi!
    pluginInstance->processBlock (buffer, midi);

    // Clear MIDI buffer after processing to prevent stuck notes
    midi.clear();

    // Push processed audio into the ring buffer so the host can consume it
    // at its own callback rate without tearing.
    const float* channels[2] = { buffer.getReadPointer (0),
                                  buffer.getNumChannels() > 1 ? buffer.getReadPointer (1)
                                                               : buffer.getReadPointer (0) };
    myapp::bridge::audioRingBufferWrite (audioOutputMemory->getData(), channels, buffer.getNumSamples());
}void PluginBridgeWorker::unloadPlugin()
{
    juce::MessageManager::callAsync ([this]
    {
        if (pluginInstance != nullptr)
        {
            pluginInstance.reset();
        }

        hwndRetryTimer.reset();
        // audioWorkerThread was removed here because we do it in unloadPlugin or already removed it earlier

        loadedPluginPath.clear();
        cleanupSharedMemory();
    });
}

juce::String PluginBridgeWorker::getLastError() const
{
    return lastError;
}

void PluginBridgeWorker::handleCommand (const IPCCommand& command)
{
    DBG ("PluginBridgeWorker::handleCommand ENTER - type=" + juce::String (static_cast<int> (command.type)) 
        + " payload=" + command.payload);
    
    if (command.type == IPCCommandType::loadPlugin)
    {
        DBG("PluginBridgeWorker: Handling loadPlugin");
        loadPlugin (command.payload);
    }
    else if (command.type == IPCCommandType::setupAudio)
    {
        DEBUG_LOG ("PluginBridgeWorker: Handling setupAudio - " + command.payload);
        auto tokens = juce::StringArray::fromTokens (command.payload, ",", "");
        if (tokens.size() == 2)
        {
            sampleRate = tokens[0].getDoubleValue();
            blockSize = tokens[1].getIntValue();
            
            DEBUG_LOG ("PluginBridgeWorker: Setting sampleRate=" + juce::String(sampleRate) 
                      + ", blockSize=" + juce::String(blockSize));
            
            if (pluginInstance != nullptr)
            {
                pluginInstance->setPlayConfigDetails (2, 2, sampleRate, blockSize);
                pluginInstance->prepareToPlay (sampleRate, blockSize);
                DEBUG_LOG ("PluginBridgeWorker: Plugin prepareToPlay called");
            }
            
            // CRITICAL: Resize audio buffer to match new block size
            audioBuffer.setSize (2, blockSize);
            audioBuffer.clear();
            DEBUG_LOG ("PluginBridgeWorker: audioBuffer resized to " + juce::String(blockSize) + " samples");
            
            if (audioInputMemory || audioOutputMemory)
            {
                // Re-initialize ring buffer sizes based on block size (e.g., 4x blockSize)
                int dynamicCapacity = 4 * blockSize;
                if (dynamicCapacity <= 0) dynamicCapacity = 8190;
                
                if (audioInputMemory)
                {
                    audioRingBufferInit (audioInputMemory->getData(), dynamicCapacity);
                    DEBUG_LOG ("PluginBridgeWorker: Input ring buffer reinitialized, capacity=" + juce::String(dynamicCapacity));
                }
                if (audioOutputMemory)
                {
                    audioRingBufferInit (audioOutputMemory->getData(), dynamicCapacity);
                    DEBUG_LOG ("PluginBridgeWorker: Output ring buffer reinitialized, capacity=" + juce::String(dynamicCapacity));
                }
            }
            
            isProcessing = true;
            DEBUG_LOG ("PluginBridgeWorker: isProcessing=true, audio thread can now process");
        }
        else
        {
            DEBUG_LOG ("PluginBridgeWorker: setupAudio parsing failed - expected 'sampleRate,blockSize'");
        }
    }
    else if (command.type == IPCCommandType::unloadPlugin)
    {
        DBG("PluginBridgeWorker: Handling unloadPlugin");
        unloadPlugin();
        sendStatus ("plugin_unloaded");
    }
    else if (command.type == IPCCommandType::setProgram)
    {
        const int programIndex = command.payload.getIntValue();
        if (pluginInstance != nullptr && programIndex >= 0)
        {
            pendingPanic.store (true);
            pendingProgramIndex.store (programIndex);
        }
    }
    else if (command.type == IPCCommandType::setDetached)
    {
        detached = (command.payload == "true");
        DBG("PluginBridgeWorker: setDetached: " + command.payload);
        sendStatus ("detached_mode_set:" + command.payload);
    }
    else if (command.type == IPCCommandType::processAudio)
    {
        tickAudio();
    }
    else if (command.type == IPCCommandType::processMidi)
    {
        const int midiData = command.payload.getIntValue();
        const int status = (midiData >> 16) & 0xFF;
        const int data1 = (midiData >> 8) & 0xFF;
        const int data2 = midiData & 0xFF;
        const int statusType = status & 0xF0;
        const int channel = (status & 0x0F) + 1;
        
        DBG("PluginBridgeWorker: Received MIDI - status=0x" + juce::String::toHexString (status) 
            + " data1=" + juce::String (data1) + " data2=" + juce::String (data2));
        
        if (statusType == 0x90) // Note On
        {
            if (data2 == 0)
                midiBuffer.addEvent (juce::MidiMessage::noteOff (channel, data1), 0);
            else
                midiBuffer.addEvent (juce::MidiMessage::noteOn (channel, data1, static_cast<float> (data2) / 127.0f), 0);

            DBG("PluginBridgeWorker: Note On added to buffer");
        }
        else if (statusType == 0x80) // Note Off
        {
            midiBuffer.addEvent (juce::MidiMessage::noteOff (channel, data1), 0);
            DBG("PluginBridgeWorker: Note Off added to buffer");
        }
        else if (statusType == 0xE0) // Pitch Bend
        {
            const int pitchValue = (data2 << 7) | data1;
            midiBuffer.addEvent (juce::MidiMessage::pitchWheel (channel, pitchValue), 0);
        }
        else if (statusType == 0xB0) // Control Change
        {
            midiBuffer.addEvent (juce::MidiMessage::controllerEvent (channel, data1, data2), 0);
        }
    }
    else if (command.type == IPCCommandType::panic)
    {
        if (pluginInstance != nullptr)
        {
            pendingPanic.store (true);
        }
    }
    else if (command.type == IPCCommandType::resetPluginState)
    {
        if (pluginInstance != nullptr)
        {
            midiBuffer.clear();

            for (auto* parameter : pluginInstance->getParameters())
            {
                if (parameter != nullptr)
                    parameter->setValueNotifyingHost (parameter->getDefaultValue());
            }

            if (pluginInstance->getNumPrograms() > 0)
                pluginInstance->setCurrentProgram (0);

            pluginInstance->reset();
            DEBUG_LOG ("PluginBridgeWorker: Plugin reset to default state");
            sendStatus ("plugin_reset");
        }
    }
    else
    {
        DBG("PluginBridgeWorker: Unknown command: " + juce::String(static_cast<int>(command.type)));
        sendStatus ("warning:unknown_command:" + juce::String (static_cast<int> (command.type)));
    }
}

void PluginBridgeWorker::sendStatus (const juce::String& text)
{
    const auto cmd = ipcSerialize ({ IPCCommandType::status, text });
    ipc.sendMessageToCoordinator (
        juce::MemoryBlock (cmd.toRawUTF8(), static_cast<size_t> (cmd.getNumBytesAsUTF8())));
}

void PluginBridgeWorker::sendEmbedWindow (void* hwnd)
{
    const auto hwndValue = reinterpret_cast<juce::uint64> (hwnd);
    const juce::String payload = juce::String::toHexString (static_cast<juce::int64> (hwndValue));
    DBG ("PluginBridgeWorker: Sending embedWindow: " + payload);
    sendStatus ("embed_sent:" + payload);
    const auto cmd = ipcSerialize ({ IPCCommandType::embedWindow, payload });
    ipc.sendMessageToCoordinator (
        juce::MemoryBlock (cmd.toRawUTF8(), static_cast<size_t> (cmd.getNumBytesAsUTF8())));
}

bool PluginBridgeWorker::setupSharedMemory()
{
   #if JUCE_WINDOWS
    auto pid = juce::String (GetCurrentProcessId());
    sharedMemoryNameInput  = "MyAppBridge_AudioInput_"  + pid;
    sharedMemoryNameOutput = "MyAppBridge_AudioOutput_" + pid;

    const size_t fileSize = static_cast<size_t> (myapp::bridge::kAudioSharedMemTotalSize);

    auto tempDir   = juce::File::getSpecialLocation (juce::File::tempDirectory);
    auto inputFile  = tempDir.getChildFile ("myapp_audio_input_"  + pid + ".dat");
    auto outputFile = tempDir.getChildFile ("myapp_audio_output_" + pid + ".dat");

    // Create files of the exact required size.
    for (auto& f : { &inputFile, &outputFile })
    {
        f->deleteFile();
        juce::FileOutputStream out (*f);
        out.setPosition ((juce::int64) fileSize - 1);
        out.writeByte (0);
    }

    audioInputMemory .reset (new juce::MemoryMappedFile (inputFile,  juce::MemoryMappedFile::readWrite));
    audioOutputMemory.reset (new juce::MemoryMappedFile (outputFile, juce::MemoryMappedFile::readWrite));

    if (audioInputMemory->getData() && audioOutputMemory->getData())
    {
        // Initialise ring buffer header + zero audio region.
        std::memset (audioInputMemory->getData(),  0, fileSize);
        myapp::bridge::audioRingBufferInit (audioOutputMemory->getData());

        inputMemoryFile  = inputFile;
        outputMemoryFile = outputFile;

        sendStatus ("shared_memory_created:" + juce::String ((int) fileSize));
        return true;
    }
   #endif

    sendStatus ("error:shared_memory_failed");
    return false;
}

void PluginBridgeWorker::cleanupSharedMemory()
{
    audioInputMemory.reset();
    audioOutputMemory.reset();
    
    // Delete temporary files
    if (inputMemoryFile.existsAsFile())
        inputMemoryFile.deleteFile();
    if (outputMemoryFile.existsAsFile())
        outputMemoryFile.deleteFile();
    
    inputMemoryFile = juce::File();
    outputMemoryFile = juce::File();
    sharedMemoryNameInput.clear();
    sharedMemoryNameOutput.clear();
}

void PluginBridgeWorker::processPluginAudio (float** inputChannels, float** outputChannels, int numSamples)
{
    if (! pluginInstance || ! isProcessing)
        return;
    
    // Copy input from shared memory to local buffer
    audioBuffer.setSize (2, numSamples, false, false, true);
    for (int ch = 0; ch < 2; ++ch)
    {
        if (inputChannels && inputChannels[ch])
            juce::FloatVectorOperations::copy (audioBuffer.getWritePointer (ch), inputChannels[ch], numSamples);
        else
            audioBuffer.clear (ch, 0, numSamples);
    }
    
    // Prepare MIDI buffer
    midiBuffer.clear();
    
    // Process through plugin
    juce::MidiBuffer emptyMidi;
    pluginInstance->processBlock (audioBuffer, emptyMidi);
    
    // Copy output to shared memory
    if (audioOutputMemory && audioOutputMemory->getData())
    {
        float* outputData = (float*) audioOutputMemory->getData();
        for (int ch = 0; ch < 2; ++ch)
        {
            if (outputChannels && outputChannels[ch])
                juce::FloatVectorOperations::copy (outputChannels[ch], audioBuffer.getReadPointer (ch), numSamples);
            
            // Also write to shared memory for host to read
            juce::FloatVectorOperations::copy (outputData + (ch * numSamples), audioBuffer.getReadPointer (ch), numSamples);
        }
    }
}

} // namespace myapp::bridge
