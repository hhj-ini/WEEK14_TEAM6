#pragma once

#include "SkeletalMeshEditorPanel.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewViewportPanel.h"

// 프리뷰 렌더 결과를 표시하고 카메라 입력과 화면 크기를 Editor에 전달합니다.
class FSkeletalMeshViewportPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FSkeletalMeshViewportPanel(FSkeletalMeshEditor& InEditor);

protected:
	void DrawContent() override;
	bool IsViewportPanel() const override { return true; }

private:
	void DrawSectionContextMenu();

	FPreviewViewportToolbarState ToolbarState;
	bool bRightClickTracking = false;
	ImVec2 RightClickStartPos = ImVec2(0.0f, 0.0f);
	int32 ContextMenuSectionIndex = -1;
	char CreateClothNameBuf[128] = "Clothing_0";
};
