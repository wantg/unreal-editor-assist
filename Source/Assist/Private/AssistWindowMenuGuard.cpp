#include "AssistWindowMenuGuard.h"

#include "Templates/UnrealTemplate.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWidget.h"

FAssistWindowMenuGuard::FAssistWindowMenuGuard(const TSharedRef<IWindowTitleBar>& InOriginal) : Original(InOriginal) {
    Original->SetAllowMenuBar(true);
}

void FAssistWindowMenuGuard::Flash() {
    Original->Flash();
}

void FAssistWindowMenuGuard::UpdateWindowMenu(TSharedPtr<SWidget> MenuContent) {
    // Local tool managers send nullptr when they do not own a menu. New asset
    // editors also do this before building their menu. Keep the current menu
    // until the selected editor supplies a valid replacement.
    if (!bApplyingEditorMenu || !MenuContent || MenuContent == SNullWidget::NullWidget || PresentedMenu.Pin() == MenuContent) {
        return;
    }
    Original->SetAllowMenuBar(true);
    Original->UpdateWindowMenu(MenuContent);
    PresentedMenu = MenuContent;
}

void FAssistWindowMenuGuard::UpdateBackgroundContent(TSharedPtr<SWidget> BackgroundContent) {
    Original->UpdateBackgroundContent(BackgroundContent);
}

void FAssistWindowMenuGuard::SetAllowMenuBar(bool bAllow) {
    // Docking refreshes can temporarily report no major tab during asset-editor
    // construction. A workspace always reserves its main menu row.
    Original->SetAllowMenuBar(true);
}

void FAssistWindowMenuGuard::ApplyEditorMenu(TFunctionRef<void()> Update) {
    TGuardValue<bool> Applying(bApplyingEditorMenu, true);
    Update();
}

float FAssistWindowMenuGuard::GetMenuInset() const {
#if PLATFORM_MAC
    constexpr float NativeInset = 30.0f;
#else
    constexpr float NativeInset = 25.0f;
#endif
    const TSharedPtr<SWidget> Menu = PresentedMenu.Pin();
    return Menu ? FMath::Max(NativeInset, static_cast<float>(Menu->GetDesiredSize().Y) + 3.0f) : NativeInset;
}
