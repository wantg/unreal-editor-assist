#include "AssistWindowMenuGuard.h"
#include "AssistWorkspaceChrome.h"
#include "AssistLayout.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/Docking/SDockingTabStack.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Input/HittestGrid.h"
#include "Layout/ArrangedChildren.h"
#include "Rendering/DrawElements.h"
#include "Misc/AutomationTest.h"
#if PLATFORM_MAC
#include "Misc/ConfigCacheIni.h"
#endif
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SWindow.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SBoxPanel.h"

namespace {
class FRecordingTitleBar : public IWindowTitleBar {
   public:
    virtual void Flash() override { ++Flashes; }
    virtual void UpdateWindowMenu(TSharedPtr<SWidget> Menu) override {
        Presented = Menu;
        ++MenuUpdates;
    }
    virtual void UpdateBackgroundContent(TSharedPtr<SWidget> Content) override { Background = Content; }
    virtual void SetAllowMenuBar(bool bAllow) override { bMenuAllowed = bAllow; }
    TSharedPtr<SWidget> Presented;
    TSharedPtr<SWidget> Background;
    int32 MenuUpdates = 0;
    int32 Flashes = 0;
    bool bMenuAllowed = false;
};

class FWorkspaceTestNativeWindow : public FGenericWindow {
   public:
    virtual float GetDPIScaleFactor() const override { return Scale; }
    virtual bool IsMaximized() const override { return bMaximized; }
    virtual bool IsVisible() const override { return true; }
    virtual void ReshapeWindow(int32 X, int32 Y, int32 Width, int32 Height) override { ++Reshapes; }
    virtual void MoveWindowTo(int32 X, int32 Y) override { ++Moves; }
    virtual void Maximize() override { ++Maximizes; }
    int32 Reshapes = 0;
    int32 Moves = 0;
    int32 Maximizes = 0;
    float Scale = 1.0f;
    bool bMaximized = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistMenuGuardTest, "Assist.Layout.Workspace.StableEditorMenu",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistMenuGuardTest::RunTest(const FString& Parameters) {
    const TSharedRef<FRecordingTitleBar> Native = MakeShared<FRecordingTitleBar>();
    FAssistWindowMenuGuard Guard(Native);
    const TSharedRef<SWidget> EditorMenu = SNew(SBox);
    const TSharedRef<SWidget> OtherMenu = SNew(SBox);
    Guard.ApplyEditorMenu([&] { Guard.UpdateWindowMenu(EditorMenu); });
    TestTrue(TEXT("The selected editor's menu is presented"), Native->Presented == EditorMenu);
    TestEqual(TEXT("One native menu update occurred"), Native->MenuUpdates, 1);

    Guard.UpdateWindowMenu(nullptr);
    Guard.UpdateWindowMenu(OtherMenu);
    Guard.SetAllowMenuBar(false);
    TestTrue(TEXT("Tool/foreign updates cannot replace the editor menu"), Native->Presented == EditorMenu);
    TestTrue(TEXT("Docking refreshes cannot hide the workspace menu row"), Native->bMenuAllowed);
    TestEqual(TEXT("Tool focus causes no native menu reattachment"), Native->MenuUpdates, 1);

    Guard.ApplyEditorMenu([&] { Guard.UpdateWindowMenu(nullptr); });
    Guard.ApplyEditorMenu([&] { Guard.UpdateWindowMenu(SNullWidget::NullWidget); });
    TestTrue(TEXT("An initializing asset editor cannot clear the previous menu"), Native->Presented == EditorMenu);
    Guard.ApplyEditorMenu([&] { Guard.UpdateWindowMenu(EditorMenu); });
    TestEqual(TEXT("Repeated pre-draw updates do not reparent the same menu"), Native->MenuUpdates, 1);
    Guard.ApplyEditorMenu([&] { Guard.UpdateWindowMenu(OtherMenu); });
    TestTrue(TEXT("A ready selected editor can replace the menu"), Native->Presented == OtherMenu);
    TestEqual(TEXT("The ready editor causes exactly one replacement"), Native->MenuUpdates, 2);
    Guard.UpdateWindowMenu(EditorMenu);
    TestTrue(TEXT("The update gate closes after each selected-editor update"), Native->Presented == OtherMenu);
    Guard.UpdateBackgroundContent(EditorMenu);
    Guard.Flash();
    TestTrue(TEXT("Native background content is forwarded"), Native->Background == EditorMenu);
    TestEqual(TEXT("Native window flashing is forwarded"), Native->Flashes, 1);
    TestTrue(TEXT("The native title bar remains available for restoration"), Guard.GetOriginal() == Native);
    TestTrue(TEXT("The menu has a nonzero stable height before prepass"), Guard.GetMenuInset() > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistWorkspaceHeaderTest, "Assist.Layout.Workspace.StableHeaderSpace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistWorkspaceHeaderTest::RunTest(const FString& Parameters) {
    const TSharedRef<SDockingTabStack> Left = SNew(SDockingTabStack, FTabManager::NewStack());
    const TSharedRef<SVerticalBox> Body = StaticCastSharedRef<SVerticalBox>(Left->GetChildren()->GetChildAt(0));
    const TSharedRef<SWidget> Header = Body->GetSlot(0).GetWidget();
    if (!TestTrue(TEXT("The structural header can be replaced with a menu spacer"), AssistWorkspaceChrome::UpdateToolHost(Left, 30.0f))) {
        return false;
    }
    const TSharedRef<SWidget> Spacer = Body->GetSlot(0).GetWidget();
    Left->SlatePrepass(1.0f);
    TestEqual(TEXT("The left tool area reserves the menu height"), static_cast<float>(Spacer->GetDesiredSize().Y), 30.0f);
    TestTrue(TEXT("The native header is kept attached"), Spacer->GetChildren()->GetChildAt(0) == Header);
    TestTrue(TEXT("The entire structural header is hidden"), Header->GetVisibility() == EVisibility::Hidden);
    AssistWorkspaceChrome::UpdateToolHost(Left, 30.0f);
    TestTrue(TEXT("Repeated updates reuse the same spacer"), Body->GetSlot(0).GetWidget() == Spacer);
    TestEqual(TEXT("No extra vertical slots are added"), Body->NumSlots(), 2);
    AssistWorkspaceChrome::UpdateToolHost(Left, 0.0f);
    Left->SlatePrepass(1.0f);
    TestEqual(TEXT("Bottom tool hosts add no empty title row"), static_cast<float>(Spacer->GetDesiredSize().Y), 0.0f);

    const TSharedRef<SDockingTabStack> Editor = SNew(SDockingTabStack, FTabManager::NewStack());
    const TSharedRef<SVerticalBox> EditorBody = StaticCastSharedRef<SVerticalBox>(Editor->GetChildren()->GetChildAt(0));
    const TSharedRef<SWidget> EditorHeader = EditorBody->GetSlot(0).GetWidget();
    const TSharedRef<SVerticalBox> Rows = StaticCastSharedRef<SVerticalBox>(EditorHeader->GetChildren()->GetChildAt(0));
    Rows->GetSlot(0).SetPadding(FMargin(7, 0, 11, 13));
    TestTrue(TEXT("Editor menu space can be reserved"), AssistWorkspaceChrome::ReserveEditorMenu(Editor, 30.0f));
    TestEqual(TEXT("The editor reserves the same menu height"), Rows->GetSlot(0).GetPadding().Top, 30.0f);
    TestEqual(TEXT("Native left reservations survive"), Rows->GetSlot(0).GetPadding().Left, 7.0f);
    TestEqual(TEXT("Native right reservations survive"), Rows->GetSlot(0).GetPadding().Right, 11.0f);
    Rows->GetSlot(0).SetPadding(FMargin(7, 0, 11, 13));
    AssistWorkspaceChrome::ReserveEditorMenu(Editor, 30.0f);
    TestEqual(TEXT("A native docking reset cannot remove the menu space"), Rows->GetSlot(0).GetPadding().Top, 30.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistEmptyToolRegionTest, "Assist.Layout.Workspace.CollapseEmptyRegions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistEmptyToolRegionTest::RunTest(const FString& Parameters) {
    for (EOrientation Orientation : {Orient_Horizontal, Orient_Vertical}) {
        const TSharedRef<SDockingTabStack> Host = SNew(SDockingTabStack, FTabManager::NewStack());
        Host->SetSizeCoefficient(0.2f);
        const TSharedRef<SWidget> Editor = SNew(SBox);
        const TSharedRef<SSplitter> Splitter = SNew(SSplitter).Orientation(Orientation).PhysicalSplitterHandleSize(5.0f)
            + SSplitter::Slot().Value(TAttribute<float>(Host, &SDockingNode::GetSizeCoefficient))[Host]
            + SSplitter::Slot().Value(0.8f)[Editor];
        const FVector2D Size(1000, 700);
        const FGeometry Geometry = FGeometry::MakeRoot(Size, FSlateLayoutTransform());
        const int32 Axis = Orientation == Orient_Horizontal ? 0 : 1;
        const float Inset = Orientation == Orient_Horizontal ? 30.0f : 0.0f;

        AssistWorkspaceChrome::UpdateToolHost(Host, Inset, false, false);
        Splitter->SlatePrepass(1.0f);
        FArrangedChildren Empty(EVisibility::Visible);
        Splitter->ArrangeChildren(Geometry, Empty);
        if (!TestEqual(TEXT("An empty host leaves only the editor visible"), Empty.Num(), 1)) {
            return false;
        }
        TestTrue(TEXT("The editor occupies all space, including the former divider"), Empty[0].Geometry.GetLocalSize().Equals(Size));
        TestTrue(TEXT("Collapsing retains the user's saved splitter ratio"), FMath::IsNearlyEqual(Host->GetSizeCoefficient(), 0.2f));

        AssistWorkspaceChrome::UpdateToolHost(Host, Inset, false, true);
        Splitter->SlatePrepass(1.0f);
        FArrangedChildren DragTargets(EVisibility::Visible);
        Splitter->ArrangeChildren(Geometry, DragTargets);
        if (!TestEqual(TEXT("A native tab drag exposes the empty docking target"), DragTargets.Num(), 2)) {
            return false;
        }
        TestTrue(TEXT("The exposed target reuses its original size"),
            FMath::IsNearlyEqual(static_cast<float>(DragTargets[0].Geometry.GetLocalSize()[Axis]), (static_cast<float>(Size[Axis]) - 5.0f) * 0.2f));
        AssistWorkspaceChrome::UpdateToolHost(Host, Inset, false, false);
        TestTrue(TEXT("An unfilled target collapses after the drag ends"), Host->GetVisibility() == EVisibility::Collapsed);
        AssistWorkspaceChrome::UpdateToolHost(Host, Inset, true, false);
        TestTrue(TEXT("A docked or reopened tool reveals its host"), Host->GetVisibility() == EVisibility::Visible);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistToolDragAcceptanceTest, "Assist.Layout.Workspace.RevealOnlyAcceptedDragTargets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistToolDragAcceptanceTest::RunTest(const FString& Parameters) {
    const TSharedRef<SWidget> LeftArea = SNew(SBox);
    const TSharedRef<SWidget> BottomArea = SNew(SBox);
    const TSharedRef<SDockingTabStack> EmptyHost = SNew(SDockingTabStack, FTabManager::NewStack());
    for (ETabRole Role : {ETabRole::MajorTab, ETabRole::PanelTab, ETabRole::DocumentTab}) {
        const TSharedRef<SDockTab> Restricted = SNew(SDockTab).TabRole(Role);
        const bool bAccepts = AssistLayout::CanAcceptToolTab(Restricted, LeftArea);
        TestFalse(TEXT("Asset/editor-local tabs are not cross-manager tool drags"), bAccepts);
        AssistWorkspaceChrome::UpdateToolHost(EmptyHost, 30.0f, false, bAccepts);
        TestTrue(TEXT("Dragging a rejected tab leaves an empty host collapsed"), EmptyHost->GetVisibility() == EVisibility::Collapsed);
        AssistWorkspaceChrome::UpdateToolHost(EmptyHost, 30.0f, true, bAccepts);
        TestTrue(TEXT("Rejecting a drag cannot hide an already populated host"), EmptyHost->GetVisibility() == EVisibility::Visible);
    }
    const TSharedRef<SDockTab> Tool = SNew(SDockTab).TabRole(ETabRole::NomadTab);
    TestTrue(TEXT("CB/Output Log/Nomad EUW drags can reveal the left area"), AssistLayout::CanAcceptToolTab(Tool, LeftArea));
    TestTrue(TEXT("Nomad tool drags can reveal the bottom area"), AssistLayout::CanAcceptToolTab(Tool, BottomArea));
    AssistWorkspaceChrome::UpdateToolHost(EmptyHost, 30.0f, false, AssistLayout::CanAcceptToolTab(Tool, LeftArea));
    TestTrue(TEXT("An accepted tool drag reveals an empty host"), EmptyHost->GetVisibility() == EVisibility::Visible);
    AssistWorkspaceChrome::UpdateToolHost(EmptyHost, 30.0f, false, AssistLayout::CanAcceptToolTab(nullptr, LeftArea));
    TestTrue(TEXT("Ending the drag folds an unfilled host again"), EmptyHost->GetVisibility() == EVisibility::Collapsed);
    TestFalse(TEXT("An uninitialized target is not offered"), AssistLayout::CanAcceptToolTab(Tool, nullptr));

    Tool->SetContent(SNew(SVerticalBox) + SVerticalBox::Slot()[LeftArea]);
    TestFalse(TEXT("A tool cannot reveal a target inside its own content"), AssistLayout::CanAcceptToolTab(Tool, LeftArea));
    TestTrue(TEXT("Another independent region can still accept that same tool"), AssistLayout::CanAcceptToolTab(Tool, BottomArea));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistReconstructedWindowTest, "Assist.Layout.Workspace.ReconstructedWindowAndHitTesting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistReconstructedWindowTest::RunTest(const FString& Parameters) {
    // Never register this window with Slate or create an actual OS window.
    const TSharedRef<FWorkspaceTestNativeWindow> Native = MakeShared<FWorkspaceTestNativeWindow>();
    SWindow::FArguments Args;
    Args.CreateTitleBar(false).UseOSWindowBorder(true).AutoCenter(EAutoCenter::None)
        .SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false).ClientSize(FVector2D(1000, 700));
    const TSharedRef<SWindow> Window = SNew(SWindow).CreateTitleBar(false).UseOSWindowBorder(true).AutoCenter(EAutoCenter::None)
        .SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false).ClientSize(FVector2D(1000, 700));
    Window->SetNativeWindow(Native);
    Window->SetCachedScreenPosition(FVector2D(320, 180));
    Window->SetIndependentViewportSize(FVector2D(1600, 900));
    Window->Construct(Args); // Match MainFrame's content-only reconstruction.
    TestFalse(TEXT("Reconstruction retains an old independent render size"), Window->IsViewportSizeDrivenByWindow());

    const TSharedRef<SWidget> MenuTarget = SNew(SBorder);
    Window->SetContent(SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(120)[MenuTarget]]
        + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SBorder)]);
    const FVector2D OriginalPosition(Window->GetPositionInScreen());
    const FVector2D OriginalSize(Window->GetSizeInScreen());
    const int32 OriginalReshapes = Native->Reshapes;
    const int32 OriginalMoves = Native->Moves;
    AssistWorkspaceChrome::FinishMainFrameReconstruction(Window);
    TestTrue(TEXT("The reused window's render size follows its client size again"), Window->IsViewportSizeDrivenByWindow());
    TestTrue(TEXT("The native window is reused"), Window->GetNativeWindow() == Native);
    TestTrue(TEXT("Finalization does not move the window"), FVector2D(Window->GetPositionInScreen()).Equals(OriginalPosition));
    TestTrue(TEXT("Finalization does not resize the window"), FVector2D(Window->GetSizeInScreen()).Equals(OriginalSize));
    TestEqual(TEXT("No native reshape is requested"), Native->Reshapes, OriginalReshapes);
    TestEqual(TEXT("No native move is requested"), Native->Moves, OriginalMoves);

    const auto PaintAndCheckHit = [&] {
        const float Scale = FSlateApplication::Get().GetApplicationScale() * Native->Scale;
        Window->SlatePrepass(Scale);
        FSlateWindowElementList Elements(Window);
        Window->PaintWindow(0.0, 0.0f, Elements, FWidgetStyle(), true);
        const FVector2D Probe = FVector2D(Window->GetPositionInScreen()) + FVector2D(40, 40) * Scale;
        const TArray<FWidgetAndPointer> Path = Window->GetHittestGrid().GetBubblePath(Probe, 0.0f, false);
        TestTrue(TEXT("The visual menu position hits its widget after reconstruction/zoom/restore"),
            Path.ContainsByPredicate([&](const FWidgetAndPointer& Item) { return Item.Widget == MenuTarget; }));
        TestTrue(TEXT("Rendering continues to follow the window's current client size"),
            FVector2D(Window->GetViewportSize()).Equals(FVector2D(Window->GetSizeInScreen())));
    };
    PaintAndCheckHit();
    // Subsequent OS zoom/restore notifications only update the normal native
    // caches; no per-frame plugin geometry observer is necessary.
    Native->bMaximized = true;
    Window->SetCachedScreenPosition(FVector2D(20, 30));
    Window->SetCachedSize(FVector2D(1800, 1000));
    PaintAndCheckHit();
    Native->bMaximized = false;
    Window->SetCachedScreenPosition(OriginalPosition);
    Window->SetCachedSize(OriginalSize);
    PaintAndCheckHit();
    return true;
}

#if PLATFORM_MAC
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistMacReconstructionPlacementTest, "Assist.Layout.Workspace.MacReconstructionKeepsNativePlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistMacReconstructionPlacementTest::RunTest(const FString& Parameters) {
    // Only an in-memory config cache and unregistered native-window stubs are
    // used. Never edit the user's settings or create/resize an actual OS window.
    FConfigCacheIni Config(EConfigCacheType::Temporary);
    Config.DisableFileOperations();
    const FString SettingsFile(TEXT("AssistReconstructionTest.ini"));
    Config.Add(SettingsFile, FConfigFile());
    const FVector2D Position(320, 180);
    const FVector2D Size(1920, 1170);
    const FString SavedPosition = FVector2D(40, 60).ToString();
    const FString SavedSize = FVector2D(1000, 700).ToString();

    for (float Scale : {1.0f, 2.0f}) {
        for (bool bMaximized : {false, true}) {
            Config.SetString(TEXT("RootWindow"), TEXT("ScreenPosition"), *SavedPosition, SettingsFile);
            Config.SetString(TEXT("RootWindow"), TEXT("WindowSize"), *SavedSize, SettingsFile);
            Config.SetBool(TEXT("RootWindow"), TEXT("InitiallyMaximized"), bMaximized, SettingsFile);
            const TSharedRef<FWorkspaceTestNativeWindow> Native = MakeShared<FWorkspaceTestNativeWindow>();
            Native->Scale = Scale;
            Native->bMaximized = bMaximized;
            const TSharedRef<SWindow> Window = SNew(SWindow).CreateTitleBar(false).UseOSWindowBorder(true)
                .AutoCenter(EAutoCenter::None).SaneWindowPlacement(false)
                .AdjustInitialSizeAndPositionForDPIScale(false).ClientSize(Size);
            Window->SetNativeWindow(Native);
            Window->SetCachedScreenPosition(Position);
            Window->SetIndependentViewportSize(FVector2D(1600, 900));

            for (int32 Attempt = 0; Attempt < 2; ++Attempt) {
                AssistWorkspaceChrome::ReconstructMacMainFrameContent(Window, Config, SettingsFile, [&] {
                    FString Value;
                    FVector2D RebuildPosition, RebuildSize;
                    Config.GetString(TEXT("RootWindow"), TEXT("ScreenPosition"), Value, SettingsFile);
                    TestTrue(TEXT("The temporary screen position parses"), RebuildPosition.InitFromString(Value));
                    TestTrue(TEXT("Reconstruction uses the current position at native DPI"), RebuildPosition.Equals(Position / Scale));
                    Config.GetString(TEXT("RootWindow"), TEXT("WindowSize"), Value, SettingsFile);
                    TestTrue(TEXT("The temporary client size parses"), RebuildSize.InitFromString(Value));
                    TestTrue(TEXT("Reconstruction uses the current client size, not the normal restore size"), RebuildSize.Equals(Size / Scale));
                    bool bInitiallyMaximized = true;
                    Config.GetBool(TEXT("RootWindow"), TEXT("InitiallyMaximized"), bInitiallyMaximized, SettingsFile);
                    TestFalse(TEXT("Rebuilding an existing window must not request another maximize"), bInitiallyMaximized);

                    // Model MainFrame's branch: true substitutes the full display
                    // size, then Construct reshapes the existing native window.
                    const FVector2D RequestedSize = bInitiallyMaximized ? FVector2D(1920, 1200) : RebuildSize * Scale;
                    SWindow::FArguments Args;
                    Args.CreateTitleBar(false).UseOSWindowBorder(true).AutoCenter(EAutoCenter::None)
                        .SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false).ClientSize(RequestedSize);
                    Window->Construct(Args);
                    if (bInitiallyMaximized) {
                        Window->Maximize();
                    }
                });
                AssistWorkspaceChrome::FinishMainFrameReconstruction(Window);
                TestEqual(TEXT("Repeated reconstruction never reshapes the native window"), Native->Reshapes, 0);
                TestEqual(TEXT("Repeated reconstruction never moves the native window"), Native->Moves, 0);
                TestEqual(TEXT("Repeated reconstruction never zooms the native window"), Native->Maximizes, 0);
                TestEqual(TEXT("Native maximized state is retained"), Window->IsWindowMaximized(), bMaximized);
                TestTrue(TEXT("The renderer uses the unchanged client size"), FVector2D(Window->GetViewportSize()).Equals(Size));
                FString Value;
                Config.GetString(TEXT("RootWindow"), TEXT("ScreenPosition"), Value, SettingsFile);
                TestEqual(TEXT("The saved startup position is retained"), Value, SavedPosition);
                Config.GetString(TEXT("RootWindow"), TEXT("WindowSize"), Value, SettingsFile);
                TestEqual(TEXT("The saved normal window size is retained"), Value, SavedSize);
                bool bSavedMaximized = !bMaximized;
                Config.GetBool(TEXT("RootWindow"), TEXT("InitiallyMaximized"), bSavedMaximized, SettingsFile);
                TestEqual(TEXT("The saved startup maximize setting is retained"), bSavedMaximized, bMaximized);
            }
        }
    }

    for (const TCHAR* Key : {TEXT("ScreenPosition"), TEXT("WindowSize"), TEXT("InitiallyMaximized")}) {
        Config.RemoveKey(TEXT("RootWindow"), Key, SettingsFile);
    }
    const TSharedRef<SWindow> Window = SNew(SWindow).CreateTitleBar(false);
    AssistWorkspaceChrome::ReconstructMacMainFrameContent(Window, Config, SettingsFile, [] {});
    for (const TCHAR* Key : {TEXT("ScreenPosition"), TEXT("WindowSize"), TEXT("InitiallyMaximized")}) {
        FString Value;
        TestFalse(TEXT("Missing startup settings are not permanently introduced"), Config.GetString(TEXT("RootWindow"), Key, Value, SettingsFile));
    }
    return true;
}
#endif
#endif
