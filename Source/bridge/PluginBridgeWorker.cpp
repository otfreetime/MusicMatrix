#include "PluginBridgeWorker.h"
#include "AudioSharedMemory.h"

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
    audioProcessingTimer.reset();
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
    DBG ("PluginBridgeWorker: Plugin instance created, sampleRate=" + juce::String (sampleRate));

    sampleRate = 44100;
    blockSize = 512;
    pluginInstance->setPlayConfigDetails (2, 2, sampleRate, blockSize);
    pluginInstance->prepareToPlay (sampleRate, blockSize);
    isProcessing = true;
    
    DBG ("PluginBridgeWorker: Plugin prepared, setting up shared memory");
    
    // Prepare audio buffer
    audioBuffer.setSize (2, blockSize);
    audioBuffer.clear();
    midiBuffer.clear();
    
    // Setup shared memory for audio I/O
    if (setupSharedMemory())
    {
        sendStatus ("shared_memory_ready");
        sendStatus ("shared_memory_paths:" + inputMemoryFile.getFullPathName() + "|" + outputMemoryFile.getFullPathName());

        // Start a timer on the message thread to drive audio processing
        // Using 100ms interval instead of 10ms to reduce CPU and avoid plugin crashes
        // Some plugins (like EasternONE) crash with continuous rapid processBlock calls
        DBG ("PluginBridgeWorker: Starting audio timer at 100ms interval");
        juce::MessageManager::callAsync ([this]
        {
            audioProcessingTimer = std::make_unique<AudioProcessingTimer> (*this);
            audioProcessingTimer->startTimer (10); // 10ms tick; tickAudio() self-throttles via ring buffer free-space check
        });
    }
    else
        sendStatus ("warning:shared_memory_failed");
    
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

void PluginBridgeWorker::tickAudio()
{
    // Only call processBlock when there is room in the ring buffer.
    // This naturally throttles production to match the host consumption rate
    // and prevents crashing plugins that misbehave under continuous rapid calls.
    if (audioOutputMemory)
    {
        const int freeSpace = myapp::bridge::audioRingBufferFreeSpace (audioOutputMemory->getData());
        if (freeSpace < blockSize)
            return; // Buffer is full enough; skip this tick
    }

    processAudioBlockInternal (audioBuffer, midiBuffer);
}

// Internal version that does the actual processing - called from SEH wrapper
void PluginBridgeWorker::processAudioBlockInternal (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (! pluginInstance || ! audioOutputMemory || ! isProcessing)
        return;

    // Clear input buffer (MIDI instrument — no audio input needed)
    buffer.clear();

    juce::MidiBuffer emptyMidi;
    pluginInstance->processBlock (buffer, emptyMidi);

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
        hwndRetryTimer.reset();
        audioProcessingTimer.reset();
        pluginEditor.reset();
        pluginInstance.reset();
    });
    loadedPluginPath.clear();
}

juce::String PluginBridgeWorker::getLastError() const
{
    return lastError;
}

void PluginBridgeWorker::handleCommand (const IPCCommand& command)
{
    DBG("PluginBridgeWorker: Received command: " + juce::String(static_cast<int>(command.type)) + " payload: " + command.payload);
    switch (command.type)
    {
        case IPCCommandType::loadPlugin:
            DBG("PluginBridgeWorker: Handling loadPlugin: " + command.payload);
            sendStatus ("handling_loadPlugin:" + command.payload);
            if (loadPlugin (command.payload))
                sendStatus ("plugin_loaded");
            else
                sendStatus ("error:plugin_load_failed:" + lastError);
            break;

        case IPCCommandType::unloadPlugin:
            DBG("PluginBridgeWorker: Handling unloadPlugin");
            unloadPlugin();
            sendStatus ("plugin_unloaded");
            break;

        case IPCCommandType::setDetached:
            detached = (command.payload == "true");
            DBG("PluginBridgeWorker: setDetached: " + command.payload);
            sendStatus ("detached_mode_set:" + command.payload);
            break;

        case IPCCommandType::processAudio:
            tickAudio();
            break;

        case IPCCommandType::processMidi:
            // TODO: Implement MIDI processing
            break;

        default:
            DBG("PluginBridgeWorker: Unknown command: " + juce::String(static_cast<int>(command.type)));
            sendStatus ("warning:unknown_command:" + juce::String (static_cast<int> (command.type)));
            break;
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
