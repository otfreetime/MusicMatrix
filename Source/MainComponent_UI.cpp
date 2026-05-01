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
    
    // Initialize MIDI keyboard listener
    keyboardListener = std::make_unique<KeyboardStateListener> (*this);
    keyboardState.addListener (keyboardListener.get());
    
    // Create custom MIDI keyboard component with octave markers
    midiKeyboard = std::make_unique<CustomMidiKeyboardComponent> (
        keyboardState,
        juce::KeyboardComponentBase::Orientation::horizontalKeyboard
    );
    
    // Configure for C0-C10 range (MIDI notes 12-132, but max is 127/G9)
    // C0 = MIDI note 12, C10 = MIDI note 132 (but we cap at 127 = G9)
    midiKeyboard->setAvailableRange (12, 127);  // C0 to G9 (full usable MIDI range)
    midiKeyboard->setScrollButtonsVisible (true);  // Enable horizontal scrolling
    midiKeyboard->setLowestVisibleKey (12);  // Start display at C0 (leftmost)
    midiKeyboard->addChangeListener (this);
    
    // Ensure the keyboard shows all keys including the last octave
    midiKeyboard->setKeyWidth (25.0f);  // Slightly wider keys for better visibility of last octave
    
    // Enable computer keyboard mapping (QWERTY row to piano keys)
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('a'), 0);   // C
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('w'), 1);   // C#
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('s'), 2);   // D
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('e'), 3);   // D#
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('d'), 4);   // E
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('f'), 5);   // F
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('t'), 6);   // F#
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('g'), 7);   // G
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('y'), 8);   // G#
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('h'), 9);   // A
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('u'), 10);  // A#
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('j'), 11);  // B
    midiKeyboard->setKeyPressForNote (juce::KeyPress ('k'), 12);  // C (next octave)
    midiKeyboard->setKeyPressBaseOctave (keyboardBaseOctave);  // Maps to C3 by default
    
    // Styling to match reference image (professional piano look)
    midiKeyboard->setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colours::white);
    midiKeyboard->setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colours::black);
    midiKeyboard->setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colours::lightgrey);
    midiKeyboard->setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colours::lightgrey.withAlpha (0.3f));
    midiKeyboard->setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colours::darkgrey);
    midiKeyboard->setColour (juce::MidiKeyboardComponent::textLabelColourId, juce::Colours::white);
    
    // MIDI configuration
    midiKeyboard->setMidiChannel (1);
    midiKeyboard->setVelocity (0.8f, false);  // Fixed velocity for consistent playback
    
    addAndMakeVisible (midiKeyboard.get());

    // Apply initial maqam highlight colours (Bayati is default)
    refreshKeyboardProgramNoteLabels();
    refreshKeyboardHighlights();

    // Create program selector
    programSelector.addItem ("-- Select Instrument --", 1);
    programSelector.setEditableText (false);
    programSelector.setJustificationType (juce::Justification::centredLeft);
    programSelector.onChange = [this]
    {
        const int selectedDisplayIndex = programSelector.getSelectedId() - 1;
        const int actualProgramIndex = getActualProgramIndexForDisplayIndex (selectedDisplayIndex);
        if (actualProgramIndex >= 0)
        {
            setCurrentProgram (actualProgramIndex);
            DEBUG_LOG ("ProgramSelector: Changed to display " + juce::String (selectedDisplayIndex)
                      + " actual " + juce::String (actualProgramIndex));
        }
    };
    addAndMakeVisible (programSelector);

    // Maqam selector setup (items already added in UIController constructor)
    uiController.getMaqamSelector()->onChange = [this]
    {
        using myapp::music::MaqamPreset;
        const auto presets = std::array<MaqamPreset, 8> {
            MaqamPreset::bayati,   MaqamPreset::rast,
            MaqamPreset::hijaz,    MaqamPreset::sika,
            MaqamPreset::ajam,     MaqamPreset::nahawand,
            MaqamPreset::saba,     MaqamPreset::kurd
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

            refreshKeyboardProgramNoteLabels();
            refreshKeyboardHighlights();
        }
    };;

    // Demo melody button
    uiController.getDemoMelodyButton()->onClick = [this]
    {
        if (melodyPlayer.isPlaying())
            stopDemoMelody();
        else
            playDemoMelody();
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

    pluginSubWindowContainer.setProgramSelectionCallback ([this] (int selectedIndex)
    {
        const int actualProgramIndex = getActualProgramIndexForDisplayIndex (selectedIndex);
        if (actualProgramIndex >= 0)
        {
            setCurrentProgram (actualProgramIndex);
            programSelector.setSelectedId (selectedIndex + 1, juce::dontSendNotification);
        }
    });
    pluginSubWindowContainer.setResetCallback ([this]
    {
        if (! (bridgePluginLoaded && bridgeManager.isAvailable()))
            return;

        bridgeManager.sendCommand ({ myapp::bridge::IPCCommandType::resetPluginState, {} });

        if (numPrograms > 0)
            setCurrentProgram (0);

        uiController.setStatusMessage ("VST2 Rest: default values restored");
    });
    pluginSubWindowContainer.clearProgramList ("-- No Plugin Loaded --");

    addAndMakeVisible (pluginSubWindowContainer);
    pluginSubWindowContainer.setVisible (false);  // Hide container, VST2 will be positioned over it
    
    // Add MIDI keyboard and program selector
    addAndMakeVisible (midiKeyboard.get());
    addAndMakeVisible (programSelector);

    // When the worker process crashes, the IPC disconnect is detected and fires this callback.
    // We then clear the embedded window reference safely on the message thread.
    bridgeManager.onWorkerDisconnected = [this]
    {
        DEBUG_LOG ("MainComponent: onWorkerDisconnected callback fired");
        pluginSubWindowContainer.clearEmbeddedWindow();
        pluginSubWindowContainer.clearProgramList ("-- Worker Disconnected --");
        pluginSubWindowContainer.setVisible (false);
        bridgePluginLoaded = false;
        uiController.setStatusMessage ("VST2 plugin connection lost. Reload to retry.");
    };

    // Listen for plugin list changes
    pluginManager.getKnownPluginList().addChangeListener (this);

    // Load cached plugin list + restore last maqam
    loadPluginCache();
    restoreMaqamPreset();
    refreshKeyboardProgramNoteLabels();
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
    const int keyboardHeight = 160;  // Increased for better playability
    const int keyboardBottomPadding = 40;
    
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
    
    // Row 1: Scan, Load, Unload
    auto row1 = juce::Rectangle<int> (margin, currentY, 
                                       getWidth() - (margin * 2), buttonHeight);
    uiController.getScanButton()->setBounds (row1.removeFromLeft (140).reduced (2, 0));
    uiController.getLoadButton()->setBounds (row1.removeFromLeft (140).reduced (2, 0));
    uiController.getUnloadButton()->setBounds (row1.removeFromLeft (140).reduced (2, 0));
    
    currentY += buttonHeight + buttonSpacing;  // Move down, NO OVERLAP
    
    // Row 2: Open Audio File, Play, Stop, Demo Melody
    auto row2 = juce::Rectangle<int> (margin, currentY,
                                       getWidth() - (margin * 2), buttonHeight);
    uiController.getOpenAudioFileButton()->setBounds (row2.removeFromLeft (140).reduced (2, 0));
    uiController.getPlayButton()->setBounds (row2.removeFromLeft (100).reduced (2, 0));
    uiController.getStopButton()->setBounds (row2.removeFromLeft (100).reduced (2, 0));
    row2.removeFromLeft (16); // spacer
    uiController.getDemoMelodyButton()->setBounds (row2.removeFromLeft (160).reduced (2, 0));
    
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
    // Allocate 60% of remaining space to plugin container
    const int containerTop = currentY;
    const int availableHeight = getHeight() - keyboardHeight - margin - keyboardBottomPadding - currentY;
    const int containerHeight = static_cast<int> (availableHeight * 0.65f);  // 65% for plugin
    
    if (containerHeight > 100)  // Ensure reasonable minimum height
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
    // Keyboard gets full width with dynamic key width based on window size
    const int keyboardTop = getHeight() - keyboardHeight - margin - keyboardBottomPadding;
    const int keyboardWidth = getWidth() - (margin * 2);
    
    midiKeyboard->setBounds (
        margin,
        keyboardTop,
        keyboardWidth,
        keyboardHeight
    );
    
    // Calculate dynamic key width to show as many octaves as possible
    // Each octave needs ~7 white keys, target is to show 4-6 octaves at once
    const float targetVisibleOctaves = 5.0f;
    const float whiteKeysPerOctave = 7.0f;
    const float desiredKeyWidth = static_cast<float> (keyboardWidth) / (targetVisibleOctaves * whiteKeysPerOctave);
    
    // Clamp key width between 15px (narrow) and 35px (wide) for playability
    const float clampedKeyWidth = juce::jlimit (15.0f, 35.0f, desiredKeyWidth);
    midiKeyboard->setKeyWidth (clampedKeyWidth);
    clampKeyboardViewportToRange();
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
