#include "AssistEditorContext.h"

#include "Engine/World.h"
#include "ToolMenuContext.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "UObject/Package.h"

TArray<UPackage*> AssistEditorContext::GetReloadPackages(const FToolMenuContext& Context, bool bLevelEditor, UWorld* EditorWorld) {
    TArray<UPackage*> Packages;
    if (bLevelEditor) {
        if (EditorWorld) {
            Packages.Add(EditorWorld->GetPackage());
        }
        return Packages;
    }
    const UAssetEditorToolkitMenuContext* MenuContext = Context.FindContext<UAssetEditorToolkitMenuContext>();
    const TSharedPtr<FAssetEditorToolkit> Toolkit = MenuContext ? MenuContext->Toolkit.Pin() : nullptr;
    if (!Toolkit) {
        return Packages;
    }
    if (const TArray<UObject*>* Objects = Toolkit->GetObjectsCurrentlyBeingEdited()) {
        for (UObject* Object : *Objects) {
            if (Object) {
                Packages.AddUnique(Object->GetPackage());
            }
        }
    }
    return Packages;
}
