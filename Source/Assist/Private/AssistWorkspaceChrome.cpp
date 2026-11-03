#include "AssistWorkspaceChrome.h"

#include "Framework/Docking/SDockingTabStack.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

namespace {
const FName HeaderTag(TEXT("Assist.WorkspaceHeader"));

TSharedPtr<SVerticalBox> FindBody(const TSharedRef<SDockingTabStack>& Stack) {
    FChildren* const Children = Stack->GetChildren();
    if (Children->Num() != 1 || Children->GetChildAt(0)->GetType() != FName(TEXT("SVerticalBox"))) {
        return nullptr;
    }
    const TSharedRef<SVerticalBox> Body = StaticCastSharedRef<SVerticalBox>(Children->GetChildAt(0));
    if (Body->NumSlots() != 2) {
        return nullptr;
    }
    return Body;
}

TSharedPtr<SVerticalBox> FindTitleRows(const TSharedRef<SWidget>& Header) {
    if (Header->GetType() != FName(TEXT("SBorder")) || Header->GetChildren()->Num() != 1) {
        return nullptr;
    }
    const TSharedRef<SWidget> Rows = Header->GetChildren()->GetChildAt(0);
    if (Rows->GetType() != FName(TEXT("SVerticalBox"))) {
        return nullptr;
    }
    const TSharedRef<SVerticalBox> Box = StaticCastSharedRef<SVerticalBox>(Rows);
    if (Box->NumSlots() != 2) {
        return nullptr;
    }
    return Box;
}
}

void AssistWorkspaceChrome::FinishMainFrameReconstruction(const TSharedRef<SWindow>& Window) {
    // SWindow::Construct does not reset a previously independent viewport size.
    // A normal editor main frame must render/hit-test at the native client size,
    // including after the user subsequently zooms or restores the window.
    Window->SetViewportSizeDrivenByWindow(true);
#if PLATFORM_MAC
    // Use UE's Cocoa resize notification path to refresh both the cached screen
    // position and client size, and resize the renderer to the actual native view.
    SynchronizeMacWindowGeometry(Window);
#endif
    Window->InvalidateRootChildOrder();
}

bool AssistWorkspaceChrome::UpdateToolHost(const TSharedRef<SDockingTabStack>& Stack, float MenuInset, bool bHasTools, bool bCanAcceptDrag) {
    const TSharedPtr<SVerticalBox> Body = FindBody(Stack);
    if (!Body) {
        return false;
    }
    TSharedRef<SWidget> Header = Body->GetSlot(0).GetWidget();
    TSharedPtr<SBox> Spacer;
    if (Header->GetType() == FName(TEXT("SBox")) && Header->GetTag() == HeaderTag && Header->GetChildren()->Num() == 1) {
        Spacer = StaticCastSharedRef<SBox>(Header);
        Header = Header->GetChildren()->GetChildAt(0);
    }
    const TSharedPtr<SVerticalBox> Rows = FindTitleRows(Header);
    if (!Rows) {
        return false;
    }
    // Hide the complete host header, not selected children of its title row.
    // The original tab well stays attached, keeping SDockTab window lookup valid.
    // Hidden, rather than Collapsed: SBox ignores HeightOverride for a
    // collapsed child, which would remove the menu spacer too.
    Header->SetVisibility(EVisibility::Hidden);
    if (!Spacer) {
        Body->GetSlot(0).AttachWidget(SNullWidget::NullWidget);
        Spacer = SNew(SBox).HeightOverride(MenuInset)[Header];
        Spacer->SetTag(HeaderTag);
        Body->GetSlot(0).AttachWidget(Spacer.ToSharedRef());
    } else {
        Spacer->SetHeightOverride(MenuInset);
    }
    // Collapse the splitter child, not just its content, so no width/height or
    // resize handle remains. Keep the node/coefficient for subsequent docking.
    Stack->SetVisibility(bHasTools || bCanAcceptDrag ? EVisibility::Visible : EVisibility::Collapsed);
    return true;
}

bool AssistWorkspaceChrome::ReserveEditorMenu(const TSharedRef<SDockingTabStack>& Stack, float MenuInset) {
    const TSharedPtr<SVerticalBox> Body = FindBody(Stack);
    const TSharedPtr<SVerticalBox> Rows = Body ? FindTitleRows(Body->GetSlot(0).GetWidget()) : nullptr;
    if (!Rows) {
        return false;
    }
    FMargin Padding = Rows->GetSlot(0).GetPadding();
    if (!FMath::IsNearlyEqual(Padding.Top, MenuInset)) {
        Padding.Top = MenuInset;
        Rows->GetSlot(0).SetPadding(Padding);
    }
    return true;
}
