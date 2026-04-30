#include "MainComponent.h"

//==============================================================================
// UI Initialization and Layout
//==============================================================================

void MainComponent::initialiseUI()
{
    // Defer audio setup until message loop is running
    auto safeThis = juce::Component::SafePointer<MainComponent> (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis != nullptr)
            safeThis->setAudioChannels (2, 2);
    });

    // Create properties file for caching
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;

    appProperties = std::make_unique<juce::PropertiesFile> (
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("MyApp")
            .getChildFile ("PluginCache.properties"),
        options);

    // Create UI components via UIController
    uiController.createComponents (pluginManager.getFormatManager(),
                                   pluginManager.getKnownPluginList(),
                                   deadMansPedalFile,
                                   appProperties.get());

    addAndMakeVisible (uiController.getPluginListComponent());
    uiController.getPluginListComponent()->getTableListBox().addMouseListener (this, true);

    // Set up button callbacks
    uiController.getScanButton()->onClick  = [this] { scanForPluginsAsync(); };
    uiController.getUnloadButton()->onClick = [this] { unloadPlugin(); };

    uiController.getOpenAudioFileButton()->onClick = [this] { openAudioFile(); };
    uiController.getPlayButton()->onClick  = [this] { playAudioFile(); };
    uiController.getStopButton()->onClick  = [this] { stopAudioFile(); };
    
    // Create virtual keyboard
    virtualKeyboard = std::make_unique<VirtualKeyboard>();
    virtualKeyboard->onNotePlayed = [this] (int midiNoteNumber, bool isNoteOn)
    {
        DEBUG_LOG ("VirtualKeyboard: onNotePlayed triggered - note=" + juce::String (midiNoteNumber) 
                  + " isNoteOn=" + (isNoteOn ? "true" : "false"));
        
        if (auto* plugin = pluginManager.getLoadedPlugin())
        {
            // Send MIDI message to plugin
            juce::MidiBuffer midiBuffer;
            if (isNoteOn)
            {
                midiBuffer.addEvent (juce::MidiMessage::noteOn (1, midiNoteNumber, 0.8f), 0);
                DEBUG_LOG ("VirtualKeyboard: Note ON " + juce::String (midiNoteNumber));
            }
            else
            {
                midiBuffer.addEvent (juce::MidiMessage::noteOff (1, midiNoteNumber), 0);
                DEBUG_LOG ("VirtualKeyboard: Note OFF " + juce::String (midiNoteNumber));
            }
            
            // Process the MIDI message through the plugin
            juce::AudioBuffer<float> tempBuffer (2, 512);
            tempBuffer.clear();
            plugin->processBlock (tempBuffer, midiBuffer);
        }
        else if (bridgePluginLoaded && bridgeManager.isAvailable())
        {
            // Send MIDI to VST2 via bridge - use callAsync to prevent IPC flooding
            const int command = isNoteOn ? 0x90 : 0x80; // Note On/Off
            const int payload = (command << 16) | (midiNoteNumber << 8) | 127;
            
            DEBUG_LOG ("VirtualKeyboard: Queuing MIDI to VST2 via IPC - command=0x" 
                      + juce::String::toHexString (command) + " note=" + juce::String (midiNoteNumber));
            
            // CRITICAL FIX: Use MessageManager::callAsync to space out IPC messages
            // This prevents JUCE ChildProcessCoordinator from dropping rapid-fire messages
            juce::MessageManager::callAsync ([this, command, payload, midiNoteNumber, isNoteOn]
            {
                if (bridgePluginLoaded && bridgeManager.isAvailable())
                {
                    const bool result = bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::processMidi, juce::String (payload) });
                    DEBUG_LOG ("VirtualKeyboard: MIDI command " + juce::String (result ? "SENT" : "FAILED") 
                              + " to worker - note=" + juce::String (midiNoteNumber) + " isNoteOn=" + (isNoteOn ? "true" : "false"));
                }
            });
        }
        else
        {
            DEBUG_LOG ("VirtualKeyboard: No plugin loaded - bridgePluginLoaded=" 
                      + juce::String (bridgePluginLoaded ? "true" : "false") 
                      + " bridgeManager.isAvailable=" 
                      + juce::String (bridgeManager.isAvailable() ? "true" : "false"));
        }
    };
    addAndMakeVisible (virtualKeyboard.get());

    // Create program selector
    programSelector.addItem ("-- Select Instrument --", 1);
    programSelector.setEditableText (false);
    programSelector.setJustificationType (juce::Justification::centredLeft);
    programSelector.onChange = [this]
    {
        const int selectedIndex = programSelector.getSelectedId() - 1;
        if (selectedIndex >= 0 && selectedIndex < numPrograms)
        {
            setCurrentProgram (selectedIndex);
            DEBUG_LOG ("ProgramSelector: Changed to program " + juce::String (selectedIndex));
        }
    };
    addAndMakeVisible (programSelector);

    // Maqam selector setup
    uiController.getMaqamSelector()->addItem ("Bayati",  1);
    uiController.getMaqamSelector()->addItem ("Rast",    2);
    uiController.getMaqamSelector()->addItem ("Hijaz",   3);
    uiController.getMaqamSelector()->addItem ("Sika",    4);
    uiController.getMaqamSelector()->setSelectedId (1, juce::dontSendNotification);
    
    uiController.getMaqamSelector()->onChange = [this]
    {
        using myapp::music::MaqamPreset;
        const auto presets = std::array<MaqamPreset, 4> {
            MaqamPreset::bayati, MaqamPreset::rast,
            MaqamPreset::hijaz,  MaqamPreset::sika
        };
        const int idx = uiController.getMaqamSelector()->getSelectedId() - 1;
        if (idx >= 0 && idx < (int) presets.size())
        {
            maqamManager.setMaqam (presets[(size_t) idx]);

            if (appProperties != nullptr)
            {
                appProperties->setValue ("lastMaqam", idx);
                appProperties->saveIfNeeded();
            }
        }
    };

    // Plugin filter selector
    uiController.getPluginFilterSelector()->addItem ("All", 1);
    uiController.getPluginFilterSelector()->addItem ("Loadable", 2);
    uiController.getPluginFilterSelector()->addItem ("Failed", 3);
    uiController.getPluginFilterSelector()->setSelectedId (1, juce::dontSendNotification);
    
    uiController.getPluginFilterSelector()->onChange = [this]
    {
        updatePluginListUI();
    };

    // Add all UI components
    uiController.addComponentsTo (*this);
    addAndMakeVisible (pluginSubWindowContainer);
    pluginSubWindowContainer.setVisible (false);  // Hide container, VST2 will be positioned over it
    
    // Add virtual keyboard and program selector
    addAndMakeVisible (virtualKeyboard.get());
    addAndMakeVisible (programSelector);

    // When the worker process crashes, the IPC disconnect is detected and fires this callback.
    // We then clear the embedded window reference safely on the message thread.
    bridgeManager.onWorkerDisconnected = [this]
    {
        DEBUG_LOG ("MainComponent: onWorkerDisconnected callback fired");
        pluginSubWindowContainer.clearEmbeddedWindow();
        pluginSubWindowContainer.setVisible (false);
        bridgePluginLoaded = false;
        uiController.setStatusMessage ("VST2 plugin connection lost. Reload to retry.");
    };

    // Listen for plugin list changes
    pluginManager.getKnownPluginList().addChangeListener (this);

    // Load cached plugin list + restore last maqam
    loadPluginCache();
    restoreMaqamPreset();
}

void MainComponent::resized()
{
    // Enforce minimum window size
    const int minWidth = 800;
    const int minHeight = 700;  // Changed from 600 to 700
    
    if (getWidth() < minWidth || getHeight() < minHeight)
    {
        setSize (juce::jmax (getWidth(), minWidth), 
                 juce::jmax (getHeight(), minHeight));
        return;
    }
    
    // Define layout constants
    const int margin = 15;
    const int spacing = 10;
    const int keyboardHeight = 140;
    
    // Start from top and work down - NO OVERLAPS
    int currentY = margin;
    
    // ===== PANEL 1: Top Bar (Program Selector) =====
    const int topBarHeight = 35;
    const int selectorWidth = 250;
    
    programSelector.setBounds (
        getWidth() - selectorWidth - margin,
        currentY,
        selectorWidth,
        topBarHeight
    );
    
    currentY += topBarHeight + spacing;  // Move down, NO OVERLAP
    
    // ===== PANEL 2: Plugin List =====
    const int pluginListHeight = 180;
    const int pluginListWidth = getWidth();  // Full width of window
    
    auto* pluginList = uiController.getPluginListComponent();
    pluginList->setBounds (
        0,  // Left edge
        currentY,
        pluginListWidth,
        pluginListHeight
    );
    
    currentY += pluginListHeight + spacing;  // Move down, NO OVERLAP
    
    // ===== PANEL 3: Control Buttons (3 rows) =====
    const int buttonHeight = 36;
    const int buttonSpacing = 8;
    const int controlPanelHeight = (buttonHeight + buttonSpacing) * 3;
    
    // Row 1: Scan, Load, Unload
    auto row1 = juce::Rectangle<int> (margin, currentY, 
                                       getWidth() - (margin * 2), buttonHeight);
    uiController.getScanButton()->setBounds (row1.removeFromLeft (140).reduced (2, 0));
    uiController.getLoadButton()->setBounds (row1.removeFromLeft (140).reduced (2, 0));
    uiController.getUnloadButton()->setBounds (row1.removeFromLeft (140).reduced (2, 0));
    
    currentY += buttonHeight + buttonSpacing;  // Move down, NO OVERLAP
    
    // Row 2: Open Audio File, Play, Stop
    auto row2 = juce::Rectangle<int> (margin, currentY,
                                       getWidth() - (margin * 2), buttonHeight);
    uiController.getOpenAudioFileButton()->setBounds (row2.removeFromLeft (140).reduced (2, 0));
    uiController.getPlayButton()->setBounds (row2.removeFromLeft (100).reduced (2, 0));
    uiController.getStopButton()->setBounds (row2.removeFromLeft (100).reduced (2, 0));
    
    currentY += buttonHeight + buttonSpacing;  // Move down, NO OVERLAP
    
    // Row 3: Maqam, Filter, Plugin Selector
    auto row3 = juce::Rectangle<int> (margin, currentY,
                                       getWidth() - (margin * 2), buttonHeight);
    uiController.getMaqamSelector()->setBounds (row3.removeFromLeft (180).reduced (2, 0));
    row3.removeFromLeft (10);  // Spacer
    uiController.getPluginFilterSelector()->setBounds (row3.removeFromLeft (150).reduced (2, 0));
    uiController.getPluginSelector()->setBounds (row3.removeFromLeft (300).reduced (2, 0));
    
    currentY += buttonHeight + spacing;  // Move down, NO OVERLAP
    
    // ===== PANEL 4: Plugin Container (VST2 Window Area) =====
    // This fills all remaining space above keyboard
    const int containerTop = currentY;
    const int containerBottom = getHeight() - keyboardHeight - margin;
    const int containerHeight = containerBottom - containerTop;
    
    if (containerHeight > 0)  // Ensure positive height
    {
        pluginSubWindowContainer.setBounds (
            margin,
            containerTop,
            getWidth() - (margin * 2),
            containerHeight
        );
        
        // Reposition VST2 window to match container
        pluginSubWindowContainer.repositionEmbeddedWindow();
    }
    
    // ===== PANEL 5: Virtual Keyboard (Bottom) =====
    const int keyboardTop = getHeight() - keyboardHeight - margin;
    
    virtualKeyboard->setBounds (
        margin,
        keyboardTop,
        getWidth() - (margin * 2),
        keyboardHeight
    );
}
void MainComponent::updatePluginListUI()
{
    const int filterType = uiController.getPluginFilterSelector()->getSelectedId();
    
    uiController.updatePluginList (pluginManager.getKnownPluginList(),
                                   *uiController.getPluginSelector(),
                                   pluginSelectorToKnownIndex,
                                   filterType);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF1E1E1E));
}
