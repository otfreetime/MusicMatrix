#include "PluginSubWindowContainer.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

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
    if (embeddedWindow == nullptr)
    {
        lastAttachStatus = "embed_error:null_hwnd";
        return;
    }

    if (auto* peer = getPeer())
    {
        const auto hostHwnd  = reinterpret_cast<HWND> (peer->getNativeHandle());
        const auto pluginHwnd = reinterpret_cast<HWND> (embeddedWindow);

        if (hostHwnd == nullptr || pluginHwnd == nullptr)
        {
            lastAttachStatus = "embed_error:invalid_hwnd";
            return;
        }

        if (! IsWindow (pluginHwnd))
        {
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
            lastAttachStatus = "embed_error:get_style:" + juce::String ((int) styleReadError);
            return;
        }

        // Remove popup style and make it look like a child visually, but keep it top-level
        style &= ~WS_POPUP;
        style |= WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

        SetLastError (0);
        const auto styleSetResult = SetWindowLongPtr (pluginHwnd, GWL_STYLE, style);
        const auto styleSetError = GetLastError();
        if (styleSetResult == 0 && styleSetError != 0)
        {
            lastAttachStatus = "embed_error:set_style:" + juce::String ((int) styleSetError);
            return;
        }

        auto screenBounds = getScreenBounds();
        POINT topLeft { screenBounds.getX(), screenBounds.getY() };
        if (MapWindowPoints (HWND_DESKTOP, hostHwnd, &topLeft, 1) == 0)
        {
            const auto mapError = GetLastError();
            if (mapError != 0)
            {
                lastAttachStatus = "embed_error:map_points:" + juce::String ((int) mapError);
                return;
            }
        }

        // Use SetWindowPos with HWND_TOP to keep it above the host, but don't use SetParent
        if (! SetWindowPos (pluginHwnd,
                            HWND_TOP,
                            topLeft.x,
                            topLeft.y,
                            screenBounds.getWidth(),
                            screenBounds.getHeight(),
                            SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_NOACTIVATE))
        {
            const auto posError = GetLastError();
            lastAttachStatus = "embed_error:set_pos:" + juce::String ((int) posError);
            return;
        }

        ShowWindow (pluginHwnd, SW_SHOW);
        lastAttachStatus = "embed_ok";
    }
    else
    {
        lastAttachStatus = "embed_error:no_peer";
    }
#endif
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
    g.fillAll (juce::Colour (0xFF121212));
    g.setColour (juce::Colour (0xFFFFBF00));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 1.5f);
    g.setColour (juce::Colours::grey);
    g.drawFittedText ("Plugin Sub-Window Container",
                      getLocalBounds().reduced (8),
                      juce::Justification::centred,
                      1);
}

void PluginSubWindowContainer::resized()
{
    attachEmbeddedWindowIfPossible();
}
} // namespace myapp::ui
