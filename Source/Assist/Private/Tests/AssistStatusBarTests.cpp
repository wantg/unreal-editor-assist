#include "AssistStatusBarHost.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"

namespace {
struct FStatusBarFixture {
    TSharedRef<SWindow> Window = SNew(SWindow).CreateTitleBar(false).ClientSize(FVector2D(1000, 700));
    TSharedRef<SWidget> Bar = SNew(SBorder);
    TSharedRef<SWidget> OtherBar = SNew(SBorder);
    TSharedRef<SVerticalBox> Editor = SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1.0f)[SNew(SBox)]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)[Bar];
    TSharedRef<SVerticalBox> OtherEditor = SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1.0f)[SNew(SBox)]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)[OtherBar];
    // Editors may occupy independent central tab stacks side by side.
    TSharedRef<SHorizontalBox> Body = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(0.5f)[Editor]
        + SHorizontalBox::Slot().FillWidth(0.5f)[OtherEditor];

    FStatusBarFixture() {
        // Never add this window to Slate: tests must not create/replace native
        // editor windows or touch the user's live tab manager and layout files.
        Window->SetContent(Body);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistStatusBarHostTest, "Assist.Layout.StatusBar.MoveAndRestore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistStatusBarHostTest::RunTest(const FString& Parameters) {
    FStatusBarFixture UI;
    FAssistStatusBarHost Host;
    const FVector2D Size(UI.Window->GetSizeInScreen());
    const FVector2D Position(UI.Window->GetPositionInScreen());
    if (!TestTrue(TEXT("The native vertical-box status bar can be moved"), Host.Attach(UI.Window, UI.Bar))) {
        return false;
    }
    const TSharedRef<SWidget> Wrapper = UI.Window->GetContent();
    if (!TestEqual(TEXT("The window hosts a vertical arrangement"), Wrapper->GetType(), FName(TEXT("SVerticalBox")))) {
        return false;
    }
    const TSharedRef<SVerticalBox> Outer = StaticCastSharedRef<SVerticalBox>(Wrapper);
    TestEqual(TEXT("Only the docking content and bottom status bar are hosted"), Outer->NumSlots(), 2);
    TestTrue(TEXT("Both editor and CB regions remain above the status bar"), Outer->GetSlot(0).GetWidget() == UI.Body);
    TestTrue(TEXT("The original status bar is at the bottom"), Outer->GetSlot(1).GetWidget() == UI.Bar);
    TestTrue(TEXT("The bar is removed from its old slot"), UI.Editor->GetSlot(1).GetWidget() != UI.Bar);
    TestEqual(TEXT("Its old slot has no leftover padding"), UI.Editor->GetSlot(1).GetPadding().Top, 0.0f);
    TestEqual(TEXT("The bottom bar retains its native padding"), Outer->GetSlot(1).GetPadding().Top, 2.0f);
    TestTrue(TEXT("Repeated updates succeed"), Host.Attach(UI.Window, UI.Bar));
    TestTrue(TEXT("Repeated updates do not nest wrappers"), UI.Window->GetContent() == Wrapper);
    TestTrue(TEXT("No window resize occurs"), FVector2D(UI.Window->GetSizeInScreen()).Equals(Size));
    TestTrue(TEXT("No window movement occurs"), FVector2D(UI.Window->GetPositionInScreen()).Equals(Position));

    Host.Restore();
    TestTrue(TEXT("The original window content is restored"), UI.Window->GetContent() == UI.Body);
    TestTrue(TEXT("The status bar returns to its editor"), UI.Editor->GetSlot(1).GetWidget() == UI.Bar);
    TestEqual(TEXT("The original status bar padding is restored"), UI.Editor->GetSlot(1).GetPadding().Top, 2.0f);
    Host.Restore();
    TestTrue(TEXT("Restoring twice is harmless"), UI.Window->GetContent() == UI.Body);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistStatusBarSwitchTest, "Assist.Layout.StatusBar.SwitchAndReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistStatusBarSwitchTest::RunTest(const FString& Parameters) {
    FStatusBarFixture UI;
    FAssistStatusBarHost Host;
    if (!TestTrue(TEXT("The first editor's bar is hosted"), Host.Attach(UI.Window, UI.Bar)) ||
        !TestTrue(TEXT("The second editor's bar can replace it"), Host.Attach(UI.Window, UI.OtherBar))) {
        return false;
    }
    TestTrue(TEXT("The first editor gets its own bar back"), UI.Editor->GetSlot(1).GetWidget() == UI.Bar);
    const TSharedRef<SVerticalBox> Outer = StaticCastSharedRef<SVerticalBox>(UI.Window->GetContent());
    TestTrue(TEXT("The new editor's actual bar is used"), Outer->GetSlot(1).GetWidget() == UI.OtherBar);
    TestTrue(TEXT("Switching editors keeps only one wrapper"), Outer->GetSlot(0).GetWidget() == UI.Body);

    const TSharedRef<SWidget> NewLayout = SNew(SBox);
    UI.Window->SetContent(NewLayout);
    Host.Restore();
    TestTrue(TEXT("A newer layout is not replaced by the old layout"), UI.Window->GetContent() == NewLayout);
    TestTrue(TEXT("The old editor's status bar is detached from the wrapper"), UI.OtherEditor->GetSlot(1).GetWidget() == UI.OtherBar);
    TestTrue(TEXT("The wrapper no longer owns the old status bar"), Outer->GetSlot(1).GetWidget() == SNullWidget::NullWidget);
    const TSharedRef<SWidget> UnsupportedBar = SNew(SBorder);
    TestFalse(TEXT("An unfamiliar status bar host is left alone"), Host.Attach(UI.Window, UnsupportedBar));
    TestTrue(TEXT("An unsupported host does not change window content"), UI.Window->GetContent() == NewLayout);
    return true;
}
#endif
