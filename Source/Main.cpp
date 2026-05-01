/*
  ==============================================================================

    This file contains the basic startup code for a JUCE application.

  ==============================================================================
*/

#include <JuceHeader.h>
#include "MainComponent.h"

//==============================================================================
class MyAppApplication  : public juce::JUCEApplication
{
public:
    //==============================================================================
    MyAppApplication() {}

    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    //==============================================================================
    void initialise (const juce::String& commandLine) override
    {
        juce::ignoreUnused (commandLine);

        mainWindow.reset (new MainWindow (getApplicationName()));
    }

    void shutdown() override
    {
        DEBUG_LOG ("MyAppApplication: shutdown() called");
        mainWindow = nullptr; // (deletes our window)
        DEBUG_LOG ("MyAppApplication: shutdown() finished");
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        DEBUG_LOG ("MyAppApplication: systemRequestedQuit() called");
        // This is called when the app is being asked to quit: you can ignore this
        // request and let the app carry on running, or call quit() to allow the app to close.
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        // When another instance of the app is launched while this one is running,
        // this method is invoked, and the commandLine parameter tells you what
        // the other instance's command-line arguments were.
        juce::ignoreUnused (commandLine);
    }

    //==============================================================================
    /*
        This class implements the desktop window that contains an instance of
        our MainComponent class.
    */
    class MainWindow    : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                          .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::closeButton | DocumentWindow::maximiseButton)
        {
            DEBUG_LOG ("MainWindow: Constructor called, creating MainComponent");
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            setResizable (true, true);
            if (auto* primaryDisplay = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                setBounds (primaryDisplay->userArea);
            else
                setSize (1400, 900);
           #endif

            setVisible (true);
            toFront (true);
            DEBUG_LOG ("MainWindow: Constructor finished, window visible");
        }

        ~MainWindow() override
        {
            DEBUG_LOG ("MainWindow: DESTRUCTOR CALLED");
        }

        void closeButtonPressed() override
        {
            DEBUG_LOG ("MainWindow: closeButtonPressed() called - user tried to close window");
            // This is called when the user tries to close this window. Here, we'll just
            // ask the app to quit when this happens, but you can change this to do
            // whatever you need.
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

        /* Note: Be careful if you override any DocumentWindow methods - the base
           class uses a lot of them, so by overriding you might break its functionality.
           It's best to do all your work in your content component instead, but if
           you really have to override any DocumentWindow methods, make sure your
           subclass also calls the superclass's method.
        */

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION (MyAppApplication)
