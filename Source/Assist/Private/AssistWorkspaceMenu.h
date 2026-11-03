#pragma once

#include "CoreMinimal.h"

class SDockTab;
class SWidget;
class SWindow;
class FWidgetPath;
class FWeakWidgetPath;
class FAssistWindowMenuGuard;
struct FFocusEvent;

// Selects the main editor's menu without changing keyboard focus. A title-bar
// guard rejects tool/empty updates synchronously, before they reach the widget.
class FAssistWorkspaceMenu : public TSharedFromThis<FAssistWorkspaceMenu> {
   public:
    void Register();
    void Unregister();
    void SuspendTitleBar();
    float GetMenuInset() const;
    TSharedPtr<SDockTab> GetCurrentEditor() const;

   private:
    void ObserveTab(const TSharedPtr<SDockTab>& Tab);
    void ObservePath(const FWidgetPath& Path);
    void OnFocusChanging(const FFocusEvent& Event, const FWeakWidgetPath& OldPath,
        const TSharedPtr<SWidget>& OldWidget, const FWidgetPath& NewPath, const TSharedPtr<SWidget>& NewWidget);
    void OnTabForegrounded(TSharedPtr<SDockTab> NewTab, TSharedPtr<SDockTab> OldTab);
    void OnMainFrameCreated(TSharedPtr<SWindow> Window, bool bStartupDialog);
    void RefreshMenu(float DeltaTime);

    TWeakPtr<SDockTab> LastEditorTab;
    TWeakPtr<SWindow> GuardedWindow;
    TSharedPtr<FAssistWindowMenuGuard> MenuGuard;
    FDelegateHandle FocusChangingHandle;
    FDelegateHandle TabForegroundedHandle;
    FDelegateHandle MainFrameCreatedHandle;
    FDelegateHandle PreTickHandle;
};
