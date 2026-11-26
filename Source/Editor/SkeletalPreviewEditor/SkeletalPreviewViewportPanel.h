#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "SkeletalPreviewEditor.h"

struct FPreviewViewportToolbarState
{
    bool bCameraSpeedDragged = false;
};

#include <functional>

// 스켈레탈 프리뷰/메시 에디터 뷰포트 상단에 공용 뷰 툴바(Projection, Camera Speed, View Mode, Show)를 오른쪽 정렬로 그린다.
void DrawPreviewViewToolbar(
    FSkeletalPreviewEditor& Editor,
    FPreviewViewportToolbarState& State,
    const std::function<void()>& LeftToolbarContent = nullptr,
    const std::function<void()>& ShowMenuContent = nullptr);   // 표시 메뉴(눈 아이콘) 맨 위에 넣을 항목

class FSkeletalPreviewViewportPanel : public TAssetEditorPanel<FSkeletalPreviewEditor>
{
public:
    explicit FSkeletalPreviewViewportPanel(FSkeletalPreviewEditor& InEditor)
        : TAssetEditorPanel<FSkeletalPreviewEditor>(InEditor, "Viewport", "AnimSequencePanel")
    {
    }

protected:
    void DrawContent() override;
    bool IsViewportPanel() const override { return true; }

private:
    FPreviewViewportToolbarState ToolbarState;
};
