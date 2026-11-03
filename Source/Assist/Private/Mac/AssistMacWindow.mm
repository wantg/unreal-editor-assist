#include "AssistWorkspaceChrome.h"

#include "Framework/Application/SlateApplication.h"
#include "Mac/MacApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"
#include "Widgets/SWindow.h"

void AssistWorkspaceChrome::ReconstructMacMainFrameContent(const TSharedRef<SWindow>& Window, FConfigCacheIni& Config,
    const FString& SettingsFile, TFunctionRef<void()> Reconstruct) {
    // Refresh the existing Cocoa client geometry before MainFrame reads it. Do
    // not use GetRestoredDimensions: content reconstruction needs the current
    // client size, not the normal (non-maximized) placement used at startup.
    Window->SetViewportSizeDrivenByWindow(true);
    SynchronizeMacWindowGeometry(Window);

    struct FSetting {
        const TCHAR* Key;
        FString Value;
        bool bWasPresent = false;
    };
    FSetting Settings[] = {
        {TEXT("ScreenPosition"), FString()},
        {TEXT("WindowSize"), FString()},
        {TEXT("InitiallyMaximized"), FString()},
    };
    for (FSetting& Setting : Settings) {
        Setting.bWasPresent = Config.GetString(TEXT("RootWindow"), Setting.Key, Setting.Value, SettingsFile);
    }
    ON_SCOPE_EXIT {
        for (const FSetting& Setting : Settings) {
            if (Setting.bWasPresent) {
                Config.SetString(TEXT("RootWindow"), Setting.Key, *Setting.Value, SettingsFile);
            } else {
                Config.RemoveKey(TEXT("RootWindow"), Setting.Key, SettingsFile);
            }
        }
    };

    // These are the DPI-normalized inputs consumed by SWindow::Construct.
    // InitiallyMaximized is a creation instruction, not the native window state:
    // false prevents the full-display resize/zoom branch on an already shown
    // window, and does not restore/unmaximize that window.
    const float Scale = Window->GetDPIScaleFactor();
    const FVector2D Position = FVector2D(Window->GetPositionInScreen()) / Scale;
    const FVector2D Size = FVector2D(Window->GetSizeInScreen()) / Scale;
    Config.SetString(TEXT("RootWindow"), TEXT("ScreenPosition"), *Position.ToString(), SettingsFile);
    Config.SetString(TEXT("RootWindow"), TEXT("WindowSize"), *Size.ToString(), SettingsFile);
    Config.SetBool(TEXT("RootWindow"), TEXT("InitiallyMaximized"), false, SettingsFile);
    Reconstruct();
}

void AssistWorkspaceChrome::SynchronizeMacWindowGeometry(const TSharedRef<SWindow>& Window) {
    const TSharedPtr<FGenericWindow> Native = Window->GetNativeWindow();
    if (!Native || !MacApplication || FSlateApplication::Get().IsRenderingOffScreen()) {
        return;
    }
    void* const Handle = Native->GetOSWindowHandle();
    if (!Handle) {
        return;
    }
    // Look up the actual registered Cocoa window instead of assuming that an
    // arbitrary native window (e.g. an automation-test stub) is an FMacWindow.
    const TSharedPtr<FMacWindow> MacWindow = MacApplication->FindWindowByNSWindow((FCocoaWindow*)Handle);
    if (MacWindow) {
        // This reads the existing native view; it does not move, resize or zoom
        // the OS window. UE performs its own Retina/notch coordinate conversion.
        MacApplication->OnWindowDidResize(MacWindow.ToSharedRef(), false);
    }
}
