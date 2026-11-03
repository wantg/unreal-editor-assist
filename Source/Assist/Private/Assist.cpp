#include "Assist.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "Kismet/KismetInternationalizationLibrary.h"
#include "FileHelpers.h"
#include "UnrealEdGlobals.h"
#include "SAssetView.h"
#include "AssistStyle.h"
#include "Styling/AppStyle.h"
#include "AssistLayout.h"
#include "AssistEditorContext.h"
#include "AssistWorkspaceMenu.h"
#include "AssistWorkspaceChrome.h"
#include "AssistToolPanelHost.h"
#include "AssistStatusBarHost.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/DragAndDrop.h"
#include "InputCoreTypes.h"
#include "Layout/WidgetPath.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/SDockingTabStack.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "ToolMenus.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssist, Log, All);

static const FName AssistTabName("Assist");

#define LOCTEXT_NAMESPACE "FAssistModule"

namespace {
    bool CanChangeEditorLayout() {
        return GEditor && !GEditor->PlayWorld && !GIsPlayInEditorWorld && FSlateApplication::IsInitialized();
    }

    // Main-frame content reconstruction also reads UE's RootWindow settings.
    // Refresh them from the user's current placement so reconstructing the Slate
    // window cannot revert to an older size, position or maximized state.
    void SaveCurrentMainWindowPlacement() {
        const TSharedPtr<SWindow> Window = FGlobalTabmanager::Get()->GetRootWindow();
        if (!Window || FSlateApplication::Get().IsRenderingOffScreen()) {
            return;
        }

        // Match UE's main-frame placement persistence, including the restore size
        // of a maximized window. Subtract Slate borders before normalizing for DPI
        // to avoid growing the window on each layout change.
        FSlateRect Rect = Window->GetNonMaximizedRectInScreen();
        if (!Window->HasOSWindowBorder()) {
            const FMargin Border = Window->GetNonMaximizedWindowBorderSize();
            Rect.Right -= Border.Left + Border.Right;
            Rect.Bottom -= Border.Top + Border.Bottom;
        }
        const float DPIScale     = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(Rect.Left, Rect.Top);
        const FVector2D Position = FVector2D(Rect.Left, Rect.Top) / DPIScale;
        const FVector2D Size     = FVector2D(Rect.GetSize()) / DPIScale;
        GConfig->SetString(TEXT("RootWindow"), TEXT("ScreenPosition"), *Position.ToString(), GEditorPerProjectIni);
        GConfig->SetString(TEXT("RootWindow"), TEXT("WindowSize"), *Size.ToString(), GEditorPerProjectIni);
        GConfig->SetBool(TEXT("RootWindow"), TEXT("InitiallyMaximized"), Window->IsWindowMaximized(), GEditorPerProjectIni);
    }

    void SaveEmbeddedEditorLayoutsWithoutTools(const TSharedRef<FGlobalTabmanager>& GlobalTabManager, const TSet<FName>& ToolTabs) {
        TSet<const FTabManager*> SavedManagers;
        const auto SaveManager = [&SavedManagers, &GlobalTabManager, &ToolTabs](const TSharedPtr<FTabManager>& Manager) {
            if (Manager && Manager.Get() != &GlobalTabManager.Get() && !SavedManagers.Contains(Manager.Get())) {
                SavedManagers.Add(Manager.Get());
                FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni,
                    AssistLayout::CloseEmbeddedTools(Manager->PersistLayout(), ToolTabs));
            }
        };
        if (TSharedPtr<SDockTab> LevelTab = GlobalTabManager->FindExistingLiveTab(FTabId(TEXT("LevelEditor")))) {
            SaveManager(GlobalTabManager->GetTabManagerForMajorTab(LevelTab));
        }
        if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()) {
            for (UObject* Asset : AssetEditors->GetAllEditedAssets()) {
                if (IAssetEditorInstance* Editor = AssetEditors->FindEditorForAsset(Asset, false)) {
                    SaveManager(Editor->GetAssociatedTabManager());
                }
            }
        }
    }
} // namespace

void FAssistModule::StartupModule() {
    // This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

    init();
    FAssistStyle::Initialize();
    FAssistStyle::ReloadTextures();

    const TSharedRef<FGlobalTabmanager> Global = FGlobalTabmanager::Get();
    for (bool bLeft : {true, false}) {
        Global->RegisterTabSpawner(bLeft ? AssistLayout::LeftToolsTabId : AssistLayout::BottomToolsTabId,
            FOnSpawnTab::CreateRaw(this, &FAssistModule::SpawnToolHost, bLeft))
            .SetDisplayName(bLeft ? LOCTEXT("LeftTools", "Left Tools") : LOCTEXT("BottomTools", "Bottom Tools"))
            .SetMenuType(ETabSpawnerMenuType::Hidden);
    }

    // https://minifloppy.it/posts/2024/adding-custom-buttons-unreal-editor-toolbars-menus
    // Register a function to be called when menu system is initialized
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAssistModule::RegisterMenus));


}

void FAssistModule::ShutdownModule() {
    // This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
    // we call this function before unloading the module.

    if (FlexibleLayoutTickHandle.IsValid() && FSlateApplication::IsInitialized()) {
        FSlateApplication::Get().OnPreTick().Remove(FlexibleLayoutTickHandle);
    }
    FlexibleLayoutTickHandle.Reset();
    FCoreDelegates::GetOnPostEngineInit().Remove(AssetEditorMenusInitHandle);
    AssetEditorMenusInitHandle.Reset();
    if (UAssetEditorSubsystem* Subsystem = MenuAssetEditorSubsystem.Get()) {
        Subsystem->OnEditorOpeningPreWidgets().Remove(EditorOpeningHandle);
    }
    MenuAssetEditorSubsystem.Reset();
    EditorOpeningHandle.Reset();
    RegisteredEditorMenus.Reset();
    if (TabMouseDownHandle.IsValid() && FSlateApplication::IsInitialized()) {
        FSlateApplication::Get().OnApplicationMousePreInputButtonDownListener().Remove(TabMouseDownHandle);
    }
    TabMouseDownHandle.Reset();
    PressedTab.Reset();

    if (StatusBarHost) {
        StatusBarHost->Restore();
        StatusBarHost.Reset();
    }

    if (WorkspaceMenu) {
        WorkspaceMenu->Unregister();
        WorkspaceMenu.Reset();
    }

    // Release plugin-owned widgets and local managers before this module is
    // unloaded; leave no callbacks into the unloaded plugin on native tabs.
    for (FName Id : {AssistLayout::LeftToolsTabId, AssistLayout::BottomToolsTabId}) {
        if (const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(Id)) {
            const TSharedRef<SWidget> Content = Tab->GetContent();
            if (Content->GetType() == FName(TEXT("SAssistToolPanelHost"))) {
                StaticCastSharedRef<SAssistToolPanelHost>(Content)->Shutdown();
            }
            Tab->SetOnPersistVisualState(SDockTab::FOnPersistVisualState());
            Tab->SetContent(SNullWidget::NullWidget);
        }
    }

    FGlobalTabmanager::Get()->UnregisterTabSpawner(AssistLayout::LeftToolsTabId);
    FGlobalTabmanager::Get()->UnregisterTabSpawner(AssistLayout::BottomToolsTabId);

    // Unregister the startup function
    UToolMenus::UnRegisterStartupCallback(this);

    // Unregister all our menu extensions
    UToolMenus::UnregisterOwner(this);

    FAssistStyle::Shutdown();
}

void FAssistModule::init() {
    if (TSharedPtr<IPlugin> ThisPlugin = IPluginManager::Get().FindPlugin("Assist")) {
        const FString& ResourceFilePath = ThisPlugin->GetBaseDir() / TEXT("Resources/config.json");
        FString FileContentString;
        FFileHelper::LoadFileToString(FileContentString, *ResourceFilePath);
        if (!FJsonObjectConverter::JsonObjectStringToUStruct(FileContentString, &AssistConfig)) {
            UE_LOG(LogJson, Error, TEXT("JsonObjectStringToUStruct failed (%s)"), *ResourceFilePath);
        }
    }
}

void FAssistModule::RegisterMenus() {
    if (!WorkspaceMenu && FSlateApplication::IsInitialized()) {
        WorkspaceMenu = MakeShared<FAssistWorkspaceMenu>();
        WorkspaceMenu->Register();
    }
    if (!FlexibleLayoutTickHandle.IsValid() && FSlateApplication::IsInitialized()) {
        // Run after input/menu updates and before Slate measures/draws the frame.
        FlexibleLayoutTickHandle = FSlateApplication::Get().OnPreTick().AddRaw(this, &FAssistModule::UpdateFlexibleLayout);
    }

    if (!TabMouseDownHandle.IsValid() && FSlateApplication::IsInitialized()) {
        TabMouseDownHandle = FSlateApplication::Get().OnApplicationMousePreInputButtonDownListener().AddRaw(this, &FAssistModule::TrackPressedTab);
    }

    for (const FString& MenuModule : AssistConfig.SupportedEditors) {
        RegisterEditorMenu(MenuModule);
    }

    RegisterAssetEditorMenus();
}

void FAssistModule::RegisterAssetEditorMenus() {
    if (EditorOpeningHandle.IsValid()) {
        return;
    }
    UAssetEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
    if (!Subsystem) {
        // ToolMenus can execute its startup callback immediately during Default
        // module loading, before GEditor or its subsystem collection exists.
        // Retry after engine initialization instead of silently skipping the hook.
        if (!AssetEditorMenusInitHandle.IsValid()) {
            AssetEditorMenusInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FAssistModule::RegisterAssetEditorMenus);
            UE_LOG(LogAssist, Log, TEXT("Waiting for engine initialization to register asset editor menus."));
        }
        return;
    }

    FCoreDelegates::GetOnPostEngineInit().Remove(AssetEditorMenusInitHandle);
    AssetEditorMenusInitHandle.Reset();
    MenuAssetEditorSubsystem = Subsystem;
    // Use each toolkit's actual name, including specialized asset editors and
    // class-specific simple editors, before UE creates the first menu widget.
    EditorOpeningHandle = Subsystem->OnEditorOpeningPreWidgets().AddRaw(this, &FAssistModule::RegisterOpenedAssetEditorMenu);
    UE_LOG(LogAssist, Log, TEXT("Asset editor menu hook registered."));
    for (UObject* Asset : Subsystem->GetAllEditedAssets()) {
        RegisterOpenedAssetEditorMenu({Asset}, Subsystem->FindEditorForAsset(Asset, false));
    }
}

void FAssistModule::RegisterOpenedAssetEditorMenu(const TArray<UObject*>& Assets, IAssetEditorInstance* Editor) {
    if (!Editor) {
        return;
    }
    for (UObject* Asset : Assets) {
        if (!Asset) {
            continue;
        }
        // InitAssetEditor registers the toolkit before firing the pre-widget
        // event. Query it instead of guessing menu names from DataAsset bases.
        RegisterToolkitMenu(FToolkitManager::Get().FindEditorForAsset(Asset), Editor);
    }
}

void FAssistModule::RegisterToolkitMenu(const TSharedPtr<IToolkit>& Toolkit, IAssetEditorInstance* Editor) {
    if (!Editor || !Toolkit || !Toolkit->IsAssetEditor()) {
        return;
    }
    // Native asset toolkits derive from FAssetEditorToolkit. Match the exact
    // instance supplied by the pre-widget event before extending its menu.
    FAssetEditorToolkit* AssetEditor = static_cast<FAssetEditorToolkit*>(Toolkit.Get());
    if (static_cast<IAssetEditorInstance*>(AssetEditor) != Editor) {
        return;
    }
    const FString MenuModule = TEXT("AssetEditor.") + AssetEditor->GetToolMenuAppName().ToString();
    if (!RegisteredEditorMenus.Contains(FName(*MenuModule))) {
        RegisterEditorMenu(MenuModule);
        UE_LOG(LogAssist, Log, TEXT("Registered asset editor menu: %s.MainMenu"), *MenuModule);
    }
}

void FAssistModule::RegisterEditorMenu(const FString& MenuModule) {
    const FName EditorMenuId(*MenuModule);
    if (RegisteredEditorMenus.Contains(EditorMenuId)) {
        return;
    }
    RegisteredEditorMenus.Add(EditorMenuId);
    FToolMenuOwnerScoped OwnerScoped(this);
    UToolMenus* ToolMenus = UToolMenus::Get();
    FString MainMenuName       = MenuModule + ".MainMenu";
    FString AssistMenuName     = "AssistMenu";
    FString AssistMenuFullName = MainMenuName + "." + AssistMenuName;
    UToolMenu* AssistMenu      = ToolMenus->RegisterMenu(FName(AssistMenuFullName));

    UToolMenu* MenuBar = ToolMenus->ExtendMenu(FName(MainMenuName));
    MenuBar->AddSubMenu(
        this,                       // InOwner
        NAME_None,                  // SectionName
        FName(AssistMenuName),      // InName
        LOCTEXT("AssistMenuLabel", "Assist"), // InLabel
        LOCTEXT("ToolTip", "Some Useful tools"));

    // Section Project
    FToolMenuSection& SectionProject = AssistMenu->AddSection("Project", LOCTEXT("ProjectSection", "Project"));
    SectionProject.AddEntry(FToolMenuEntry::InitMenuEntry(
        "ReloadProject",
        INVTEXT("Reload Project"),
        INVTEXT("No tooltip for Reload Project"),
        FSlateIcon(FAssistStyle::GetStyleSetName(), "Assist.ReloadProject"),
        FExecuteAction::CreateRaw(this, &FAssistModule::ReloadProject)));

    // Section Asset
    FToolMenuSection& SectionAsset = AssistMenu->AddSection("Asset", LOCTEXT("AssetSection", "Asset"));
    TArray<FString> MenuModuleArray;
    MenuModule.ParseIntoArray(MenuModuleArray, TEXT("."));
    FString AssetType = MenuModuleArray.Last();
    if (AssetType.EndsWith("Editor")) {
        AssetType = AssetType.LeftChop(6);
    }
    const bool bLevelEditor = MenuModule == TEXT("LevelEditor");
    FToolUIAction ReloadAction;
    ReloadAction.ExecuteAction = FToolMenuExecuteAction::CreateRaw(this, &FAssistModule::ReloadAsset, bLevelEditor);
    ReloadAction.CanExecuteAction = FToolMenuCanExecuteAction::CreateRaw(this, &FAssistModule::CanReloadAsset, bLevelEditor);
    SectionAsset.AddEntry(FToolMenuEntry::InitMenuEntry(
        "ReloadAsset",
        INVTEXT("Reload " + AssetType),
        LOCTEXT("ReloadAssetTooltip", "Reload the assets belonging to this editor from disk."),
        FSlateIcon(FAssistStyle::GetStyleSetName(), "Assist.ReloadProject"),
        ReloadAction));

    // Section Layout
    FToolMenuSection& SectionLayout = AssistMenu->AddSection("Layout", LOCTEXT("LayoutSection", "Layout"));
    SectionLayout.AddEntry(FToolMenuEntry::InitMenuEntry(
        "SetFlexibleLayout",
        LOCTEXT("FlexibleLayout", "Flexible Layout"),
        LOCTEXT("FlexibleLayoutTooltip", "Create left and bottom docking areas for general-purpose tool panels. Stop Play or Simulate before changing layouts."),
        FSlateIcon(FAssistStyle::GetStyleSetName(), "Assist.FlexibleLayout"),
        FUIAction(FExecuteAction::CreateRaw(this, &FAssistModule::SetFlexibleLayout),
            FCanExecuteAction::CreateStatic(&CanChangeEditorLayout))));

    SectionLayout.AddDynamicEntry("DefaultEditorLayoutShortcut",
        FNewToolMenuSectionDelegate::CreateRaw(this, &FAssistModule::AddDefaultEditorLayoutShortcut));

    // Section Language
    FToolMenuSection& SectionLanguage = AssistMenu->AddSection("Language", LOCTEXT("LanguageSection", "Language"));
    SectionLanguage.AddEntry(FToolMenuEntry::InitMenuEntry(
        "SetLanguageToEn",
        INVTEXT("en"),
        INVTEXT("Set Language to en"),
        FSlateIcon(FAssistStyle::GetStyleSetName(), "Assist.SetLanguageToEn"),
        FExecuteAction::CreateRaw(this, &FAssistModule::SetCurrentLanguage, FString("en"))));

    SectionLanguage.AddEntry(FToolMenuEntry::InitMenuEntry(
        "SetLanguageToZhHans",
        INVTEXT("zh-Hans"),
        INVTEXT("Set Language to zh-Hans"),
        FSlateIcon(FAssistStyle::GetStyleSetName(), "Assist.SetLanguageToZhHans"),
        FExecuteAction::CreateRaw(this, &FAssistModule::SetCurrentLanguage, FString("zh-hans"))));
}

void FAssistModule::ReloadProject() {
    FText DialogText              = FText::Format(LOCTEXT("PluginButtonDialogText", "Reload {0} Project ?"), FText::FromString(FApp::GetProjectName()));
    EAppReturnType::Type Decision = FMessageDialog::Open(EAppMsgType::OkCancel, DialogText);
    if (Decision == EAppReturnType::Ok) {
        FUnrealEdMisc::Get().RestartEditor(false);
        // FText OutFailReason;
        // GameProjectUtils::OpenProject(FPaths::GetProjectFilePath(), OutFailReason);
    }
}

bool FAssistModule::CanReloadAsset(const FToolMenuContext& Context, bool bLevelEditor) const {
    if (!CanChangeEditorLayout()) {
        return false;
    }
    UWorld* World = bLevelEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    return !AssistEditorContext::GetReloadPackages(Context, bLevelEditor, World).IsEmpty();
}

void FAssistModule::ReloadAsset(const FToolMenuContext& Context, bool bLevelEditor) {
    if (!CanChangeEditorLayout()) {
        return;
    }
    UWorld* World = bLevelEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    TArray<UPackage*> Packages = AssistEditorContext::GetReloadPackages(Context, bLevelEditor, World);
    if (Packages.IsEmpty()) {
        return;
    }
    bool bAnyPackagesReloaded = false;
    FText ErrorMessage;
    UEditorLoadingAndSavingUtils::ReloadPackages(Packages, bAnyPackagesReloaded, ErrorMessage, EReloadPackagesInteractionMode::Interactive);
    if (!ErrorMessage.IsEmpty()) {
        FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage);
    }
}

void FAssistModule::TrackPressedTab(const FPointerEvent& Event) {
    if (Event.GetEffectingButton() != EKeys::LeftMouseButton) {
        return;
    }
    PressedTab.Reset();
    FSlateApplication& Slate = FSlateApplication::Get();
    if (Slate.IsDragDropping()) {
        return;
    }
    // Native docking's payload is private to Slate. Record the exact pressed
    // header before the drag starts; focus/active tabs can refer to another
    // editor or be cleared while the native drag preview is being created.
    // This notification observes input without consuming it or changing focus.
    const FWidgetPath Path = Slate.LocateWindowUnderMouse(Event.GetScreenSpacePosition(),
        Slate.GetInteractiveTopLevelWindows(), false, Event.GetUserIndex());
    for (int32 Index = Path.Widgets.Num() - 1; Index >= 0; --Index) {
        const TSharedRef<SWidget>& Widget = Path.Widgets[Index].Widget;
        if (Widget->GetType() == FName(TEXT("SDockTab"))) {
            PressedTab = StaticCastSharedRef<SDockTab>(Widget);
            break;
        }
    }
}

void FAssistModule::UpdateFlexibleLayout(float DeltaTime) {
    if (!FSlateApplication::IsInitialized()) {
        return;
    }
    if (!StatusBarHost) {
        StatusBarHost = MakeShared<FAssistStatusBarHost>();
    }
    StatusBarHost->Update(WorkspaceMenu ? WorkspaceMenu->GetCurrentEditor() : nullptr);
    const TSharedRef<FGlobalTabmanager> Global = FGlobalTabmanager::Get();
    const TSharedPtr<SWindow> Root = Global->GetRootWindow();
    const TSharedPtr<SDockTab> Left = Global->FindExistingLiveTab(AssistLayout::LeftToolsTabId);
    const TSharedPtr<SDockTab> Bottom = Global->FindExistingLiveTab(AssistLayout::BottomToolsTabId);
    const TSharedPtr<SDockTab> Level = Global->FindExistingLiveTab(FTabId(TEXT("LevelEditor")));
    if (!Root || !Left || !Bottom || !Level || Left->GetParentWindow() != Root ||
        Bottom->GetParentWindow() != Root || Level->GetParentWindow() != Root) {
        return;
    }
    const float MenuInset = WorkspaceMenu ? WorkspaceMenu->GetMenuInset() : 30.0f;
    FSlateApplication& Slate = FSlateApplication::Get();
    const TSharedPtr<FDragDropOperation> Drag = Slate.GetDragDroppingContent();
    // A native docking operation alone is insufficient: each tool host must
    // accept the tab that initiated this drag before its empty area is exposed.
    const bool bDockingDrag = Drag && Drag->IsOfTypeImpl(TEXT("FDockingDragOperation"));
    const TSharedPtr<SDockTab> DraggedTab = bDockingDrag ? PressedTab.Pin() : nullptr;
    if (!bDockingDrag && !Slate.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton)) {
        PressedTab.Reset();
    }
    const auto UpdateHost = [&DraggedTab](const TSharedPtr<SDockTab>& Host, float Inset) {
        const TSharedRef<SWidget> Content = Host->GetContent();
        const TSharedPtr<SDockingTabStack> Stack = Host->GetParentDockTabStack();
        if (Stack && Content->GetType() == FName(TEXT("SAssistToolPanelHost"))) {
            const TSharedRef<SAssistToolPanelHost> Tools = StaticCastSharedRef<SAssistToolPanelHost>(Content);
            AssistWorkspaceChrome::UpdateToolHost(Stack.ToSharedRef(), Inset, Tools->HasOpenTools(), Tools->CanAcceptTab(DraggedTab));
        }
    };
    UpdateHost(Left, MenuInset);
    UpdateHost(Bottom, 0.0f);
    if (const TSharedPtr<SDockingTabStack> Stack = Level->GetParentDockTabStack()) {
        AssistWorkspaceChrome::ReserveEditorMenu(Stack.ToSharedRef(), MenuInset);
    }
}

TSharedRef<SDockTab> FAssistModule::SpawnToolHost(const FSpawnTabArgs& Args, bool bLeft) {
    const TSharedRef<SDockTab> Tab = SNew(SDockTab)
        .TabRole(ETabRole::PanelTab)
        .CanEverClose(false)
        .ContentPadding(FMargin(0))
        .Label(bLeft ? LOCTEXT("LeftTools", "Left Tools") : LOCTEXT("BottomTools", "Bottom Tools"));
    FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
    FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("OutputLog"));
    const TSharedRef<SAssistToolPanelHost> Content = SNew(SAssistToolPanelHost, Tab, bLeft);
    Tab->SetContent(Content);
    Tab->SetOnPersistVisualState(SDockTab::FOnPersistVisualState::CreateSP(Content, &SAssistToolPanelHost::RequestSave));
    return Tab;
}

void FAssistModule::SetFlexibleLayout() {
    if (!CanChangeEditorLayout()) {
        return;
    }
    const TSharedRef<FGlobalTabmanager> TabManager = FGlobalTabmanager::Get();
    if (!TabManager->GetRootWindow().IsValid()) {
        return;
    }
    FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
    FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("OutputLog"));
    TabManager->SaveAllVisualState();
    TSet<FName> ToolTabs;
    for (bool bLeft : {true, false}) {
        const TSharedRef<FTabManager::FLayout> Tools = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, AssistLayout::MakeToolLayout(bLeft));
        ToolTabs.Append(AssistLayout::GetOpenToolTabs(Tools));
    }
    const TSharedPtr<FTabManager::FLayout> Layout = AssistLayout::MakeFlexibleLayout(TabManager->PersistLayout(), ToolTabs);
    if (!Layout) {
        FMessageDialog::Open(EAppMsgType::Ok,
            LOCTEXT("FlexibleMissingPrimaryArea", "The main editor layout could not be found. Open the Level Editor before applying this layout."));
        return;
    }
    // Do not let embedded editors claim the singleton tools before the hosts.
    // The outer layout and each host's layout remain separate native layouts.
    SaveEmbeddedEditorLayoutsWithoutTools(TabManager, ToolTabs);
    FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, Layout.ToSharedRef());
    GConfig->Flush(false, GEditorLayoutIni);
    ReloadEditorLayout();

    ContentBrowser.Get().SetPrimaryContentBrowser(TEXT("ContentBrowserTab1"));
    const TSharedPtr<SDockTab> Left = TabManager->FindExistingLiveTab(AssistLayout::LeftToolsTabId);
    const TSharedPtr<FTabManager> Tools = Left ? TabManager->GetTabManagerForMajorTab(Left) : nullptr;
    if (const TSharedPtr<SDockTab> Browser = Tools ? Tools->FindExistingLiveTab(FTabId(TEXT("ContentBrowserTab1"))) : nullptr) {
        TSharedPtr<SWidget> AssetViewWidget;
        FindWidget(Browser->GetContent(), TEXT("SAssetView"), AssetViewWidget);
        if (AssetViewWidget) {
            SAssetView& AssetView = static_cast<SAssetView&>(*AssetViewWidget);
            AssetView.SetCurrentViewType(EAssetViewType::List);
            AssetView.SetCurrentThumbnailSize(EThumbnailSize::Small);
        }
    }
    TabManager->SaveAllVisualState();
    GConfig->Flush(false, GEditorLayoutIni);
}

void FAssistModule::AddDefaultEditorLayoutShortcut(FToolMenuSection& Section) {
    UToolMenus* ToolMenus      = UToolMenus::Get();
    UToolMenu* WindowMenu      = ToolMenus->GenerateMenu(TEXT("MainFrame.MainMenu.Window"), Section.Context);
    UToolMenu* LoadMenu        = WindowMenu ? ToolMenus->GenerateSubMenu(WindowMenu, TEXT("LoadLayout")) : nullptr;
    FToolMenuSection* Defaults = LoadMenu ? LoadMenu->FindSection(TEXT("DefaultLayouts")) : nullptr;
    if (Defaults) {
        for (const FToolMenuEntry& NativeEntry : Defaults->Blocks) {
            const FText NativeLabel          = NativeEntry.Label.Get();
            const TOptional<FString> TextKey = FTextInspector::GetKey(NativeLabel);
            const FString* SourceLabel       = FTextInspector::GetSourceString(NativeLabel);
            // Match the engine entry independently of the editor's display language.
            if ((TextKey.IsSet() && TextKey.GetValue() == TEXT("Default_Editor_Layout")) ||
                (SourceLabel && *SourceLabel == TEXT("Default Editor Layout"))) {
                // Keep UE's original action and enabled state. UE owns the load,
                // confirmation dialogs and editor restart/restore behavior.
                FToolMenuEntry Shortcut = NativeEntry;
                Shortcut.Name           = TEXT("LoadDefaultEditorLayout");
                Shortcut.Owner          = Section.Owner;
                Shortcut.Label          = LOCTEXT("RestoreDefaultLayout", "Restore Default Layout");
                Shortcut.ToolTip        = LOCTEXT("RestoreDefaultLayoutTooltip", "Shortcut to Window > Load Layout > Default Editor Layout.");
                Shortcut.Icon           = FSlateIcon(FAppStyle::GetAppStyleSetName(), "MainFrame.LoadLayout");
                Section.AddEntry(Shortcut);
                return;
            }
        }
    }
    Section.AddEntry(FToolMenuEntry::InitMenuEntry(
        "LoadDefaultEditorLayout",
        LOCTEXT("RestoreDefaultLayout", "Restore Default Layout"),
        LOCTEXT("DefaultEditorLayoutUnavailable", "The engine's Default Editor Layout menu entry is unavailable."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "MainFrame.LoadLayout"),
        FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; }))));
}

void FAssistModule::ReloadEditorLayout() {
    if (WorkspaceMenu) {
        WorkspaceMenu->SuspendTitleBar();
    }
    if (StatusBarHost) {
        StatusBarHost->Restore();
    }
    // Match UE's layout reload flow: suppress saving the layout being torn down,
    // and reopen the asset editors instead of losing the current editing session.
    UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    const bool bHasOpenAssets           = AssetEditors && !AssetEditors->GetAllEditedAssets().IsEmpty();
    TOptional<bool> PreviousRestoreOverride;
    if (bHasOpenAssets) {
        AssetEditors->SaveOpenAssetEditors(true);
        PreviousRestoreOverride = AssetEditors->GetAutoRestoreAndDisableSavingOverride();
        AssetEditors->SetAutoRestoreAndDisableSavingOverride(true);
    }
    const TSharedRef<FGlobalTabmanager> TabManager = FGlobalTabmanager::Get();
    const bool bCouldSave                          = TabManager->CanSavePersistentLayouts();
    TabManager->SetCanSavePersistentLayouts(false);
    SaveCurrentMainWindowPlacement();
    // Reuse the native main window; only rebuild its docking content. PIE and
    // Simulate are blocked by CanChangeEditorLayout, so do not start either.
    IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
#if PLATFORM_MAC
    if (const TSharedPtr<SWindow> Root = TabManager->GetRootWindow()) {
        AssistWorkspaceChrome::ReconstructMacMainFrameContent(Root.ToSharedRef(), *GConfig, GEditorPerProjectIni,
            [&MainFrame] { MainFrame.ReconstructDefaultMainFrameContent(false, false); });
    }
#else
    MainFrame.ReconstructDefaultMainFrameContent(false, false);
#endif
    if (const TSharedPtr<SWindow> Root = TabManager->GetRootWindow()) {
        AssistWorkspaceChrome::FinishMainFrameReconstruction(Root.ToSharedRef());
    }
    TabManager->SetCanSavePersistentLayouts(bCouldSave);
    if (bHasOpenAssets) {
        AssetEditors->RestorePreviouslyOpenAssets();
        AssetEditors->SetAutoRestoreAndDisableSavingOverride(PreviousRestoreOverride);
    }
}

void FAssistModule::SetCurrentLanguage(const FString Language) {
    UKismetInternationalizationLibrary::SetCurrentLanguage(Language);
}

void FAssistModule::FindWidget(TSharedRef<SWidget> Parent, FString TypeString, TSharedPtr<SWidget>& Result) {
    if (Parent->GetTypeAsString() == TypeString) {
        Result = Parent;
        return;
    }
    for (int32 i = 0; i < Parent->GetChildren()->Num(); i++) {
        FindWidget(Parent->GetChildren()->GetChildAt(i), TypeString, Result);
        if (Result) return;
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAssistModule, Assist)
