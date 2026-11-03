#pragma once

#include "Framework/Docking/TabManager.h"

class SDockTab;
class SWidget;

namespace AssistLayout {
extern const FName LeftToolsTabId;
extern const FName BottomToolsTabId;
extern const FName BootstrapTabId;
bool IsToolHost(FName TabId);
bool IsToolTab(const TSharedPtr<SDockTab>& Tab);

// These local tool areas accept cross-manager Nomad tabs. Never offer a target
// inside the dragged tab's own content (which would create a docking cycle).
bool CanAcceptToolTab(const TSharedPtr<SDockTab>& Tab, const TSharedPtr<SWidget>& TargetArea);

// The outer layout contains stable hosts; their independent native tab managers
// own tool contents. Resource editor layouts and their restricted panels stay local.
TSharedPtr<FTabManager::FLayout> MakeFlexibleLayout(const TSharedRef<FTabManager::FLayout>& Source, const TSet<FName>& ToolTabs);
TSharedRef<FTabManager::FLayout> CloseEmbeddedTools(const TSharedRef<FTabManager::FLayout>& Source, const TSet<FName>& ToolTabs);
TSharedRef<FTabManager::FLayout> MakeToolLayout(bool bLeft);
TSet<FName> GetOpenToolTabs(const TSharedRef<FTabManager::FLayout>& Source);

// A temporary local tab initializes UE's empty-area docking targets on restore.
// It is closed before the first draw and is never written to the saved layout.
TSharedRef<FTabManager::FLayout> PrepareToolLayout(const TSharedRef<FTabManager::FLayout>& Source);
TSharedRef<FTabManager::FLayout> CleanToolLayout(const TSharedRef<FTabManager::FLayout>& Source);
}
