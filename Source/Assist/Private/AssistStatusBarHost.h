#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"

class SDockTab;
class SVerticalBox;
class SWidget;
class SWindow;

// Hosts the current editor's original status bar below the outer docking layout.
// All persistent references are weak so layout reloads can release old widgets.
class FAssistStatusBarHost {
   public:
    ~FAssistStatusBarHost();
    void Update(const TSharedPtr<SDockTab>& Editor);
    void Restore();

    // Moves a status bar from its native vertical-box slot; also used by the
    // widget regression tests without creating native windows or editor tabs.
    bool Attach(const TSharedRef<SWindow>& Window, const TSharedRef<SWidget>& StatusBar);

   private:
    bool IsInstalled(const TSharedRef<SWindow>& Window) const;

    TWeakPtr<SWindow> HostedWindow;
    TWeakPtr<SVerticalBox> WindowContent;
    TWeakPtr<SVerticalBox> OriginalOwner;
    TWeakPtr<SWidget> OriginalPlaceholder;
    TWeakPtr<SWidget> HostedStatusBar;
    TWeakPtr<SDockTab> HostedEditor;
    FMargin OriginalPadding;
};
