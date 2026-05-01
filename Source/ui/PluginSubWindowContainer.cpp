#include "PluginSubWindowContainer.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

// Include debug logger for file-based logging
#include "../debug/DebugLogger.h"

namespace myapp::ui
{
PluginSubWindowContainer::PluginSubWindowContainer()
{
    presetLabel.setText ("Preset", juce::dontSendNotification);
    presetLabel.setJustificationType (juce::Justification::centredLeft);
    presetLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    addAndMakeVisible (presetLabel);

    presetSelector.setEditableText (false);
    presetSelector.setJustificationType (juce::Justification::centredLeft);
    presetSelector.addItem ("-- No Plugin Loaded --", 1);
    presetSelector.setEnabled (false);
    presetSelector.onChange = [this]
    {
        if (suppressPresetChangeCallback)
            return;

        if (onProgramSelectionChanged != nullptr)
            onProgramSelectionChanged (presetSelector.getSelectedId() - 1);
    };
    addAndMakeVisible (presetSelector);

    resetButton.setTooltip ("Rest VST2 plugin to default values");
    resetButton.setEnabled (false);
    resetButton.onClick = [this]
    {
        if (onResetRequested != nullptr)
            onResetRequested();
    };
    addAndMakeVisible (resetButton);
}

PluginSubWindowContainer::~PluginSubWindowContainer()
{
    clearEmbeddedWindow();
}

void PluginSubWindowContainer::setEmbeddedWindowHandle (void* nativeWindowHandle)
{
    DBG("PluginSubWindowContainer: Setting embedded window handle: " + juce::String::toHexString((juce::int64)nativeWindowHandle));
    embeddedWindow = nativeWindowHandle;
    lastAttachStatus.clear();

    attachEmbeddedWindowIfPossible();
    DBG("PluginSubWindowContainer: Attach status: " + lastAttachStatus);
}

juce::String PluginSubWindowContainer::getLastAttachStatus() const
{
    return lastAttachStatus;
}

void PluginSubWindowContainer::attachEmbeddedWindowIfPossible()
{
#if JUCE_WINDOWS
    DEBUG_LOG ("PluginSubWindowContainer::attachEmbeddedWindowIfPossible() called");
    DEBUG_LOG ("  embeddedWindow = " + juce::String::toHexString ((juce::int64) embeddedWindow));
    DEBUG_LOG ("  Container bounds = " + getBounds().toString());
    DEBUG_LOG ("  Container screen bounds = " + getScreenBounds().toString());
    DEBUG_LOG ("  Has peer = " + juce::String (getPeer() != nullptr ? "yes" : "no"));
    DEBUG_LOG ("  Is on desktop = " + juce::String (isOnDesktop() ? "yes" : "no"));
    
    // Calculate expected screen position manually
    if (auto* peer = getPeer())
    {
        auto peerBounds = peer->getBounds();
        DEBUG_LOG ("  Peer window bounds = " + peerBounds.toString());
        DEBUG_LOG ("  Expected screen position = " + juce::String (peerBounds.getX() + getX()) 
                  + "," + juce::String (peerBounds.getY() + getY()));
    }
    
    if (embeddedWindow == nullptr)
    {
        DEBUG_LOG ("  ERROR: embeddedWindow is null");
        lastAttachStatus = "embed_error:null_hwnd";
        return;
    }

    if (auto* peer = getPeer())
    {
        const auto hostHwnd  = reinterpret_cast<HWND> (peer->getNativeHandle());
        const auto pluginHwnd = reinterpret_cast<HWND> (embeddedWindow);

        DEBUG_LOG ("  hostHwnd = " + juce::String::toHexString ((juce::int64) hostHwnd));
        DEBUG_LOG ("  pluginHwnd = " + juce::String::toHexString ((juce::int64) pluginHwnd));

        if (hostHwnd == nullptr || pluginHwnd == nullptr)
        {
            DEBUG_LOG ("  ERROR: Invalid HWND");
            lastAttachStatus = "embed_error:invalid_hwnd";
            return;
        }

        if (! IsWindow (pluginHwnd))
        {
            DEBUG_LOG ("  ERROR: pluginHwnd is not a valid window");
            lastAttachStatus = "embed_error:plugin_hwnd_not_window";
            return;
        }

        // For cross-process windows, avoid SetParent which can cause crashes.
        // Instead, keep the plugin as a top-level window but position it
        // exactly over our container area.
        SetLastError (0);
        LONG_PTR style = GetWindowLongPtr (pluginHwnd, GWL_STYLE);
        const auto styleReadError = GetLastError();
        if (style == 0 && styleReadError != 0)
        {
            DEBUG_LOG ("  ERROR: GetWindowLongPtr failed, error = " + juce::String ((int) styleReadError));
            lastAttachStatus = "embed_error:get_style:" + juce::String ((int) styleReadError);
            return;
        }

        DEBUG_LOG ("  Original window style = " + juce::String::toHexString ((juce::int64) style));

        // CRITICAL FIX: Use SetParent to make VST2 window a true child of our container
        // After SetParent(pluginHwnd, hostHwnd), coordinates are relative to hostHwnd's client area
        DEBUG_LOG ("  Calling SetParent to embed VST2 window as child...");
        DEBUG_LOG ("  hostHwnd (container) = " + juce::String::toHexString ((juce::int64) hostHwnd));
        
        SetLastError (0);
        HWND oldParent = SetParent (pluginHwnd, hostHwnd);
        const auto parentError = GetLastError();
        
        if (oldParent != nullptr)
        {
            DEBUG_LOG ("  SetParent succeeded, old parent = " + juce::String::toHexString ((juce::int64) oldParent));
        }
        else
        {
            DEBUG_LOG ("  SetParent failed, error = " + juce::String (parentError));
        }
        
        // Remove popup style and make it a proper child window
        style &= ~WS_POPUP;
        style |= WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE;

        SetLastError (0);
        const auto styleSetResult = SetWindowLongPtr (pluginHwnd, GWL_STYLE, style);
        const auto styleSetError = GetLastError();
        if (styleSetResult == 0 && styleSetError != 0)
        {
            DEBUG_LOG ("  ERROR: SetWindowLongPtr failed, error = " + juce::String ((int) styleSetError));
        }
        else
        {
            DEBUG_LOG ("  New window style = " + juce::String::toHexString ((juce::int64) style));
        }

        const auto editorArea = getEditorArea();
        const int childX = getX() + editorArea.getX();
        const int childY = getY() + editorArea.getY();
        
        DEBUG_LOG ("  Setting VST2 window at client position "
              + juce::String (childX) + "," + juce::String (childY)
              + " size: " + juce::String (editorArea.getWidth()) + "x" + juce::String (editorArea.getHeight()));

        const BOOL setResult = SetWindowPos (pluginHwnd,
                            HWND_TOP,
                    childX,
                    childY,
                    editorArea.getWidth(),
                    editorArea.getHeight(),
                            SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        const auto posError = GetLastError();
        
        DEBUG_LOG ("  SetWindowPos result = " + juce::String (setResult));
        DEBUG_LOG ("  GetLastError = " + juce::String (posError));
        
        if (! setResult)
        {
            DEBUG_LOG ("  ERROR: SetWindowPos failed");
        }
        
        // Verify the window position in screen coordinates
        RECT actualRect;
        if (GetWindowRect (pluginHwnd, &actualRect))
        {
            DEBUG_LOG ("  VST2 window screen position: " 
                      + juce::String (actualRect.left) + "," + juce::String (actualRect.top)
                      + " size: " + juce::String (actualRect.right - actualRect.left) 
                      + "x" + juce::String (actualRect.bottom - actualRect.top));
            
            // Verify it matches container's screen bounds
            auto expectedScreenBounds = getScreenBounds();
            if (actualRect.left == expectedScreenBounds.getX() && 
                actualRect.top == expectedScreenBounds.getY())
            {
                DEBUG_LOG ("  ✓ VST2 window position MATCHES container screen bounds");
            }
            else
            {
                DEBUG_LOG ("  ✗ VST2 window position MISMATCH! Expected: " 
                          + expectedScreenBounds.toString());
            }
        }
        
        ShowWindow (pluginHwnd, SW_SHOW);
        DEBUG_LOG ("  VST2 window shown");
        
        lastAttachStatus = "embed_ok";
        DEBUG_LOG ("  SUCCESS: VST2 window embedded as child");
    }
    else
    {
        DEBUG_LOG ("  ERROR: No peer (window not yet created)");
        lastAttachStatus = "embed_error:no_peer";
    }
#else
    DEBUG_LOG ("PluginSubWindowContainer::attachEmbeddedWindowIfPossible() - not Windows");
#endif
}

void PluginSubWindowContainer::repositionEmbeddedWindow()
{
    // Public wrapper that repositions the embedded VST2 HWND to match current bounds
    // This should be called after setBounds() changes the container's position/size
    attachEmbeddedWindowIfPossible();
}

void PluginSubWindowContainer::clearEmbeddedWindow()
{
#if JUCE_WINDOWS
    if (embeddedWindow != nullptr)
    {
        const auto pluginHwnd = reinterpret_cast<HWND> (embeddedWindow);

        if (IsWindow (pluginHwnd))
        {
            // Restore original style and hide
            const LONG_PTR style = GetWindowLongPtr (pluginHwnd, GWL_STYLE);
            SetWindowLongPtr (pluginHwnd, GWL_STYLE, style | WS_POPUP);
            ShowWindow (pluginHwnd, SW_HIDE);
        }
    }
#endif
    embeddedWindow = nullptr;
    lastAttachStatus.clear();
}

void PluginSubWindowContainer::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF000000));

    constexpr int titleBarHeight = 32;
    auto titleBar = getLocalBounds().removeFromTop (titleBarHeight);
    g.setColour (juce::Colour (0xFF2A2F37));
    g.fillRect (titleBar);

    g.setColour (juce::Colour (0xFF3B4250));
    g.drawLine ((float) titleBar.getX(), (float) titleBar.getBottom() - 0.5f,
                (float) titleBar.getRight(), (float) titleBar.getBottom() - 0.5f, 1.0f);
}

void PluginSubWindowContainer::resized()
{
    constexpr int titleBarHeight = 32;
    auto bounds = getLocalBounds();
    auto titleBar = bounds.removeFromTop (titleBarHeight);

    auto controls = titleBar.removeFromLeft (300).reduced (8, 4);
    presetLabel.setBounds (controls.removeFromLeft (54));
    controls.removeFromLeft (4);
    presetSelector.setBounds (controls.removeFromLeft (120));
    controls.removeFromLeft (8);
    resetButton.setBounds (controls.removeFromLeft (74));

    attachEmbeddedWindowIfPossible();
}

juce::Rectangle<int> PluginSubWindowContainer::getEditorArea() const
{
    constexpr int titleBarHeight = 32;
    return getLocalBounds().withTrimmedTop (titleBarHeight);
}

void PluginSubWindowContainer::setProgramList (const juce::StringArray& programs, int selectedIndex, bool enabled)
{
    suppressPresetChangeCallback = true;

    presetSelector.clear();
    for (int i = 0; i < programs.size(); ++i)
        presetSelector.addItem (programs[i], i + 1);

    if (programs.isEmpty())
    {
        presetSelector.addItem ("-- No Programs --", 1);
        presetSelector.setSelectedId (1, juce::dontSendNotification);
        presetSelector.setEnabled (false);
    }
    else
    {
        const int clampedIndex = juce::jlimit (0, programs.size() - 1, selectedIndex);
        presetSelector.setSelectedId (clampedIndex + 1, juce::dontSendNotification);
        presetSelector.setEnabled (enabled);
    }

    resetButton.setEnabled (enabled);

    suppressPresetChangeCallback = false;
}

void PluginSubWindowContainer::clearProgramList (const juce::String& placeholderText)
{
    suppressPresetChangeCallback = true;
    presetSelector.clear();
    presetSelector.addItem (placeholderText.isNotEmpty() ? placeholderText : "-- No Plugin Loaded --", 1);
    presetSelector.setSelectedId (1, juce::dontSendNotification);
    presetSelector.setEnabled (false);
    resetButton.setEnabled (false);
    suppressPresetChangeCallback = false;
}

void PluginSubWindowContainer::setSelectedProgramIndex (int selectedIndex)
{
    if (presetSelector.getNumItems() <= 0)
        return;

    const int clampedIndex = juce::jlimit (0, presetSelector.getNumItems() - 1, selectedIndex);
    suppressPresetChangeCallback = true;
    presetSelector.setSelectedId (clampedIndex + 1, juce::dontSendNotification);
    suppressPresetChangeCallback = false;
}

void PluginSubWindowContainer::setProgramSelectionCallback (ProgramSelectionCallback callback)
{
    onProgramSelectionChanged = std::move (callback);
}

void PluginSubWindowContainer::setResetCallback (ResetCallback callback)
{
    onResetRequested = std::move (callback);
}
} // namespace myapp::ui
