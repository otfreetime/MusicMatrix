#pragma once

#include <JuceHeader.h>
#include "../bridge/PluginBridgeMaster.h"

namespace myapp::host
{

/**
    Manages VST2 bridge worker lifecycle and IPC communication.
    Extracted from MainComponent for better maintainability.
*/
class BridgeManager
{
public:
    BridgeManager();
    ~BridgeManager();

    //==========================================================================
    // Bridge Lifecycle
    //==========================================================================
    void initialise (std::function<void(bool)> onBridgeReady);
    void shutdown();
    bool isAvailable() const { return bridgeAvailable; }
    bool isWorkerRunning() const { return bridgeMaster.isWorkerRunning(); }

    //==========================================================================
    // Plugin Loading via Bridge
    //==========================================================================
    bool ensureWorkerForPlugin (const juce::String& pluginPath, juce::String& errorMessage);
    bool loadPlugin (const juce::String& pluginPath, const juce::String& pluginName);
    void unloadPlugin();
    void setUIMode (bool detached);

    //==========================================================================
    // IPC Communication
    //==========================================================================
    void setCommandCallback (std::function<void(const myapp::bridge::IPCCommand&)> callback);
    bool sendCommand (const myapp::bridge::IPCCommand& command);

    /** Called on the message thread when the worker IPC connection is lost (worker crashed/exited). */
    std::function<void()> onWorkerDisconnected;

    //==========================================================================
    // Audio Routing
    //==========================================================================
    bool openAudioFiles (const juce::String& inputPath, const juce::String& outputPath);
    void closeAudioFiles();
    bool isAudioOpen() const { return bridgeAudioInputMemory != nullptr; }

    const std::unique_ptr<juce::MemoryMappedFile>& getAudioInputMemory() const { return bridgeAudioInputMemory; }
    const std::unique_ptr<juce::MemoryMappedFile>& getAudioOutputMemory() const { return bridgeAudioOutputMemory; }

    juce::File getActiveWorkerExecutable() const { return activeBridgeWorkerExecutable; }

private:
    juce::File findWorkerExecutable() const;
    juce::File findWorkerExecutableForPlugin (const juce::String& pluginPath) const;

    enum class PluginBinaryArch { unknown, x86, x64 };
    PluginBinaryArch detectBinaryArch (const juce::String& filePath) const;

    //==========================================================================
    myapp::bridge::PluginBridgeMaster bridgeMaster;
    juce::File activeBridgeWorkerExecutable;
    bool bridgeAvailable = false;

    // Each time the worker is (re)launched, increment this counter.
    // The disconnect callAsync captures the value at time of disconnect;
    // if the value has changed by the time it fires, a newer worker is
    // already running, so we must not override bridgeAvailable.
    uint64_t launchGeneration = 0;

    // Bridge audio routing
    juce::File bridgeAudioInputFile;
    juce::File bridgeAudioOutputFile;
    std::unique_ptr<juce::MemoryMappedFile> bridgeAudioInputMemory;
    std::unique_ptr<juce::MemoryMappedFile> bridgeAudioOutputMemory;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeManager)
};

} // namespace myapp::host
