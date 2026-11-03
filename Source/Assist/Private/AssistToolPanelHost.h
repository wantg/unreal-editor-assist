#pragma once

#include "Framework/Docking/TabManager.h"
#include "Widgets/SCompoundWidget.h"

class SDockTab;
class SDockingNode;

// A local native TabManager accepts NomadTab tools; editor-local PanelTab and
// MajorTab drags are rejected by UE's own docking rules.
class SAssistToolPanelHost : public SCompoundWidget {
   public:
    SLATE_BEGIN_ARGS(SAssistToolPanelHost) {}
    SLATE_END_ARGS()
    void Construct(const FArguments& Args, const TSharedRef<SDockTab>& Owner, bool bLeft);
    bool HasOpenTools() const;
    bool CanAcceptTab(const TSharedPtr<SDockTab>& Tab) const;
    void RequestSave();
    void Shutdown();

   private:
    void Persist(const TSharedRef<FTabManager::FLayout>& Layout);
    TSharedPtr<FTabManager> Manager;
    TWeakPtr<SDockingNode> DockingArea;
    TArray<TWeakPtr<SWidget>> Sidebars;
};
