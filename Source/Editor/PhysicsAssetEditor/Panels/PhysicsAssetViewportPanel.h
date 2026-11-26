#pragma once

#include "PhysicsAssetEditorPanel.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewViewportPanel.h"

// 프리뷰 화면. 왼쪽 툴바에 시뮬레이트와 기즈모 모드(이동·회전·크기), 표시 메뉴에 Physics Rendering을 둔다.
// W/E/R로 기즈모 모드, Delete로 선택 삭제. 마우스·카메라 입력과 Alt·Shift 상태를 에디터로 넘긴다.
class FPhysicsAssetViewportPanel : public FPhysicsAssetEditorPanel
{
public:
	explicit FPhysicsAssetViewportPanel(FPhysicsAssetEditor& InEditor);

protected:
	void DrawContent() override;
	bool IsViewportPanel() const override { return true; }

private:
	void DrawLeftToolbar();
	void DrawSimulateButton();   // 시뮬레이트 버튼과 ⋮ 옵션 메뉴 (방식, 중력 없음, 바닥 충돌)
	void DrawPhysicsRenderingMenu();   // 표시 메뉴(눈 아이콘) 맨 위의 질량·메시·바디·조인트 표시

	FPreviewViewportToolbarState ToolbarState;
};
