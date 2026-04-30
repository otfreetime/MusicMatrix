#include "PluginSubWindowContainer.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

// Include debug logger for file-based logging
#include "../debug/DebugLogger.h"

namespace myapp::ui
{
PluginSubWindowContainer::PluginSubWindowContainer() = default;

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

        // Position the child window at (0,0) within the container's client area
        auto containerBounds = getLocalBounds();
        
        DEBUG_LOG ("  Setting VST2 window at client position 0,0 size: " 
                  + juce::String (containerBounds.getWidth()) + "x" + juce::String (containerBounds.getHeight()));

        const BOOL setResult = SetWindowPos (pluginHwnd,
                            HWND_TOP,
                            0,     // X position within container client area
                            0,     // Y position within container client area
                            containerBounds.getWidth(),
                            containerBounds.getHeight(),
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
    // Hide container visual - VST2 window will cover this area
    g.fillAll (juce::Colour (0xFF000000));
}

void PluginSubWindowContainer::resized()
{
    attachEmbeddedWindowIfPossible();
}
} // namespace myapp::ui
