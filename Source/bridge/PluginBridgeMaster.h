#pragma once

#include <JuceHeader.h>
#include "IPCManager.h"

namespace myapp::bridge
{
//==============================================================================
// PluginBridgeMaster — host side of the bridge.
//
// Uses juce::ChildProcessCoordinator to launch and communicate with the worker
// process. JUCE manages the pipe creation and handshake internally, which
// eliminates the timing races that plagued the old InterprocessConnection code.
//==============================================================================
class PluginBridgeMaster
{
public:
    using CommandCallback    = std::function<void (const IPCCommand&)>;
    using ConnectionCallback = std::function<void (bool isConnected)>;

    PluginBridgeMaster();
    ~PluginBridgeMaster();

    /** Launch the worker executable. Blocks until the worker connects or times out. */
    bool launchWorker (const juce::File& workerExecutable);
    /** Terminate the worker process and reset state. */
    void shutdownWorker();
    /** True while the coordinator has an active connection to the worker. */
    bool isWorkerRunning() const;

    void setCommandCallback    (CommandCallback    callback);
    void setConnectionCallback (ConnectionCallback callback);

    /** Serialize and send a command to the worker. */
    bool send (const IPCCommand& command);

private:
    //==========================================================================
    // ChildProcessCoordinator subclass — receives messages and connection events.
    struct Coordinator final : public juce::ChildProcessCoordinator
    {
        explicit Coordinator (PluginBridgeMaster& o) : owner (o) {}

        void handleMessageFromWorker (const juce::MemoryBlock& mb) override
        {
            const juce::String text (juce::String::fromUTF8 (
                static_cast<const char*> (mb.getData()), static_cast<int> (mb.getSize())));
            const auto cmd = ipcDeserialize (text);
            if (owner.onCommand)
                owner.onCommand (cmd);
        }

        void handleConnectionLost() override
        {
            owner.workerConnected = false;
            if (owner.onConnection)
                owner.onConnection (false);
        }

        PluginBridgeMaster& owner;
    };

    //==========================================================================
    std::unique_ptr<Coordinator> coordinator;
    CommandCallback    onCommand;
    ConnectionCallback onConnection;
    bool               workerConnected { false };
};

} // namespace myapp::bridge
