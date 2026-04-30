#include "PluginBridgeMaster.h"

namespace myapp::bridge
{

// The unique token that both coordinator and worker embed in the command line.
// Must match the string used in PluginBridgeWorker::start().
static constexpr const char* kWorkerUID = "myapp-bridge-worker";

//==============================================================================
PluginBridgeMaster::PluginBridgeMaster()
{
    coordinator = std::make_unique<Coordinator> (*this);
}

PluginBridgeMaster::~PluginBridgeMaster() { shutdownWorker(); }

bool PluginBridgeMaster::launchWorker (const juce::File& workerExecutable)
{
    if (! workerExecutable.existsAsFile())
        return false;

    // Destroy any previous coordinator state (kills old process if still alive).
    shutdownWorker();

    DBG ("PluginBridgeMaster: Launching worker: " + workerExecutable.getFullPathName());

    coordinator = std::make_unique<Coordinator> (*this);

    // launchWorkerProcess blocks until the worker calls initialiseFromCommandLine
    // and the handshake succeeds, or until the timeout expires (10 s).
    const bool ok = coordinator->launchWorkerProcess (workerExecutable, kWorkerUID, 10000);

    workerConnected = ok;
    if (ok)
        DBG ("PluginBridgeMaster: Worker connected.");
    else
        DBG ("PluginBridgeMaster: Worker failed to connect within timeout.");

    return ok;
}

void PluginBridgeMaster::shutdownWorker()
{
    coordinator.reset();
    workerConnected = false;
}

bool PluginBridgeMaster::isWorkerRunning() const
{
    return workerConnected;
}

void PluginBridgeMaster::setCommandCallback (CommandCallback callback)
{
    onCommand = std::move (callback);
}

void PluginBridgeMaster::setConnectionCallback (ConnectionCallback callback)
{
    onConnection = std::move (callback);
}

bool PluginBridgeMaster::send (const IPCCommand& command)
{
    DBG ("PluginBridgeMaster::send called - type=" + juce::String (static_cast<int> (command.type)));
    
    if (! workerConnected || ! coordinator)
    {
        DBG ("PluginBridgeMaster::send FAILED - workerConnected=" + juce::String (workerConnected ? "true" : "false") 
            + " coordinator=" + juce::String (coordinator ? "valid" : "null"));
        return false;
    }

    const auto text = ipcSerialize (command);
    DBG ("PluginBridgeMaster::send serialized command: " + text);
    
    const auto memoryBlock = juce::MemoryBlock (text.toRawUTF8(), static_cast<size_t> (text.getNumBytesAsUTF8()));
    DBG ("PluginBridgeMaster::send memory block size: " + juce::String (memoryBlock.getSize()));
    
    const bool result = coordinator->sendMessageToWorker (memoryBlock);
    
    DBG ("PluginBridgeMaster::send sendMessageToWorker result=" + juce::String (result ? "true" : "false"));
    
    if (! result)
    {
        DBG ("PluginBridgeMaster::send FAILED - sendMessageToWorker returned false");
    }
    
    return result;
}

} // namespace myapp::bridge
