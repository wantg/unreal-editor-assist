#include "AssistStatusBarHost.h"
#include "AssistLayout.h"

#include "Framework/Docking/SDockingTabStack.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"

namespace {
TSharedPtr<SWidget> FindStatusBar(const TSharedRef<SWidget>& Widget) {
    if (Widget->GetType() == FName(TEXT("SStatusBar"))) {
        return Widget;
    }
    FChildren* const Children = Widget->GetChildren();
    // Native editor hosts put the status bar last. Search from the bottom to
    // avoid walking the editor's viewport/panel tree in the usual case.
    for (int32 Index = Children->Num() - 1; Index >= 0; --Index) {
        if (TSharedPtr<SWidget> StatusBar = FindStatusBar(Children->GetChildAt(Index))) {
            return StatusBar;
        }
    }
    return nullptr;
}

bool IsInside(const TSharedPtr<SWidget>& Widget, const TSharedRef<SWidget>& Ancestor) {
    for (TSharedPtr<SWidget> Parent = Widget; Parent; Parent = Parent->GetParentWidget()) {
        if (Parent == Ancestor) {
            return true;
        }
    }
    return false;
}
}

FAssistStatusBarHost::~FAssistStatusBarHost() {
    Restore();
}

bool FAssistStatusBarHost::IsInstalled(const TSharedRef<SWindow>& Window) const {
    const TSharedPtr<SVerticalBox> Content = WindowContent.Pin();
    const TSharedPtr<SVerticalBox> Owner = OriginalOwner.Pin();
    const TSharedPtr<SWidget> Placeholder = OriginalPlaceholder.Pin();
    const TSharedPtr<SWidget> Bar = HostedStatusBar.Pin();
    if (HostedWindow.Pin() != Window || !Content || Window->GetContent() != Content || !Owner || !Placeholder || !Bar ||
        Content->GetSlot(1).GetWidget() != Bar) {
        return false;
    }
    for (int32 Index = 0; Index < Owner->NumSlots(); ++Index) {
        if (Owner->GetSlot(Index).GetWidget() == Placeholder) {
            return true;
        }
    }
    return false;
}

bool FAssistStatusBarHost::Attach(const TSharedRef<SWindow>& Window, const TSharedRef<SWidget>& StatusBar) {
    if (IsInstalled(Window) && HostedStatusBar.Pin() == StatusBar) {
        return true;
    }
    Restore();

    // Both SLevelEditor and SStandaloneAssetEditorToolkitHost use a final
    // AutoHeight slot in an SVerticalBox. Leave unfamiliar editor hosts alone.
    const TSharedPtr<SWidget> Parent = StatusBar->GetParentWidget();
    if (!Parent || Parent->GetType() != FName(TEXT("SVerticalBox"))) {
        return false;
    }
    const TSharedRef<SVerticalBox> Owner = StaticCastSharedRef<SVerticalBox>(Parent.ToSharedRef());
    for (int32 Index = 0; Index < Owner->NumSlots(); ++Index) {
        SVerticalBox::FSlot& Slot = Owner->GetSlot(Index);
        if (Slot.GetWidget() != StatusBar) {
            continue;
        }
        if (Index != Owner->NumSlots() - 1 || Slot.GetSizeRule() != FSizeParam::SizeRule_Auto) {
            return false;
        }
        OriginalPadding = Slot.GetPadding();
        const TSharedRef<SWidget> Placeholder = SNew(SBox);
        Slot.AttachWidget(Placeholder);
        Slot.SetPadding(FMargin(0));

        const TSharedRef<SWidget> DockingContent = Window->GetContent();
        Window->SetContent(SNullWidget::NullWidget);
        const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
            + SVerticalBox::Slot().FillHeight(1.0f)
            [
                DockingContent
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(OriginalPadding)
            [
                StatusBar
            ];
        Window->SetContent(Content);
        HostedWindow = Window;
        WindowContent = Content;
        OriginalOwner = Owner;
        OriginalPlaceholder = Placeholder;
        HostedStatusBar = StatusBar;
        return true;
    }
    return false;
}

void FAssistStatusBarHost::Restore() {
    const TSharedPtr<SWindow> Window = HostedWindow.Pin();
    const TSharedPtr<SVerticalBox> Content = WindowContent.Pin();
    const TSharedPtr<SVerticalBox> Owner = OriginalOwner.Pin();
    const TSharedPtr<SWidget> Placeholder = OriginalPlaceholder.Pin();
    const TSharedPtr<SWidget> Bar = HostedStatusBar.Pin();
    if (Content && Bar && Content->GetSlot(1).GetWidget() == Bar) {
        Content->GetSlot(1).AttachWidget(SNullWidget::NullWidget);
    }
    if (Owner && Placeholder && Bar) {
        for (int32 Index = 0; Index < Owner->NumSlots(); ++Index) {
            SVerticalBox::FSlot& Slot = Owner->GetSlot(Index);
            if (Slot.GetWidget() == Placeholder) {
                Slot.AttachWidget(Bar.ToSharedRef());
                Slot.SetPadding(OriginalPadding);
                break;
            }
        }
    }
    // UE may already have replaced the window content while reloading a layout.
    // Never replace newer content with widgets belonging to that old layout.
    if (Window && Content && Window->GetContent() == Content) {
        const TSharedRef<SWidget> DockingContent = Content->GetSlot(0).GetWidget();
        Content->GetSlot(0).AttachWidget(SNullWidget::NullWidget);
        Window->SetContent(DockingContent);
    }
    HostedWindow.Reset();
    WindowContent.Reset();
    OriginalOwner.Reset();
    OriginalPlaceholder.Reset();
    HostedStatusBar.Reset();
    HostedEditor.Reset();
}

void FAssistStatusBarHost::Update(const TSharedPtr<SDockTab>& Editor) {
    const TSharedRef<FGlobalTabmanager> Global = FGlobalTabmanager::Get();
    const TSharedPtr<SWindow> Window = Global->GetRootWindow();
    const TSharedPtr<SDockTab> LeftHost = Global->FindExistingLiveTab(AssistLayout::LeftToolsTabId);
    const TSharedPtr<SDockTab> Level = Global->FindExistingLiveTab(FTabId(TEXT("LevelEditor")));
    if (!Window || !LeftHost || !Level || LeftHost->GetTabManagerPtr() != Global ||
        LeftHost->GetParentWindow() != Window || Level->GetParentWindow() != Window ||
        LeftHost->GetDockArea() != Level->GetDockArea() || !Global->FindExistingLiveTab(AssistLayout::BottomToolsTabId)) {
        Restore();
        return;
    }
    const TSharedPtr<SDockingTabStack> LeftStack = LeftHost->GetParentDockTabStack();
    const TSharedPtr<SDockingTabStack> EditorStack = Editor ? Editor->GetParentDockTabStack() : nullptr;
    if (!LeftStack || !EditorStack || LeftStack == EditorStack) {
        Restore();
        return;
    }
    if (!Editor || Editor->GetParentWindow() != Window || !Global->GetTabManagerForMajorTab(Editor)) {
        Restore();
        return;
    }
    if (HostedEditor.Pin() == Editor && IsInstalled(Window.ToSharedRef()) &&
        IsInside(OriginalOwner.Pin(), Editor->GetContent())) {
        return;
    }
    Restore();
    if (const TSharedPtr<SWidget> Bar = FindStatusBar(Editor->GetContent())) {
        if (Attach(Window.ToSharedRef(), Bar.ToSharedRef())) {
            HostedEditor = Editor;
        }
    }
}
