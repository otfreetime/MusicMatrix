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
    if (! workerConnected || ! coordinator)
        return false;

    const auto text = ipcSerialize (command);
    return coordinator->sendMessageToWorker (
        juce::MemoryBlock (text.toRawUTF8(), static_cast<size_t> (text.getNumBytesAsUTF8())));
}

} // namespace myapp::bridge
