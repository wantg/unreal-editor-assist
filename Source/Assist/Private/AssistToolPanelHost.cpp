#include "AssistToolPanelHost.h"

#include "AssistLayout.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/Docking/SDockingNode.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Docking/SDockTab.h"

namespace {
void FindSidebars(const TSharedRef<SWidget>& Widget, TArray<TWeakPtr<SWidget>>& Result) {
    if (Widget->GetType() == FName(TEXT("STabSidebar"))) {
        Result.Add(Widget);
        return;
    }
    // Sidebars belong to the area itself; don't walk through tool contents.
    if (Widget->GetType() == FName(TEXT("SDockingTabStack"))) {
        return;
    }
    FChildren* const Children = Widget->GetChildren();
    for (int32 Index = 0; Index < Children->Num(); ++Index) {
        FindSidebars(Children->GetChildAt(Index), Result);
    }
}
}

void SAssistToolPanelHost::Construct(const FArguments& Args, const TSharedRef<SDockTab>& Owner, bool bLeft) {
    Manager = FGlobalTabmanager::Get()->NewTabManager(Owner);
    Manager->RegisterTabSpawner(AssistLayout::BootstrapTabId, FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&) {
        return SNew(SDockTab).TabRole(ETabRole::PanelTab).Label(FText::GetEmpty());
    })).SetMenuType(ETabSpawnerMenuType::Hidden);
    Manager->SetOnPersistLayout(FTabManager::FOnPersistLayout::CreateSP(this, &SAssistToolPanelHost::Persist));
    const TSharedRef<FTabManager::FLayout> Layout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, AssistLayout::MakeToolLayout(bLeft));
    const TSharedPtr<SWidget> Content = Manager->RestoreFrom(AssistLayout::PrepareToolLayout(Layout), FGlobalTabmanager::Get()->GetRootWindow(), false);
    if (!ensure(Content)) {
        return;
    }
    const TSharedRef<SDockingNode> Area = StaticCastSharedRef<SDockingNode>(Content.ToSharedRef());
    DockingArea = Area;
    FindSidebars(Area, Sidebars);
    // Use the native area's empty-state UI while showing a drag target; don't
    // overlay a second message on UE's own placeholder.
    ChildSlot[Content.ToSharedRef()];
    // Closing this temporary local tab invokes native CleanUp and enables the
    // center drop target even when all saved tools were closed or dragged out.
    if (const TSharedPtr<SDockTab> Bootstrap = Manager->FindExistingLiveTab(AssistLayout::BootstrapTabId)) {
        Bootstrap->RequestCloseTab();
    }
}

bool SAssistToolPanelHost::HasOpenTools() const {
    const TSharedPtr<SDockingNode> Area = DockingArea.Pin();
    if (Area && Area->GetNumTabs() > 0) {
        return true;
    }
    // A tool moved to a native sidebar is still present, even when its drawer
    // is closed. Do not collapse the region and hide its reopen button.
    for (const TWeakPtr<SWidget>& Sidebar : Sidebars) {
        const TSharedPtr<SWidget> Widget = Sidebar.Pin();
        if (Widget && Widget->GetVisibility().IsVisible()) {
            return true;
        }
    }
    return false;
}

bool SAssistToolPanelHost::CanAcceptTab(const TSharedPtr<SDockTab>& Tab) const {
    return Manager && AssistLayout::CanAcceptToolTab(Tab, DockingArea.Pin());
}

void SAssistToolPanelHost::Persist(const TSharedRef<FTabManager::FLayout>& Layout) {
    if (FGlobalTabmanager::Get()->CanSavePersistentLayouts()) {
        FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, AssistLayout::CleanToolLayout(Layout));
    }
}

void SAssistToolPanelHost::RequestSave() {
    if (Manager && FGlobalTabmanager::Get()->CanSavePersistentLayouts()) {
        // Gathering the outer layout also persists this host tab. Never gather
        // the local layout synchronously here: a native docking drag temporarily
        // registers its preview as a second primary area in the source manager.
        // UE suppresses deferred saves during the drag and schedules a final
        // save after relocation, once the preview has been removed.
        Manager->RequestSavePersistentLayout();
    }
}

void SAssistToolPanelHost::Shutdown() {
    if (Manager) {
        if (FGlobalTabmanager::Get()->CanSavePersistentLayouts()
            && (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsDragDropping())) {
            Manager->SavePersistentLayout();
        }
        Manager->ClearPendingLayoutSave();
        Manager->SetOnPersistLayout(FTabManager::FOnPersistLayout());
        Manager->CloseAllAreas();
    }
    ChildSlot[SNullWidget::NullWidget];
    DockingArea.Reset();
    Sidebars.Empty();
    Manager.Reset();
}
