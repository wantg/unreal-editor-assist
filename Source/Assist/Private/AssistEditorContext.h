#pragma once

#include "CoreMinimal.h"

class UPackage;
class UWorld;
struct FToolMenuContext;

namespace AssistEditorContext {
// Asset commands belong to their menu's toolkit. A missing/expired asset context
// must not fall back to another editor or to the level currently being edited.
TArray<UPackage*> GetReloadPackages(const FToolMenuContext& Context, bool bLevelEditor, UWorld* EditorWorld = nullptr);
}
