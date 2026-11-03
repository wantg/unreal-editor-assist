#include "Assist.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AssistEditorContext.h"
#include "Editor.h"
#include "UnrealEdGlobals.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "ToolMenus.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Subsystems/AssetEditorSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistGenericMenuStartupTest, "Assist.Menus.GenericEditorStartupBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistGenericMenuStartupTest::RunTest(const FString& Parameters) {
    // Inspect the real post-startup binding, without re-running registration.
    // This fails if ToolMenus ran before GEditor existed and the hook was skipped.
    FAssistModule* Module = FModuleManager::GetModulePtr<FAssistModule>(TEXT("Assist"));
    if (!TestNotNull(TEXT("Assist is loaded"), Module)) {
        return false;
    }
    UAssetEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
    if (!TestNotNull(TEXT("The asset editor subsystem is initialized"), Subsystem)) {
        return false;
    }
    TestTrue(TEXT("Asset editor menus are hooked after startup, even when ToolMenus initialized before GEditor"),
        Subsystem->OnEditorOpeningPreWidgets().IsBoundToObject(Module));
    return true;
}

namespace {
class FAssistMenuProbeToolkit : public FAssetEditorToolkit {
   public:
    explicit FAssistMenuProbeToolkit(FName InName) : Name(InName) {}
    virtual FName GetToolkitFName() const override { return Name; }
    virtual FText GetBaseToolkitName() const override { return FText::FromName(Name); }
    virtual FText GetToolkitName() const override { return GetBaseToolkitName(); }
    virtual FString GetWorldCentricTabPrefix() const override { return TEXT("AssistTest"); }
    virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor::White; }
    virtual const TArray<UObject*>* GetObjectsCurrentlyBeingEdited() const override { return &Objects; }
    TArray<UObject*> Objects;
   private:
    FName Name;
};

TSharedPtr<FAssistMenuProbeToolkit> MakeMenuProbe(FName Name) {
    return MakeShareable(new FAssistMenuProbeToolkit(Name), [](FAssistMenuProbeToolkit* Toolkit) {
        // This probe never calls InitAssetEditor or registers with the subsystem.
        // Suppress the base destructor's NotifyEditorClosed for this synchronous
        // deletion only, without touching the user's live editors or settings.
        TGuardValue<UEditorEngine*> NoEditor(GEditor, nullptr);
        delete Toolkit;
    });
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistAssetMenuRegressionTest, "Assist.Menus.ToolkitContextAndOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistAssetMenuRegressionTest::RunTest(const FString& Parameters) {
    const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TStrongObjectPtr<UPackage> PackageA(CreatePackage(*(TEXT("/Temp/AssistMenuA_") + Suffix)));
    TStrongObjectPtr<UPackage> PackageB(CreatePackage(*(TEXT("/Temp/AssistMenuB_") + Suffix)));
    TStrongObjectPtr<UObject> AssetA(NewObject<UObject>(PackageA.Get()));
    TStrongObjectPtr<UObject> AssetA2(NewObject<UObject>(PackageA.Get()));
    TStrongObjectPtr<UObject> AssetB(NewObject<UObject>(PackageB.Get()));
    TSharedPtr<FAssistMenuProbeToolkit> ToolkitA = MakeMenuProbe(FName(*(TEXT("AssistTest Behavior Tree_") + Suffix)));
    TSharedPtr<FAssistMenuProbeToolkit> ToolkitB = MakeMenuProbe(FName(*(TEXT("AssistTest Niagara_") + Suffix)));
    ToolkitA->Objects = {AssetA.Get(), AssetA2.Get(), nullptr, AssetB.Get()};
    ToolkitB->Objects = {AssetB.Get()};
    TestFalse(TEXT("Specialized toolkits do not need IsSimpleAssetEditor"), ToolkitA->IsSimpleAssetEditor());
    TestFalse(TEXT("Reload target selection needs no tab manager or native window"), ToolkitA->GetAssociatedTabManager().IsValid());

    TStrongObjectPtr<UAssetEditorToolkitMenuContext> MenuObjectA(NewObject<UAssetEditorToolkitMenuContext>());
    TStrongObjectPtr<UAssetEditorToolkitMenuContext> MenuObjectB(NewObject<UAssetEditorToolkitMenuContext>());
    MenuObjectA->Toolkit = ToolkitA;
    MenuObjectB->Toolkit = ToolkitB;
    FToolMenuContext ContextA;
    FToolMenuContext ContextB;
    ContextA.AddObject(MenuObjectA.Get());
    ContextB.AddObject(MenuObjectB.Get());
    const TArray<UPackage*> Packages = AssistEditorContext::GetReloadPackages(ContextA, false);
    TestEqual(TEXT("All toolkit packages are collected once"), Packages.Num(), 2);
    TestTrue(TEXT("The first toolkit includes both of its asset packages"), Packages.Contains(PackageA.Get()) && Packages.Contains(PackageB.Get()));
    const TArray<UPackage*> OtherPackages = AssistEditorContext::GetReloadPackages(ContextB, false);
    TestTrue(TEXT("A second editor reloads only its own package"), OtherPackages.Num() == 1 && OtherPackages[0] == PackageB.Get());
    TestTrue(TEXT("Missing asset context cannot guess another editor or reload the level"),
        AssistEditorContext::GetReloadPackages(FToolMenuContext(), false, GEditor ? GEditor->GetEditorWorldContext().World() : nullptr).IsEmpty());
    if (UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr) {
        const TArray<UPackage*> LevelPackages = AssistEditorContext::GetReloadPackages(ContextA, true, World);
        TestTrue(TEXT("Only an explicit level command targets the editor world"), LevelPackages.Num() == 1 && LevelPackages[0] == World->GetPackage());
    }

    TestTrue(TEXT("A level command without an editor world has no targets"),
        AssistEditorContext::GetReloadPackages(ContextA, true).IsEmpty());
    ToolkitB->Objects.Reset();
    TestTrue(TEXT("An empty toolkit cannot guess a reload target"), AssistEditorContext::GetReloadPackages(ContextB, false).IsEmpty());

    // Use unique test menu names; never replace or unregister real editor menus.
    FAssistModule Module;
    UToolMenus* Menus = UToolMenus::Get();
    const FName MainMenu(*(TEXT("AssetEditor.") + ToolkitA->GetToolkitFName().ToString() + TEXT(".MainMenu")));
    const FName AssistMenu(*(MainMenu.ToString() + TEXT(".AssistMenu")));
    Menus->RegisterMenu(MainMenu, NAME_None, EMultiBoxType::MenuBar);
    ON_SCOPE_EXIT {
        Menus->UnregisterOwner(&Module);
        Menus->RemoveMenu(AssistMenu);
        Menus->RemoveMenu(MainMenu);
    };
    Module.RegisterToolkitMenu(ToolkitA, ToolkitB.Get());
    TestTrue(TEXT("A mismatched toolkit/editor pair is ignored"), Module.RegisteredEditorMenus.IsEmpty());
    Module.RegisterToolkitMenu(ToolkitA, ToolkitA.Get());
    Module.RegisterToolkitMenu(ToolkitA, ToolkitA.Get());
    TestEqual(TEXT("Opening the same toolkit does not duplicate registration"), Module.RegisteredEditorMenus.Num(), 1);
    UToolMenu* Generated = Menus->GenerateMenu(MainMenu, ContextA);
    const FToolMenuSection* MenuSection = Generated ? Generated->FindSection(NAME_None) : nullptr;
    const FToolMenuEntry* Entry = MenuSection ? MenuSection->FindEntry(TEXT("AssistMenu")) : nullptr;
    if (TestNotNull(TEXT("A specialized toolkit gets the Assist menu under its exact native name"), Entry)) {
        TestTrue(TEXT("The top-level entry has the module's owner"), Entry->Owner == FToolMenuOwner(&Module));
    }
    UToolMenu* Registered = Menus->FindMenu(AssistMenu);
    const FToolMenuSection* AssetSection = Registered ? Registered->FindSection(TEXT("Asset")) : nullptr;
    const FToolMenuEntry* Reload = AssetSection ? AssetSection->FindEntry(TEXT("ReloadAsset")) : nullptr;
    if (TestNotNull(TEXT("The toolkit menu provides Reload"), Reload)) {
        // Test the module's availability predicate used by the menu. ToolMenus
        // keeps action storage/conversion private; never execute a real Reload.
        if (GEditor && !GEditor->PlayWorld && !GIsPlayInEditorWorld) {
            TestTrue(TEXT("A live toolkit menu enables Reload"), Module.CanReloadAsset(ContextA, false));
        }
        TestFalse(TEXT("Missing toolkit context disables Reload"), Module.CanReloadAsset(FToolMenuContext(), false));
        ToolkitA.Reset();
        TestTrue(TEXT("Expired asset context yields no reload targets"), AssistEditorContext::GetReloadPackages(ContextA, false).IsEmpty());
        TestFalse(TEXT("A menu left open after its toolkit closes cannot reload another asset"), Module.CanReloadAsset(ContextA, false));
    }
    Menus->UnregisterOwner(&Module);
    Generated = Menus->GenerateMenu(MainMenu, FToolMenuContext());
    TestTrue(TEXT("Module unload removes the top-level Assist entry"), Generated && !Generated->ContainsEntry(TEXT("AssistMenu")));
    return true;
}
#endif
