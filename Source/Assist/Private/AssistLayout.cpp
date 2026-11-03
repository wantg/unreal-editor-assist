#include "AssistLayout.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Widgets/Docking/SDockTab.h"

namespace AssistLayout {
const FName LeftToolsTabId(TEXT("Assist.LeftTools"));
const FName BottomToolsTabId(TEXT("Assist.BottomTools"));
const FName BootstrapTabId(TEXT("Assist.ToolAreaBootstrap"));

bool IsToolHost(FName TabId) {
    return TabId == LeftToolsTabId || TabId == BottomToolsTabId;
}

bool IsToolTab(const TSharedPtr<SDockTab>& Tab) {
    if (!Tab) {
        return false;
    }
    if (IsToolHost(Tab->GetLayoutIdentifier().TabType)) {
        return true;
    }
    const TSharedPtr<FTabManager> Manager = Tab->GetTabManagerPtr();
    const TSharedPtr<SDockTab> Owner = Manager ? Manager->GetOwnerTab() : nullptr;
    return Owner && IsToolHost(Owner->GetLayoutIdentifier().TabType);
}

bool CanAcceptToolTab(const TSharedPtr<SDockTab>& Tab, const TSharedPtr<SWidget>& TargetArea) {
    if (!Tab || !TargetArea || Tab->GetTabRole() != ETabRole::NomadTab) {
        return false;
    }
    // Mirror the native docking check that prevents dropping a parent tab into
    // one of its own embedded tool areas. Evaluate each region independently.
    const TSharedRef<SWidget> Content = Tab->GetContent();
    for (TSharedPtr<SWidget> Parent = TargetArea; Parent; Parent = Parent->GetParentWidget()) {
        if (Parent == Content) {
            return false;
        }
    }
    return true;
}

namespace {
bool Matches(const FString& Id, const TSet<FName>& Ids) {
    return Ids.Contains(FName(*Id));
}

void RemoveDrawers(const TSharedPtr<FJsonObject>& Node, const TSet<FName>& Ids) {
    FString Active;
    if (Node->TryGetStringField(TEXT("ActivePanelDrawerTab"), Active) && Matches(Active, Ids)) {
        Node->RemoveField(TEXT("ActivePanelDrawerTab"));
        Node->RemoveField(TEXT("MainContentCoefficient"));
        Node->RemoveField(TEXT("PanelDrawerCoefficient"));
    }
    const TArray<TSharedPtr<FJsonValue>>* Inactive = nullptr;
    if (Node->TryGetArrayField(TEXT("InactivePanelDrawerTabs"), Inactive)) {
        TArray<TSharedPtr<FJsonValue>> Remaining = *Inactive;
        Remaining.RemoveAll([&Ids](const TSharedPtr<FJsonValue>& Value) {
            return Matches(Value->AsObject()->GetStringField(TEXT("TabId")), Ids);
        });
        Node->SetArrayField(TEXT("InactivePanelDrawerTabs"), Remaining);
    }
}

// Edit a JSON snapshot, preserving the input model. Empty areas remain valid
// primary drop targets when requested; redundant splitters are flattened.
TSharedPtr<FJsonObject> RemoveTabs(const TSharedPtr<FJsonObject>& Node, const TSet<FName>& Ids, bool bKeepArea = false) {
    RemoveDrawers(Node, Ids);
    if (Node->GetStringField(TEXT("Type")) == TEXT("Stack")) {
        TArray<TSharedPtr<FJsonValue>> Tabs = Node->GetArrayField(TEXT("Tabs"));
        Tabs.RemoveAll([&Ids](const TSharedPtr<FJsonValue>& Tab) {
            return Matches(Tab->AsObject()->GetStringField(TEXT("TabId")), Ids);
        });
        if (Tabs.IsEmpty()) {
            return nullptr;
        }
        Node->SetArrayField(TEXT("Tabs"), Tabs);
        FString Foreground;
        if (Node->TryGetStringField(TEXT("ForegroundTab"), Foreground) && Matches(Foreground, Ids)) {
            Node->RemoveField(TEXT("ForegroundTab"));
        }
        return Node;
    }
    TArray<TSharedPtr<FJsonValue>> Children;
    for (const TSharedPtr<FJsonValue>& Child : Node->GetArrayField(TEXT("nodes"))) {
        if (TSharedPtr<FJsonObject> Remaining = RemoveTabs(Child->AsObject(), Ids)) {
            Children.Add(MakeShared<FJsonValueObject>(Remaining));
        }
    }
    if (Children.IsEmpty() && !bKeepArea) {
        return nullptr;
    }
    if (Children.Num() == 1 && Node->GetStringField(TEXT("Type")) == TEXT("Splitter")) {
        const TSharedPtr<FJsonObject> Only = Children[0]->AsObject();
        Only->SetNumberField(TEXT("SizeCoefficient"), Node->GetNumberField(TEXT("SizeCoefficient")));
        return Only;
    }
    Node->SetArrayField(TEXT("nodes"), Children);
    return Node;
}

void CloseTabs(const TSharedPtr<FJsonObject>& Node, const TSet<FName>& Ids) {
    RemoveDrawers(Node, Ids);
    const TArray<TSharedPtr<FJsonValue>>* Tabs = nullptr;
    if (Node->TryGetArrayField(TEXT("Tabs"), Tabs)) {
        for (const TSharedPtr<FJsonValue>& Value : *Tabs) {
            const TSharedPtr<FJsonObject> Tab = Value->AsObject();
            if (Matches(Tab->GetStringField(TEXT("TabId")), Ids)) {
                Tab->SetStringField(TEXT("TabState"), TEXT("ClosedTab"));
                for (const TCHAR* Key : {TEXT("SidebarLocation"), TEXT("SidebarCoeff"), TEXT("SidebarPinned")}) {
                    Tab->RemoveField(Key);
                }
            }
        }
    }
    FString Foreground;
    if (Node->TryGetStringField(TEXT("ForegroundTab"), Foreground) && Matches(Foreground, Ids)) {
        Node->RemoveField(TEXT("ForegroundTab"));
    }
    const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
    if (Node->TryGetArrayField(TEXT("nodes"), Children) || Node->TryGetArrayField(TEXT("Areas"), Children)) {
        for (const TSharedPtr<FJsonValue>& Child : *Children) {
            CloseTabs(Child->AsObject(), Ids);
        }
    }
}

bool Contains(const TSharedPtr<FJsonObject>& Node, FName Id) {
    const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
    if (Node->TryGetArrayField(TEXT("Tabs"), Children)) {
        for (const TSharedPtr<FJsonValue>& Tab : *Children) {
            if (FName(*Tab->AsObject()->GetStringField(TEXT("TabId"))) == Id) {
                return true;
            }
        }
    } else if (Node->TryGetArrayField(TEXT("nodes"), Children)) {
        for (const TSharedPtr<FJsonValue>& Child : *Children) {
            if (Contains(Child->AsObject(), Id)) {
                return true;
            }
        }
    }
    return false;
}

TSharedRef<FJsonObject> Stack(FName Id, double Coefficient) {
    const TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
    Node->SetStringField(TEXT("Type"), TEXT("Stack"));
    Node->SetNumberField(TEXT("SizeCoefficient"), Coefficient);
    Node->SetBoolField(TEXT("HideTabWell"), false);
    Node->SetStringField(TEXT("ForegroundTab"), Id.ToString());
    const TSharedRef<FJsonObject> Tab = MakeShared<FJsonObject>();
    Tab->SetStringField(TEXT("TabId"), Id.ToString());
    Tab->SetStringField(TEXT("TabState"), TEXT("OpenedTab"));
    Node->SetArrayField(TEXT("Tabs"), {MakeShared<FJsonValueObject>(Tab)});
    return Node;
}
}

TSharedPtr<FTabManager::FLayout> MakeFlexibleLayout(const TSharedRef<FTabManager::FLayout>& Source, const TSet<FName>& ToolTabs) {
    const TSharedRef<FJsonObject> Json = Source->ToJson();
    const TArray<TSharedPtr<FJsonValue>> Areas = Json->GetArrayField(TEXT("Areas"));
    const int32 PrimaryIndex = static_cast<int32>(Json->GetNumberField(TEXT("PrimaryAreaIndex")));
    if (!Areas.IsValidIndex(PrimaryIndex)) {
        return nullptr;
    }
    TSet<FName> RemoveIds = ToolTabs;
    RemoveIds.Add(LeftToolsTabId);
    RemoveIds.Add(BottomToolsTabId);
    const TSharedPtr<FJsonObject> Primary = RemoveTabs(Areas[PrimaryIndex]->AsObject(), RemoveIds);
    if (!Primary || !Contains(Primary, TEXT("LevelEditor"))) {
        return nullptr;
    }
    const TArray<TSharedPtr<FJsonValue>> Nodes = Primary->GetArrayField(TEXT("nodes"));
    TSharedPtr<FJsonObject> Editor;
    if (Nodes.Num() == 1) {
        Editor = Nodes[0]->AsObject();
    } else {
        Editor = MakeShared<FJsonObject>();
        Editor->SetStringField(TEXT("Type"), TEXT("Splitter"));
        Editor->SetStringField(TEXT("Orientation"), Primary->GetStringField(TEXT("Orientation")));
        Editor->SetArrayField(TEXT("nodes"), Nodes);
    }
    Editor->SetNumberField(TEXT("SizeCoefficient"), 0.75);
    const TSharedRef<FJsonObject> Right = MakeShared<FJsonObject>();
    Right->SetStringField(TEXT("Type"), TEXT("Splitter"));
    Right->SetNumberField(TEXT("SizeCoefficient"), 0.8);
    Right->SetStringField(TEXT("Orientation"), TEXT("Orient_Vertical"));
    Right->SetArrayField(TEXT("nodes"), {MakeShared<FJsonValueObject>(Editor), MakeShared<FJsonValueObject>(Stack(BottomToolsTabId, 0.25))});
    Primary->SetStringField(TEXT("Orientation"), TEXT("Orient_Horizontal"));
    Primary->SetArrayField(TEXT("nodes"), {MakeShared<FJsonValueObject>(Stack(LeftToolsTabId, 0.2)), MakeShared<FJsonValueObject>(Right)});

    TArray<TSharedPtr<FJsonValue>> Remaining;
    for (int32 Index = 0; Index < Areas.Num(); ++Index) {
        const TSharedPtr<FJsonObject> Area = Index == PrimaryIndex ? Primary : RemoveTabs(Areas[Index]->AsObject(), RemoveIds);
        if (Area) {
            if (Index == PrimaryIndex) {
                Json->SetNumberField(TEXT("PrimaryAreaIndex"), Remaining.Num());
            }
            Remaining.Add(MakeShared<FJsonValueObject>(Area));
        }
    }
    Json->SetArrayField(TEXT("Areas"), Remaining);
    return FTabManager::FLayout::NewFromJson(Json);
}

TSharedRef<FTabManager::FLayout> CloseEmbeddedTools(const TSharedRef<FTabManager::FLayout>& Source, const TSet<FName>& ToolTabs) {
    const TSharedRef<FJsonObject> Json = Source->ToJson();
    CloseTabs(Json, ToolTabs);
    return FTabManager::FLayout::NewFromJson(Json).ToSharedRef();
}

TSharedRef<FTabManager::FLayout> MakeToolLayout(bool bLeft) {
    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(bLeft ? TEXT("Assist.LeftTools_v1") : TEXT("Assist.BottomTools_v1"))
        ->AddArea(FTabManager::NewPrimaryArea()->Split(FTabManager::NewStack()
            ->AddTab(bLeft ? FName(TEXT("ContentBrowserTab1")) : FName(TEXT("OutputLog")), ETabState::OpenedTab)));
    const TSharedRef<FJsonObject> Json = Layout->ToJson();
    Json->SetNumberField(TEXT("PrimaryAreaIndex"), 0);
    return FTabManager::FLayout::NewFromJson(Json).ToSharedRef();
}

TSet<FName> GetOpenToolTabs(const TSharedRef<FTabManager::FLayout>& Source) {
    TSet<FName> Result;
    TFunction<void(const TSharedPtr<FJsonObject>&)> Visit = [&](const TSharedPtr<FJsonObject>& Node) {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (Node->TryGetArrayField(TEXT("Tabs"), Values)) {
            for (const TSharedPtr<FJsonValue>& Value : *Values) {
                const TSharedPtr<FJsonObject> Tab = Value->AsObject();
                const FString State = Tab->GetStringField(TEXT("TabState"));
                const FName Id(*Tab->GetStringField(TEXT("TabId")));
                if (Id != BootstrapTabId && (State == TEXT("OpenedTab") || State == TEXT("SidebarTab") || State == TEXT("InvalidTab"))) {
                    Result.Add(Id);
                }
            }
        }
        FString Drawer;
        if (Node->TryGetStringField(TEXT("ActivePanelDrawerTab"), Drawer) && FName(*Drawer) != BootstrapTabId) {
            Result.Add(FName(*Drawer));
        }
        if (Node->TryGetArrayField(TEXT("nodes"), Values) || Node->TryGetArrayField(TEXT("Areas"), Values)) {
            for (const TSharedPtr<FJsonValue>& Value : *Values) {
                Visit(Value->AsObject());
            }
        }
    };
    Visit(Source->ToJson());
    return Result;
}

TSharedRef<FTabManager::FLayout> CleanToolLayout(const TSharedRef<FTabManager::FLayout>& Source) {
    const TSharedRef<FJsonObject> Json = Source->ToJson();
    const TArray<TSharedPtr<FJsonValue>> Areas = Json->GetArrayField(TEXT("Areas"));
    int32 PrimaryIndex = static_cast<int32>(Json->GetNumberField(TEXT("PrimaryAreaIndex")));
    // Older synchronous saves could include a native drag preview as another
    // NoWindow area, and mark that last area as primary. The permanent embedded
    // area is registered first. Keep it and discard any transient duplicates.
    for (int32 Index = 0; Index < Areas.Num(); ++Index) {
        if (Areas[Index]->AsObject()->GetStringField(TEXT("WindowPlacement")) == TEXT("Placement_NoWindow")) {
            PrimaryIndex = Index;
            break;
        }
    }
    TArray<TSharedPtr<FJsonValue>> Remaining;
    const TSet<FName> Ids{BootstrapTabId};
    for (int32 Index = 0; Index < Areas.Num(); ++Index) {
        if (Index != PrimaryIndex && Areas[Index]->AsObject()->GetStringField(TEXT("WindowPlacement")) == TEXT("Placement_NoWindow")) {
            continue;
        }
        if (const TSharedPtr<FJsonObject> Area = RemoveTabs(Areas[Index]->AsObject(), Ids, Index == PrimaryIndex)) {
            if (Index == PrimaryIndex) {
                Json->SetNumberField(TEXT("PrimaryAreaIndex"), Remaining.Num());
            }
            Remaining.Add(MakeShared<FJsonValueObject>(Area));
        }
    }
    if (!Areas.IsValidIndex(PrimaryIndex)) {
        const TSharedRef<FJsonObject> Area = MakeShared<FJsonObject>();
        Area->SetStringField(TEXT("Type"), TEXT("Area"));
        Area->SetNumberField(TEXT("SizeCoefficient"), 1);
        Area->SetStringField(TEXT("Orientation"), TEXT("Orient_Horizontal"));
        Area->SetStringField(TEXT("WindowPlacement"), TEXT("Placement_NoWindow"));
        Area->SetArrayField(TEXT("nodes"), {});
        Json->SetNumberField(TEXT("PrimaryAreaIndex"), Remaining.Num());
        Remaining.Add(MakeShared<FJsonValueObject>(Area));
    }
    Json->SetArrayField(TEXT("Areas"), Remaining);
    return FTabManager::FLayout::NewFromJson(Json).ToSharedRef();
}

TSharedRef<FTabManager::FLayout> PrepareToolLayout(const TSharedRef<FTabManager::FLayout>& Source) {
    const TSharedRef<FJsonObject> Json = CleanToolLayout(Source)->ToJson();
    const int32 Index = static_cast<int32>(Json->GetNumberField(TEXT("PrimaryAreaIndex")));
    const TSharedPtr<FJsonObject> Area = Json->GetArrayField(TEXT("Areas"))[Index]->AsObject();
    // Tool areas must never reshape the native main window or reserve its chrome.
    Area->SetStringField(TEXT("WindowPlacement"), TEXT("Placement_NoWindow"));
    TArray<TSharedPtr<FJsonValue>> Nodes = Area->GetArrayField(TEXT("nodes"));
    Nodes.Add(MakeShared<FJsonValueObject>(Stack(BootstrapTabId, 0.001)));
    Area->SetArrayField(TEXT("nodes"), Nodes);
    return FTabManager::FLayout::NewFromJson(Json).ToSharedRef();
}
}
