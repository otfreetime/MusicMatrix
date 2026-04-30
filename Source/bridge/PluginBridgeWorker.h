#pragma once

#include <JuceHeader.h>
#include "IPCManager.h"

namespace myapp::bridge
{
class PluginBridgeWorker
{
public:
    PluginBridgeWorker();
    ~PluginBridgeWorker();

    /** Called from the worker's main(). commandLine is argv joined; JUCE parses
        the bridge handshake token from it automatically. */
    bool start (const juce::String& commandLine);
    void stop();

    bool loadPlugin (const juce::String& pluginPath);
    void unloadPlugin();

    juce::String getLastError() const;

private:
    //==========================================================================
    // ChildProcessWorker subclass — owns the IPC channel to the coordinator.
    struct WorkerIPC final : public juce::ChildProcessWorker
    {
        explicit WorkerIPC (PluginBridgeWorker& o) : owner (o) {}

        void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
        {
            const juce::String text (juce::String::fromUTF8 (
                static_cast<const char*> (mb.getData()), static_cast<int> (mb.getSize())));
            owner.handleCommand (ipcDeserialize (text));
        }

        void handleConnectionLost() override
        {
            // Coordinator died — exit the worker process cleanly.
            juce::JUCEApplication::getInstance()->systemRequestedQuit();        
        }

        PluginBridgeWorker& owner;
    };

    //==========================================================================
    // Thread that drives audio processing via shared memory
    class AudioWorkerThread : public juce::Thread
    {
    public:
        explicit AudioWorkerThread (PluginBridgeWorker& owner)
            : juce::Thread("BridgeAudioThread"), ownerWorker(owner) {}

        void run() override;

    private:
        PluginBridgeWorker& ownerWorker;
    };

    // Timer that polls for the plugin editor HWND becoming available
    struct HWNDRetryTimer : public juce::Timer
    {
        HWNDRetryTimer (PluginBridgeWorker& o, int maxAttempts)
            : owner (o), attemptsLeft (maxAttempts) {}

        void timerCallback() override
        {
            if (owner.pluginEditor == nullptr)
            {
                stopTimer();
                owner.sendStatus ("error:editor_missing_during_hwnd_poll");
                return;
            }

            void* hwnd = nullptr;
           #if JUCE_WINDOWS
            if (auto* peer = owner.pluginEditor->getPeer())
                hwnd = peer->getNativeHandle();
           #endif

            if (hwnd != nullptr)
            {
                stopTimer();
                owner.sendStatus ("editor_hwnd_ready_polled");
                owner.sendEmbedWindow (hwnd);
                return;
            }

            if (attemptsLeft % 10 == 0)
                owner.sendStatus ("editor_hwnd_poll_remaining:" + juce::String (attemptsLeft));

            if (--attemptsLeft <= 0)
            {
                stopTimer();
                owner.sendStatus ("error:editor_hwnd_unavailable");
            }
        }

        PluginBridgeWorker& owner;
        int attemptsLeft;
    };

    //==========================================================================
    void handleCommand (const IPCCommand& command);
    void sendStatus (const juce::String& text);
    void sendEmbedWindow (void* hwnd);
    void createPluginEditor();
    void publishEditorWindowHandleWithRetry (int attemptsRemaining);

    void processAudioBlockInternal (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void tickAudio();
    bool setupSharedMemory();
    void cleanupSharedMemory();
    void processPluginAudio (float** inputChannels, float** outputChannels, int numSamples);

    //==========================================================================
    juce::String lastError;
    juce::String loadedPluginPath;
    bool running { false };

    WorkerIPC ipc { *this };   // <<< replaces IPCManager

    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
    std::unique_ptr<juce::AudioProcessorEditor> pluginEditor;
    std::unique_ptr<HWNDRetryTimer>         hwndRetryTimer;
    std::unique_ptr<AudioWorkerThread>      audioWorkerThread;
    juce::StringArray nativeEditorCrashPluginPaths;
    bool detached { false };
    std::unique_ptr<juce::DocumentWindow> detachedWindow;

    juce::AudioBuffer<float> audioBuffer;
    juce::MidiBuffer         midiBuffer;
    int sampleRate { 44100 };
    int blockSize  { 512 };

    std::unique_ptr<juce::MemoryMappedFile> audioInputMemory;
    std::unique_ptr<juce::MemoryMappedFile> audioOutputMemory;
    juce::String sharedMemoryNameInput;
    juce::String sharedMemoryNameOutput;
    juce::File inputMemoryFile;
    juce::File outputMemoryFile;

    bool isProcessing { false };
};
} // namespace myapp::bridge
