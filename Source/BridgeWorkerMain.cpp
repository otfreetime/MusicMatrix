#include <JuceHeader.h>
#include "bridge/PluginBridgeWorker.h"

#if JUCE_WINDOWS
 #include <objbase.h>
#endif

class MyAppBridgeWorkerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "MyAppBridgeWorker"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
#if JUCE_WINDOWS
        const auto hr = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
        comInitialised = SUCCEEDED (hr) || hr == RPC_E_CHANGED_MODE;
#endif
        juce::File logFile (juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                .getChildFile ("MyApp_worker_log.txt"));
        juce::FileOutputStream stream (logFile);
        stream.writeText ("BridgeWorkerMain: Starting (ChildProcessWorker)\n", false, false, nullptr);

        worker = std::make_unique<myapp::bridge::PluginBridgeWorker>();

        // Pass the full commandLine — ChildProcessWorker::initialiseFromCommandLine
        // extracts the JUCE handshake token automatically.
        if (! worker->start (commandLine))
        {
            juce::Logger::writeToLog ("Bridge worker failed to start: " + worker->getLastError());
            stream.writeText ("Bridge worker failed to start: " + worker->getLastError() + "\n", false, false, nullptr);
            quit();
            return;
        }

        stream.writeText ("Bridge worker started successfully\n", false, false, nullptr);
        juce::Logger::writeToLog ("Bridge worker started successfully");
    }

    void shutdown() override
    {
        if (worker != nullptr)
        {
            worker->stop();
            worker.reset();
        }

#if JUCE_WINDOWS
        if (comInitialised)
            CoUninitialize();
        comInitialised = false;
#endif
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<myapp::bridge::PluginBridgeWorker> worker;
#if JUCE_WINDOWS
    bool comInitialised { false };
#endif
};

START_JUCE_APPLICATION (MyAppBridgeWorkerApplication)
