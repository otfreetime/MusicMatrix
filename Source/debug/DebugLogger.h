#pragma once
#include <JuceHeader.h>
#include <fstream>
#include <mutex>

namespace myapp::debug
{

class DebugLogger
{
public:
    static DebugLogger& getInstance()
    {
        static DebugLogger instance;
        return instance;
    }

    void log (const juce::String& message)
    {
        std::lock_guard<std::mutex> lock (mutex);
        
        const auto timestamp = juce::Time::getCurrentTime().toString (true, true, true, true);
        const auto line = "[" + timestamp + "] " + message + "\n";
        
        if (fileStream.is_open())
        {
            fileStream << line.toStdString();
            fileStream.flush();
        }
        
        DBG (message);
    }

    void setFilePath (const juce::File& file)
    {
        std::lock_guard<std::mutex> lock (mutex);
        
        if (fileStream.is_open())
            fileStream.close();
        
        fileStream.open (file.getFullPathName().toStdString(), std::ios::out | std::ios::app);
        
        if (fileStream.is_open())
        {
            const auto header = "\n\n========== MyApp Debug Session Started: " 
                              + juce::Time::getCurrentTime().toString (true, true) 
                              + " ==========\n\n";
            fileStream << header.toStdString();
            fileStream.flush();
        }
    }

private:
    DebugLogger()
    {
        // Write log file to workspace directory for easy access during debugging
        auto logFile = juce::File ("E:\\Maqam Classification\\MyApp\\MyApp_debug_log.txt");
        setFilePath (logFile);
    }

    ~DebugLogger()
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (fileStream.is_open())
        {
            fileStream << "\n========== Debug Session Ended ==========\n";
            fileStream.close();
        }
    }

    std::ofstream fileStream;
    std::mutex mutex;
};

#define DEBUG_LOG(msg) myapp::debug::DebugLogger::getInstance().log(msg)

} // namespace myapp::debug
