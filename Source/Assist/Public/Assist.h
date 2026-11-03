#pragma once

#include "Modules/ModuleManager.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "AssistConfig.h"

class IAssetEditorInstance;
class IToolkit;
class UAssetEditorSubsystem;
class UObject;
class SWidget;
class FAssistWorkspaceMenu;
class SDockTab;
class FSpawnTabArgs;
class FAssistStatusBarHost;
struct FToolMenuContext;
struct FToolMenuSection;
struct FPointerEvent;

class FAssistModule : public IModuleInterface {
   public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

   private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FAssistAssetMenuRegressionTest;
#endif
    void init();
    void RegisterMenus();
    void RegisterAssetEditorMenus();
    void RegisterEditorMenu(const FString& MenuModule);
    void RegisterOpenedAssetEditorMenu(const TArray<UObject*>& Assets, IAssetEditorInstance* Editor);
    void RegisterToolkitMenu(const TSharedPtr<IToolkit>& Toolkit, IAssetEditorInstance* Editor);
    FAssistConfig AssistConfig;

    void ReloadProject();
    void ReloadAsset(const FToolMenuContext& Context, bool bLevelEditor);
    bool CanReloadAsset(const FToolMenuContext& Context, bool bLevelEditor) const;
    void SetFlexibleLayout();
    TSharedRef<SDockTab> SpawnToolHost(const FSpawnTabArgs& Args, bool bLeft);
    void AddDefaultEditorLayoutShortcut(FToolMenuSection& Section);
    void ReloadEditorLayout();
    void SetCurrentLanguage(const FString Language);

    void FindWidget(TSharedRef<SWidget> Parent, FString TypeString, TSharedPtr<SWidget>& Result);

    void TrackPressedTab(const FPointerEvent& Event);
    void UpdateFlexibleLayout(float DeltaTime);

    TSet<FName> RegisteredEditorMenus;
    TWeakObjectPtr<UAssetEditorSubsystem> MenuAssetEditorSubsystem;
    FDelegateHandle EditorOpeningHandle;
    FDelegateHandle AssetEditorMenusInitHandle;
    FDelegateHandle FlexibleLayoutTickHandle;
    FDelegateHandle TabMouseDownHandle;
    TWeakPtr<SDockTab> PressedTab;
    TSharedPtr<FAssistWorkspaceMenu> WorkspaceMenu;
    TSharedPtr<FAssistStatusBarHost> StatusBarHost;
};
