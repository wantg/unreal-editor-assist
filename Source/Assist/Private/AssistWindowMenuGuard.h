#pragma once

#include "Application/SlateApplicationBase.h"

// The native title-bar widget remains in the window's Slate tree. This logical
// proxy filters menu updates before they can clear/reparent its menu slot.
class FAssistWindowMenuGuard : public IWindowTitleBar {
   public:
    explicit FAssistWindowMenuGuard(const TSharedRef<IWindowTitleBar>& InOriginal);
    virtual void Flash() override;
    virtual void UpdateWindowMenu(TSharedPtr<SWidget> MenuContent) override;
    virtual void UpdateBackgroundContent(TSharedPtr<SWidget> BackgroundContent) override;
    virtual void SetAllowMenuBar(bool bAllow) override;

    void ApplyEditorMenu(TFunctionRef<void()> Update);
    TSharedRef<IWindowTitleBar> GetOriginal() const { return Original; }
    float GetMenuInset() const;

   private:
    TSharedRef<IWindowTitleBar> Original;
    TWeakPtr<SWidget> PresentedMenu;
    bool bApplyingEditorMenu = false;
};
