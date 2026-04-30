#pragma once

#include <JuceHeader.h>

namespace myapp::bridge
{
//==============================================================================
// Command types exchanged between host coordinator and bridge worker.
// Transport is now juce::ChildProcessCoordinator / ChildProcessWorker.
//==============================================================================
enum class IPCCommandType
{
    unknown = 0,
    loadPlugin,
    unloadPlugin,
    setupAudio,
    setProgram,
    setParameter,
    embedWindow,
    heartbeat,
    status,
    setDetached,
    processAudio,
    processMidi
};

struct IPCCommand
{
    IPCCommandType type { IPCCommandType::unknown };
    juce::String payload;
};

//==============================================================================
// Serialization helpers shared by coordinator and worker.
//==============================================================================
inline juce::String ipcSerialize (const IPCCommand& command)
{
    return juce::String (static_cast<int> (command.type)) + "|" + command.payload;
}

inline IPCCommand ipcDeserialize (const juce::String& text)
{
    IPCCommand command;
    const auto sep = text.indexOfChar ('|');
    if (sep < 0)
        return command;
    command.type    = static_cast<IPCCommandType> (text.substring (0, sep).getIntValue());
    command.payload = text.substring (sep + 1);
    return command;
}

} // namespace myapp::bridge
