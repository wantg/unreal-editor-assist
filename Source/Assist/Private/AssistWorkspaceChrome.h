#pragma once

#include "CoreMinimal.h"

class FConfigCacheIni;
class SDockingTabStack;
class SWindow;

namespace AssistWorkspaceChrome {
// The content-reconstruction API reuses an existing SWindow. Finish that
// one-time transition by matching the render/input geometry to its native window.
void FinishMainFrameReconstruction(const TSharedRef<SWindow>& Window);

#if PLATFORM_MAC
// MainFrame's content-only reload otherwise replaces a maximized window's size
// with the full display size and maximizes it again. Override only the settings
// read during reconstruction; retain the existing native window and saved state.
void ReconstructMacMainFrameContent(const TSharedRef<SWindow>& Window, FConfigCacheIni& Config,
    const FString& SettingsFile, TFunctionRef<void()> Reconstruct);
void SynchronizeMacWindowGeometry(const TSharedRef<SWindow>& Window);
#endif

// Retain the native header in the tree for tab/window ownership, but constrain
// structural hosts to a stable menu spacer (left) or zero-height header (bottom).
// Empty hosts collapse without removing their saved docking layout or size.
// Show them only when they can accept the current native tab drag.
bool UpdateToolHost(const TSharedRef<SDockingTabStack>& Stack, float MenuInset, bool bHasTools = true, bool bCanAcceptDrag = false);
bool ReserveEditorMenu(const TSharedRef<SDockingTabStack>& Stack, float MenuInset);
}
