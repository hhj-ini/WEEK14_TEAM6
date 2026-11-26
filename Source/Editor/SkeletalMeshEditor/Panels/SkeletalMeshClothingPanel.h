#pragma once

#include "SkeletalMeshEditorPanel.h"

// 언리얼 SkeletalMeshEditor의 Clothing 패널 (클로딩 데이터 목록, Cloth Painting 설정, NvCloth Config, 저장)
class FSkeletalMeshClothingPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FSkeletalMeshClothingPanel(FSkeletalMeshEditor& InEditor);

protected:
	void DrawContent() override;

private:
	int32 NewAssetSectionIndex = 0;
	char NewAssetNameBuf[128] = "Clothing_0";
};
