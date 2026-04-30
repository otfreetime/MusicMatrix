#include "BridgeManager.h"
#include "../debug/DebugLogger.h"

namespace myapp::host
{

BridgeManager::BridgeManager() = default;

BridgeManager::~BridgeManager()
{
    shutdown();
}

void BridgeManager::initialise (std::function<void(bool)> onBridgeReady)
{
    auto executable = findWorkerExecutable();

    ++launchGeneration;

    juce::Thread::launch ([this, executable, onBridgeReady]
    {
        const bool ok = bridgeMaster.launchWorker (executable);

        juce::MessageManager::callAsync ([this, ok, executable, onBridgeReady]
        {
            bridgeAvailable = ok;
            activeBridgeWorkerExecutable = ok ? executable : juce::File();

            if (onBridgeReady)
                onBridgeReady (ok);
        });
    });
}

void BridgeManager::shutdown()
{
    closeAudioFiles();
    bridgeMaster.shutdownWorker();
    bridgeAvailable = false;
    activeBridgeWorkerExecutable = {};
}

bool BridgeManager::ensureWorkerForPlugin (const juce::String& pluginPath, juce::String& errorMessage)
{
    const auto workerExecutable = findWorkerExecutableForPlugin (pluginPath);

    if (! workerExecutable.existsAsFile())
    {
        const auto arch = detectBinaryArch (pluginPath);
        if (arch == PluginBinaryArch::x86)
            errorMessage = "x86 bridge worker not found. Set MYAPP_BRIDGE_WORKER_X86_PATH or build build-x86/MyAppBridgeWorker.";
        else
            errorMessage = "Bridge worker executable not found.";

        bridgeAvailable = false;
        return false;
    }

    DBG("BridgeManager: Worker executable found: " + workerExecutable.getFullPathName());

    const bool needRelaunch = (! bridgeMaster.isWorkerRunning())
                              || (! activeBridgeWorkerExecutable.existsAsFile())
                              || (activeBridgeWorkerExecutable.getFullPathName() != workerExecutable.getFullPathName());

    if (needRelaunch)
    {
        DBG("BridgeManager: Relaunching worker for plugin: " + pluginPath);
        DBG("BridgeManager: Worker executable: " + workerExecutable.getFullPathName());
        bridgeMaster.shutdownWorker();
        ++launchGeneration;
        bridgeAvailable = bridgeMaster.launchWorker (workerExecutable);

        if (! bridgeAvailable)
        {
            errorMessage = "Failed to launch bridge worker";
            activeBridgeWorkerExecutable = {};
            return false;
        }

        activeBridgeWorkerExecutable = workerExecutable;
        DBG("BridgeManager: Worker launched successfully");
    }

    errorMessage.clear();
    return true;
}

bool BridgeManager::loadPlugin (const juce::String& pluginPath, const juce::String& pluginName)
{
    if (! bridgeAvailable)
    {
        DBG("BridgeManager: Bridge not available for loadPlugin");
        return false;
    }

    setUIMode (false);
    juce::Thread::sleep (50);

    DBG("BridgeManager: Sending loadPlugin command for: " + pluginPath);
    return bridgeMaster.send ({ myapp::bridge::IPCCommandType::loadPlugin, pluginPath });
}

void BridgeManager::unloadPlugin()
{
    if (bridgeAvailable)
        bridgeMaster.send ({ myapp::bridge::IPCCommandType::unloadPlugin, {} });
}

void BridgeManager::setUIMode (bool detached)
{
    if (bridgeAvailable)
        bridgeMaster.send ({ myapp::bridge::IPCCommandType::setDetached, detached ? "true" : "false" });
}

void BridgeManager::setCommandCallback (std::function<void(const myapp::bridge::IPCCommand&)> callback)
{
    bridgeMaster.setCommandCallback (std::move (callback));

    // Wire the connection-lost event. The coordinator calls this on its own
    // thread when the worker pipe drops — we post to the message thread so
    // all bridgeAvailable writes happen there, eliminating data races.
    bridgeMaster.setConnectionCallback ([this] (bool connected)
    {
        if (! connected)
        {
            DEBUG_LOG ("BridgeManager: Worker disconnected (ChildProcessCoordinator)");
            const auto gen = launchGeneration;
            juce::MessageManager::callAsync ([this, gen]
            {
                // Ignore if a newer worker was already launched (stale event).
                if (gen != launchGeneration)
                {
                    DEBUG_LOG ("BridgeManager: Stale disconnect ignored (worker was relaunched)");
                    return;
                }
                bridgeAvailable = false;
                DEBUG_LOG ("BridgeManager: Firing onWorkerDisconnected callback");
                if (onWorkerDisconnected)
                    onWorkerDisconnected();
            });
        }
    });
}

bool BridgeManager::sendCommand (const myapp::bridge::IPCCommand& command)
{
    return bridgeMaster.send (command);
}

bool BridgeManager::openAudioFiles (const juce::String& inputPath, const juce::String& outputPath)
{
    closeAudioFiles();

    bridgeAudioInputFile = juce::File (inputPath);
    bridgeAudioOutputFile = juce::File (outputPath);

    if (! bridgeAudioInputFile.existsAsFile() || ! bridgeAudioOutputFile.existsAsFile())
        return false;

    bridgeAudioInputMemory = std::make_unique<juce::MemoryMappedFile> (
        bridgeAudioInputFile, juce::MemoryMappedFile::readWrite);
    bridgeAudioOutputMemory = std::make_unique<juce::MemoryMappedFile> (
        bridgeAudioOutputFile, juce::MemoryMappedFile::readWrite);

    return bridgeAudioInputMemory->getData() != nullptr &&
           bridgeAudioOutputMemory->getData() != nullptr;
}

void BridgeManager::closeAudioFiles()
{
    bridgeAudioInputMemory.reset();
    bridgeAudioOutputMemory.reset();
    bridgeAudioInputFile = {};
    bridgeAudioOutputFile = {};
}

juce::File BridgeManager::findWorkerExecutable() const
{
    return findWorkerExecutableForPlugin ({});
}

juce::File BridgeManager::findWorkerExecutableForPlugin (const juce::String& pluginPath) const
{
    const auto arch = detectBinaryArch (pluginPath);
    const bool wantsX86Worker = (arch == PluginBinaryArch::x86);

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

    auto configDir = hostExe.getParentDirectory();
    auto artefactDir = configDir.getParentDirectory();
    auto buildDir = artefactDir.getParentDirectory();
    auto projectDir = buildDir.getParentDirectory();

    auto findWorkerForArchitecture = [&] (bool useX86) -> juce::File
    {
        if (! useX86 && buildDir.isDirectory())
        {
            auto workerFromBuildLayout = buildDir
                .getChildFile ("MyAppBridgeWorker_artefacts")
                .getChildFile (configDir.getFileName())
                .getChildFile ("MyAppBridgeWorker.exe");

            if (workerFromBuildLayout.existsAsFile())
                return workerFromBuildLayout;
        }

        if (useX86 && projectDir.isDirectory())
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

        return {};
    };

    if (arch == PluginBinaryArch::unknown)
    {
        auto x86Worker = findWorkerForArchitecture (true);
        if (x86Worker.existsAsFile())
            return x86Worker;

        return findWorkerForArchitecture (false);
    }

    if (wantsX86Worker)
        return findWorkerForArchitecture (true);

    return findWorkerForArchitecture (false);

    return {};
}

BridgeManager::PluginBinaryArch BridgeManager::detectBinaryArch (const juce::String& filePath) const
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

} // namespace myapp::host
