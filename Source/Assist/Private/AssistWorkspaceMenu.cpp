#include "AssistWorkspaceMenu.h"
#include "AssistLayout.h"
#include "AssistWindowMenuGuard.h"
#include "Interfaces/IMainFrameModule.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/SDockingTabStack.h"
#include "Framework/Docking/TabManager.h"
#include "Layout/WidgetPath.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

namespace {
bool HasWorkspaceTools() {
    const TSharedRef<FGlobalTabmanager> Global = FGlobalTabmanager::Get();
    const TSharedPtr<SWindow> Root = Global->GetRootWindow();
    for (FName Id : {AssistLayout::LeftToolsTabId, AssistLayout::BottomToolsTabId}) {
        const TSharedPtr<SDockTab> Host = Global->FindExistingLiveTab(Id);
        if (Host && Root && Host->GetParentWindow() == Root) {
            return true;
        }
    }
    return false;
}

// Minor tabs use their owning major editor's menu. Never borrow a menu from a
// separate editor window, even if its tab manager belongs to a main-window editor.
TSharedPtr<SDockTab> FindEditorForTab(const TSharedPtr<SDockTab>& Tab) {
    const TSharedRef<FGlobalTabmanager> Global = FGlobalTabmanager::Get();
    const TSharedPtr<SWindow> Root = Global->GetRootWindow();
    if (!Tab || !Root || Tab->GetParentWindow() != Root || AssistLayout::IsToolTab(Tab)) {
        return nullptr;
    }
    const TSharedPtr<FTabManager> Manager = Tab->GetTabManagerPtr();
    if (!Manager) {
        return nullptr;
    }
    const TSharedPtr<SDockTab> Editor = Manager == Global ? Tab : Global->GetMajorTabForTabManager(Manager.ToSharedRef());
    if (!Editor || AssistLayout::IsToolTab(Editor) || Editor->GetParentWindow() != Root || !Editor->GetParentDockTabStack() || !Editor->IsForeground()) {
        return nullptr;
    }
    const TSharedPtr<FTabManager> EditorManager = Global->GetTabManagerForMajorTab(Editor);
    return EditorManager && EditorManager->AllowsWindowMenuBar() ? Editor : nullptr;
}
}

void FAssistWorkspaceMenu::Register() {
    FSlateApplication& Slate = FSlateApplication::Get();
    FocusChangingHandle = Slate.OnFocusChanging().AddSP(AsShared(), &FAssistWorkspaceMenu::OnFocusChanging);
    TabForegroundedHandle = FGlobalTabmanager::Get()->OnTabForegrounded_Subscribe(
        FOnActiveTabChanged::FDelegate::CreateSP(AsShared(), &FAssistWorkspaceMenu::OnTabForegrounded));
    if (IMainFrameModule* MainFrame = FModuleManager::GetModulePtr<IMainFrameModule>(TEXT("MainFrame"))) {
        MainFrameCreatedHandle = MainFrame->OnMainFrameCreationFinished().AddSP(AsShared(), &FAssistWorkspaceMenu::OnMainFrameCreated);
    }
    // Poll the selected editor before drawing: native menu updates also happen
    // during editor construction/window activation, without focus notifications.
    PreTickHandle = Slate.OnPreTick().AddSP(AsShared(), &FAssistWorkspaceMenu::RefreshMenu);
}

void FAssistWorkspaceMenu::Unregister() {
    SuspendTitleBar();
    if (FSlateApplication::IsInitialized()) {
        FSlateApplication& Slate = FSlateApplication::Get();
        Slate.OnFocusChanging().Remove(FocusChangingHandle);
        Slate.OnPreTick().Remove(PreTickHandle);
    }
    if (IMainFrameModule* MainFrame = FModuleManager::GetModulePtr<IMainFrameModule>(TEXT("MainFrame"))) {
        MainFrame->OnMainFrameCreationFinished().Remove(MainFrameCreatedHandle);
    }
    FGlobalTabmanager::Get()->OnTabForegrounded_Unsubscribe(TabForegroundedHandle);
    FocusChangingHandle.Reset();
    TabForegroundedHandle.Reset();
    PreTickHandle.Reset();
    MainFrameCreatedHandle.Reset();
    LastEditorTab.Reset();
}

void FAssistWorkspaceMenu::ObserveTab(const TSharedPtr<SDockTab>& Tab) {
    if (const TSharedPtr<SDockTab> Editor = FindEditorForTab(Tab)) {
        LastEditorTab = Editor;
    }
}

void FAssistWorkspaceMenu::ObservePath(const FWidgetPath& Path) {
    // The tab header and its content are siblings in Slate. Find the innermost
    // docking stack as well as explicit tab headers in the focus path.
    for (int32 Index = Path.Widgets.Num() - 1; Index >= 0; --Index) {
        const TSharedRef<SWidget>& Widget = Path.Widgets[Index].Widget;
        if (Widget->GetType() == FName(TEXT("SDockTab"))) {
            ObserveTab(StaticCastSharedRef<SDockTab>(Widget));
            return;
        }
        if (Widget->GetType() == FName(TEXT("SDockingTabStack"))) {
            const TSlotlessChildren<SDockTab>& Tabs = StaticCastSharedRef<SDockingTabStack>(Widget)->GetTabs();
            for (int32 TabIndex = 0; TabIndex < Tabs.Num(); ++TabIndex) {
                const TSharedRef<SDockTab>& Tab = Tabs[TabIndex];
                if (Tab->IsForeground()) {
                    ObserveTab(Tab);
                    return;
                }
            }
        }
    }
}

void FAssistWorkspaceMenu::OnFocusChanging(const FFocusEvent& Event, const FWeakWidgetPath& OldPath,
    const TSharedPtr<SWidget>& OldWidget, const FWidgetPath& NewPath, const TSharedPtr<SWidget>& NewWidget) {
    ObservePath(NewPath);
}

void FAssistWorkspaceMenu::OnTabForegrounded(TSharedPtr<SDockTab> NewTab, TSharedPtr<SDockTab> OldTab) {
    ObserveTab(NewTab);
}

void FAssistWorkspaceMenu::SuspendTitleBar() {
    const TSharedPtr<SWindow> Window = GuardedWindow.Pin();
    if (Window && MenuGuard && Window->GetTitleBar() == MenuGuard) {
        Window->SetTitleBar(MenuGuard->GetOriginal());
    }
    MenuGuard.Reset();
    GuardedWindow.Reset();
    LastEditorTab.Reset();
}

float FAssistWorkspaceMenu::GetMenuInset() const {
    if (MenuGuard) {
        return MenuGuard->GetMenuInset();
    }
#if PLATFORM_MAC
    return 30.0f;
#else
    return 25.0f;
#endif
}

void FAssistWorkspaceMenu::OnMainFrameCreated(TSharedPtr<SWindow> Window, bool bStartupDialog) {
    // Release the old title-bar/menu tree before asset restoration can reuse
    // singleton spawners. Never overwrite a newer native title bar.
    SuspendTitleBar();
    RefreshMenu(0.0f);
}

TSharedPtr<SDockTab> FAssistWorkspaceMenu::GetCurrentEditor() const {
    const TSharedRef<FGlobalTabmanager> Global = FGlobalTabmanager::Get();
    const TSharedPtr<SWindow> Root = Global->GetRootWindow();
    if (!Root) {
        return nullptr;
    }
    TSharedPtr<SDockTab> Editor = FindEditorForTab(LastEditorTab.Pin());
    if (!Editor) {
        Editor = FindEditorForTab(Global->GetActiveTab());
    }
    if (!Editor) {
        const TSharedPtr<SDockTab> Level = Global->FindExistingLiveTab(FTabId(TEXT("LevelEditor")));
        if (!Level || !Level->GetParentDockTabStack()) {
            return nullptr;
        }
        const TSharedPtr<SDockTab> Selected = Level->GetParentDockTabStack()->GetSelectedTabFromWell();
        Editor = FindEditorForTab(Selected);
        // A general-purpose tab explicitly docked in the center keeps UE's
        // global menu. Tool-area tabs still cannot take over the editor menu.
        if (!Editor && Selected && !AssistLayout::IsToolTab(Selected) &&
            Selected->GetTabRole() == ETabRole::NomadTab && Selected->GetParentWindow() == Root) {
            Editor = Selected;
        }
    }
    return Editor;
}

void FAssistWorkspaceMenu::RefreshMenu(float DeltaTime) {
    const TSharedRef<FGlobalTabmanager> Global = FGlobalTabmanager::Get();
    const TSharedPtr<SWindow> Root = Global->GetRootWindow();
    if (!HasWorkspaceTools() || !Root || !Root->GetTitleBar()) {
        SuspendTitleBar();
        return;
    }
    if (GuardedWindow.Pin() != Root || !MenuGuard || Root->GetTitleBar() != MenuGuard) {
        SuspendTitleBar();
        MenuGuard = MakeShared<FAssistWindowMenuGuard>(Root->GetTitleBar().ToSharedRef());
        GuardedWindow = Root;
        Root->SetTitleBar(MenuGuard);
    }
    FSlateApplication& Slate = FSlateApplication::Get();
    // SWindow::Construct can reset IsActive while the native window stays active.
    // Use Slate's active window/focus paths instead of that reconstructed flag.
    if ((Slate.GetActiveTopLevelWindow() != Root && !Root->HasFocusedDescendants()) || Slate.GetActiveModalWindow()) {
        return;
    }
    const TSharedPtr<SDockTab> Editor = GetCurrentEditor();
    if (Editor) {
        if (FindEditorForTab(Editor)) {
            LastEditorTab = Editor;
        }
        MenuGuard->ApplyEditorMenu([&Global, &Editor] {
            Global->UpdateMainMenu(Editor.ToSharedRef(), true);
        });
    }
}
