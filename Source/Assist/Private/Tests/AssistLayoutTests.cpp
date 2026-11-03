#include "AssistLayout.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

namespace {
const TSet<FName> DefaultTools{TEXT("ContentBrowserTab1"), TEXT("OutputLog")};

TSharedRef<FTabManager::FLayout> MakeTestLayout() {
    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("AssistLayoutTest"))
        ->AddArea(FTabManager::NewArea(400, 800)->Split(
            FTabManager::NewStack()->AddTab(TEXT("ContentBrowserTab1"), ETabState::OpenedTab)))
        ->AddArea(FTabManager::NewPrimaryArea()->Split(FTabManager::NewStack()
            ->AddTab(TEXT("LevelEditor"), ETabState::OpenedTab)->AddTab(TEXT("DockedToolkit"), ETabState::ClosedTab)))
        ->AddArea(FTabManager::NewArea(600, 400)->SetWindow(FVector2D(100, 200), false)->Split(FTabManager::NewStack()
            ->AddTab(TEXT("Statistics"), ETabState::OpenedTab)->AddTab(TEXT("OutputLog"), ETabState::OpenedTab)));
    const TSharedRef<FJsonObject> Json = Layout->ToJson();
    Json->SetNumberField(TEXT("PrimaryAreaIndex"), 1);
    return FTabManager::FLayout::NewFromJson(Json).ToSharedRef();
}

int32 CountTabs(const TSharedPtr<FJsonObject>& Node, FName Id, const FString& State = FString()) {
    const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
    int32 Count = 0;
    if (Node->TryGetArrayField(TEXT("Tabs"), Children)) {
        for (const TSharedPtr<FJsonValue>& Value : *Children) {
            const TSharedPtr<FJsonObject> Tab = Value->AsObject();
            if (FName(*Tab->GetStringField(TEXT("TabId"))) == Id && (State.IsEmpty() || Tab->GetStringField(TEXT("TabState")) == State)) {
                ++Count;
            }
        }
    }
    if (Node->TryGetArrayField(TEXT("nodes"), Children) || Node->TryGetArrayField(TEXT("Areas"), Children)) {
        for (const TSharedPtr<FJsonValue>& Child : *Children) {
            Count += CountTabs(Child->AsObject(), Id, State);
        }
    }
    return Count;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistFlexibleLayoutTest, "Assist.Layout.Flexible.RegionsAndPreservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistFlexibleLayoutTest::RunTest(const FString& Parameters) {
    const TSharedRef<FTabManager::FLayout> Source = MakeTestLayout();
    const FString Before = Source->ToString();
    const TSharedPtr<FTabManager::FLayout> Result = AssistLayout::MakeFlexibleLayout(Source, DefaultTools);
    if (!TestTrue(TEXT("The flexible layout is produced"), Result.IsValid())) {
        return false;
    }
    TestEqual(TEXT("The source is unchanged"), Source->ToString(), Before);
    TestEqual(TEXT("The main layout name is preserved"), Result->GetLayoutName(), Source->GetLayoutName());
    TestEqual(TEXT("The browser-only floating area is removed"), Result->GetAreas().Num(), 2);
    TestEqual(TEXT("One left host exists"), CountTabs(Result->ToJson(), AssistLayout::LeftToolsTabId), 1);
    TestEqual(TEXT("One bottom host exists"), CountTabs(Result->ToJson(), AssistLayout::BottomToolsTabId), 1);
    TestEqual(TEXT("The outer layout cannot claim CB1"), CountTabs(Result->ToJson(), TEXT("ContentBrowserTab1")), 0);
    TestEqual(TEXT("The outer layout cannot claim Output Log"), CountTabs(Result->ToJson(), TEXT("OutputLog")), 0);
    const TSharedPtr<FTabManager::FArea> Primary = Result->GetPrimaryArea().Pin();
    if (!TestTrue(TEXT("The primary index is fixed after removing an earlier area"), Primary.IsValid())) {
        return false;
    }
    TestEqual(TEXT("Left and right are split horizontally"), Primary->GetOrientation(), Orient_Horizontal);
    if (!TestEqual(TEXT("There are exactly two outer regions"), Primary->GetChildNodes().Num(), 2)) {
        return false;
    }
    TestEqual(TEXT("The left region owns the left tool host"), Primary->GetChildNodes()[0]->AsStack()->GetTabs()[0].TabId.TabType, AssistLayout::LeftToolsTabId);
    TestTrue(TEXT("The left region starts at 20 percent"), FMath::IsNearlyEqual(Primary->GetChildNodes()[0]->GetSizeCoefficient(), 0.2f));
    const TSharedPtr<FTabManager::FSplitter> Right = Primary->GetChildNodes()[1]->AsSplitter();
    if (!TestTrue(TEXT("The right region is a vertical split"), Right && Right->GetOrientation() == Orient_Vertical && Right->GetChildNodes().Num() == 2)) {
        return false;
    }
    TestEqual(TEXT("Resource editors remain in the original editor stack"), Right->GetChildNodes()[0]->AsStack()->GetTabs().Num(), 2);
    TestEqual(TEXT("The bottom host is below the editors"), Right->GetChildNodes()[1]->AsStack()->GetTabs()[0].TabId.TabType, AssistLayout::BottomToolsTabId);
    TestTrue(TEXT("The bottom region starts at 25 percent"), FMath::IsNearlyEqual(Right->GetChildNodes()[1]->GetSizeCoefficient(), 0.25f));
    TestEqual(TEXT("Unrelated floating panels survive"), Result->GetAreas()[1]->GetChildNodes()[0]->AsStack()->GetTabs()[0].TabId.TabType, FName(TEXT("Statistics")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistFlexibleRepeatTest, "Assist.Layout.Flexible.RepeatedApplication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistFlexibleRepeatTest::RunTest(const FString& Parameters) {
    TSharedRef<FTabManager::FLayout> Layout = MakeTestLayout();
    for (int32 Index = 0; Index < 5; ++Index) {
        const TSharedPtr<FTabManager::FLayout> Next = AssistLayout::MakeFlexibleLayout(Layout, DefaultTools);
        if (!TestTrue(TEXT("Repeated conversion succeeds"), Next.IsValid())) {
            return false;
        }
        Layout = FTabManager::FLayout::NewFromString(Next->ToString()).ToSharedRef();
        TestEqual(TEXT("The left host is not duplicated"), CountTabs(Layout->ToJson(), AssistLayout::LeftToolsTabId), 1);
        TestEqual(TEXT("The bottom host is not duplicated"), CountTabs(Layout->ToJson(), AssistLayout::BottomToolsTabId), 1);
        const TSharedPtr<FTabManager::FSplitter> Right = Layout->GetPrimaryArea().Pin()->GetChildNodes()[1]->AsSplitter();
        TestTrue(TEXT("The editor is not nested in redundant splitters"), Right && Right->GetChildNodes()[0]->AsStack().IsValid());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistLegacyFixedLayoutTest, "Assist.Layout.Flexible.ReplaceLegacyFixedLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistLegacyFixedLayoutTest::RunTest(const FString& Parameters) {
    for (EOrientation Orientation : {Orient_Horizontal, Orient_Vertical}) {
        const TSharedRef<FTabManager::FLayout> Legacy = FTabManager::NewLayout(TEXT("ExistingEditorLayout"))
            ->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orientation)
                ->Split(FTabManager::NewStack()->SetHideTabWell(true)->AddTab(TEXT("ContentBrowserTab1"), ETabState::OpenedTab))
                ->Split(FTabManager::NewStack()->AddTab(TEXT("LevelEditor"), ETabState::OpenedTab)));
        const TSharedRef<FJsonObject> Json = Legacy->ToJson();
        Json->SetNumberField(TEXT("PrimaryAreaIndex"), 0);
        const TSharedPtr<FTabManager::FLayout> Converted = AssistLayout::MakeFlexibleLayout(
            FTabManager::FLayout::NewFromJson(Json).ToSharedRef(), DefaultTools);
        if (!TestTrue(TEXT("An existing fixed layout can be replaced"), Converted.IsValid())) {
            return false;
        }
        TestEqual(TEXT("Old CB1 is not retained in the outer layout"), CountTabs(Converted->ToJson(), TEXT("ContentBrowserTab1")), 0);
        TestEqual(TEXT("The level editor is retained once"), CountTabs(Converted->ToJson(), TEXT("LevelEditor")), 1);
        TestEqual(TEXT("The left host is created once"), CountTabs(Converted->ToJson(), AssistLayout::LeftToolsTabId), 1);
        TestEqual(TEXT("The bottom host is created once"), CountTabs(Converted->ToJson(), AssistLayout::BottomToolsTabId), 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistEmbeddedToolsTest, "Assist.Layout.Flexible.CloseEmbeddedTools",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistEmbeddedToolsTest::RunTest(const FString& Parameters) {
    const TSharedRef<FTabManager::FLayout> Source = FTabManager::NewLayout(TEXT("Embedded"))
        ->AddArea(FTabManager::NewPrimaryArea()->Split(FTabManager::NewStack()
            ->AddTab(TEXT("Viewport"), ETabState::OpenedTab)
            ->AddTab(TEXT("ContentBrowserTab1"), ETabState::SidebarTab, ESidebarLocation::Left, 0.3f, true)
            ->AddTab(TEXT("OutputLog"), ETabState::OpenedTab)
            ->AddTab(TEXT("ContentBrowserTab2"), ETabState::OpenedTab)->SetForegroundTab(FTabId(TEXT("OutputLog")))));
    const TSharedRef<FJsonObject> Json = Source->ToJson();
    Json->SetNumberField(TEXT("PrimaryAreaIndex"), 0);
    const TSharedPtr<FJsonObject> Area = Json->GetArrayField(TEXT("Areas"))[0]->AsObject();
    Area->SetStringField(TEXT("ActivePanelDrawerTab"), TEXT("ContentBrowserTab1"));
    Area->SetNumberField(TEXT("PanelDrawerCoefficient"), 0.25);
    const TSharedRef<FTabManager::FLayout> Input = FTabManager::FLayout::NewFromJson(Json).ToSharedRef();
    const FString Before = Input->ToString();
    const TSharedRef<FTabManager::FLayout> Result = AssistLayout::CloseEmbeddedTools(Input, DefaultTools);
    TestEqual(TEXT("The input remains unchanged"), Input->ToString(), Before);
    TestEqual(TEXT("The browser is retained closed"), CountTabs(Result->ToJson(), TEXT("ContentBrowserTab1"), TEXT("ClosedTab")), 1);
    TestEqual(TEXT("Output Log is retained closed"), CountTabs(Result->ToJson(), TEXT("OutputLog"), TEXT("ClosedTab")), 1);
    TestEqual(TEXT("Editor-local content stays open"), CountTabs(Result->ToJson(), TEXT("Viewport"), TEXT("OpenedTab")), 1);
    TestEqual(TEXT("Unclaimed browsers stay open"), CountTabs(Result->ToJson(), TEXT("ContentBrowserTab2"), TEXT("OpenedTab")), 1);
    TestFalse(TEXT("The drawer cannot claim the global tool"), Result->ToJson()->GetArrayField(TEXT("Areas"))[0]->AsObject()->HasField(TEXT("ActivePanelDrawerTab")));
    TestEqual(TEXT("Closing tools is idempotent"), AssistLayout::CloseEmbeddedTools(Result, DefaultTools)->ToString(), Result->ToString());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistToolLayoutTest, "Assist.Layout.Flexible.ToolDefaultsAndEmptyRestore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistToolLayoutTest::RunTest(const FString& Parameters) {
    for (bool bLeft : {true, false}) {
        const TSharedRef<FTabManager::FLayout> Default = AssistLayout::MakeToolLayout(bLeft);
        const FName Tool(bLeft ? TEXT("ContentBrowserTab1") : TEXT("OutputLog"));
        TestTrue(TEXT("The intended tool is opened"), AssistLayout::GetOpenToolTabs(Default).Contains(Tool));
        TestFalse(TEXT("Single-tool defaults show tabs"), Default->ToJson()->GetArrayField(TEXT("Areas"))[0]->AsObject()
            ->GetArrayField(TEXT("nodes"))[0]->AsObject()->GetBoolField(TEXT("HideTabWell")));
        for (bool bHide : {false, true}) {
            const TSharedRef<FJsonObject> SavedJson = Default->ToJson();
            SavedJson->GetArrayField(TEXT("Areas"))[0]->AsObject()->GetArrayField(TEXT("nodes"))[0]->AsObject()
                ->SetBoolField(TEXT("HideTabWell"), bHide);
            const TSharedRef<FTabManager::FLayout> Saved = FTabManager::FLayout::NewFromJson(SavedJson).ToSharedRef();
            const FString Before = Saved->ToString();
            const TSharedRef<FTabManager::FLayout> Restored = AssistLayout::CleanToolLayout(AssistLayout::PrepareToolLayout(Saved));
            TestEqual(TEXT("The user's tab visibility choice is preserved"), Restored->ToJson()->GetArrayField(TEXT("Areas"))[0]->AsObject()
                ->GetArrayField(TEXT("nodes"))[0]->AsObject()->GetBoolField(TEXT("HideTabWell")), bHide);
            TestEqual(TEXT("Restoring does not change the saved tab visibility"), Saved->ToString(), Before);
        }
        const TSharedRef<FTabManager::FLayout> Empty = AssistLayout::CloseEmbeddedTools(Default, {Tool});
        const FString Before = Empty->ToString();
        const TSharedRef<FTabManager::FLayout> Prepared = AssistLayout::PrepareToolLayout(Empty);
        TestEqual(TEXT("Preparing does not mutate the saved empty layout"), Empty->ToString(), Before);
        TestEqual(TEXT("One temporary tab initializes the native empty drop target"), CountTabs(Prepared->ToJson(), AssistLayout::BootstrapTabId, TEXT("OpenedTab")), 1);
        TestEqual(TEXT("The bootstrap tab is not treated as a user tool"), AssistLayout::GetOpenToolTabs(Prepared).Num(), 0);
        const TSharedRef<FTabManager::FLayout> Clean = AssistLayout::CleanToolLayout(Prepared);
        TestEqual(TEXT("The temporary tab is not saved"), CountTabs(Clean->ToJson(), AssistLayout::BootstrapTabId), 0);
        TestTrue(TEXT("An empty primary tool area survives"), Clean->GetPrimaryArea().IsValid());
        TestEqual(TEXT("No closed tool is reopened in an emptied container"), AssistLayout::GetOpenToolTabs(Clean).Num(), 0);
        TestEqual(TEXT("Repeated initialization does not nest bootstrap stacks"),
            AssistLayout::CleanToolLayout(AssistLayout::PrepareToolLayout(Clean))->ToString(), Clean->ToString());
    }
    const TSharedRef<FTabManager::FLayout> Missing = FTabManager::NewLayout(TEXT("NoArea"));
    TestFalse(TEXT("An outer layout without a main editor is rejected"), AssistLayout::MakeFlexibleLayout(Missing, DefaultTools).IsValid());
    TestTrue(TEXT("An entirely empty saved tool layout gets a primary drop area"), AssistLayout::PrepareToolLayout(Missing)->GetPrimaryArea().IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssistDragPreviewLayoutTest, "Assist.Layout.Flexible.RecoverDragPreview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssistDragPreviewLayoutTest::RunTest(const FString& Parameters) {
    // Reproduce a snapshot gathered mid-drag: PersistLayout enumerates the real
    // area first, then the cursor preview, and records the latter as primary.
    const TSharedRef<FTabManager::FLayout> Source = FTabManager::NewLayout(TEXT("Assist.LeftTools_v1"))
        ->AddArea(FTabManager::NewPrimaryArea()->Split(FTabManager::NewStack()->SetHideTabWell(true)
            ->AddTab(TEXT("ContentBrowserTab1"), ETabState::OpenedTab)))
        ->AddArea(FTabManager::NewPrimaryArea()->Split(FTabManager::NewStack()
            ->AddTab(TEXT("EUW_ConsoleCommands"), ETabState::OpenedTab)))
        ->AddArea(FTabManager::NewArea(600, 400)->SetWindow(FVector2D(100, 200), false)->Split(FTabManager::NewStack()
            ->AddTab(TEXT("Statistics"), ETabState::OpenedTab)));
    const TSharedRef<FJsonObject> Json = Source->ToJson();
    Json->SetNumberField(TEXT("PrimaryAreaIndex"), 1);
    const TSharedRef<FTabManager::FLayout> Input = FTabManager::FLayout::NewFromJson(Json).ToSharedRef();
    const FString Before = Input->ToString();
    const TSharedRef<FTabManager::FLayout> Clean = AssistLayout::CleanToolLayout(Input);
    TestEqual(TEXT("Repair does not mutate the saved snapshot"), Input->ToString(), Before);
    if (!TestEqual(TEXT("Only the real embedded area and floating window survive"), Clean->GetAreas().Num(), 2)) {
        return false;
    }
    TestTrue(TEXT("The permanent area becomes primary again"), Clean->GetPrimaryArea().Pin() == Clean->GetAreas()[0]);
    TestEqual(TEXT("The permanent tool is preserved"), CountTabs(Clean->ToJson(), TEXT("ContentBrowserTab1"), TEXT("OpenedTab")), 1);
    TestEqual(TEXT("The cursor preview is not restored as a second region"), CountTabs(Clean->ToJson(), TEXT("EUW_ConsoleCommands")), 0);
    TestEqual(TEXT("Unrelated floating tools survive"), CountTabs(Clean->ToJson(), TEXT("Statistics"), TEXT("OpenedTab")), 1);
    TestTrue(TEXT("The real area's tab visibility is preserved"), Clean->ToJson()->GetArrayField(TEXT("Areas"))[0]->AsObject()
        ->GetArrayField(TEXT("nodes"))[0]->AsObject()->GetBoolField(TEXT("HideTabWell")));
    TestEqual(TEXT("Repair is idempotent"), AssistLayout::CleanToolLayout(Clean)->ToString(), Clean->ToString());
    TestEqual(TEXT("Preparing for restore does not recreate the preview"),
        AssistLayout::CleanToolLayout(AssistLayout::PrepareToolLayout(Input))->ToString(), Clean->ToString());
    return true;
}
#endif
