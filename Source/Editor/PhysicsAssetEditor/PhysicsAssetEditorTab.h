#pragma once

#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Editor/AssetEditor/AssetEditorPanel.h"

class FPhysicsAssetEditor;

// 배치: 왼쪽 위 Skeleton Tree, 왼쪽 아래 Graph, 가운데 Viewport, 오른쪽 Details.
class FPhysicsAssetEditorTab : public FAssetEditorTab
{
public:
	explicit FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor);

	void DrawMenuBar() override;
	bool HasFocusedSubWindow() const override { return EditorDock::HasFocusedPanel(Panels); }
	void OnRender(bool bActive) override;

private:
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size);
	void DrawRegenerateConfirm();   // 전체 재생성은 편집한 값을 모두 덮으므로 한 번 묻는다

	bool bOpenRegenerateConfirm = false;

	FPhysicsAssetEditor& PhysicsEditor;

	// 순서가 BuildDefaultLayout의 배치와 맞는다
	enum EPanel : int32 { Viewport, SkeletonTree, Graph, Details, Count };
	TArray<TUniquePtr<FAssetEditorPanelBase>> Panels;

	ImGuiID DockSpaceId = 0;
	ImGuiID PanelClassId = 0;
	bool bLayoutInitialized = false;
};
