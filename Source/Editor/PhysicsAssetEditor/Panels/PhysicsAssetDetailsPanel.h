#pragma once

#include "PhysicsAssetEditorPanel.h"
#include "Containers/Array.h"
#include "Math/Transform.h"

class UBodySetup;
class UPhysicsConstraintTemplate;

// 선택한 바디 또는 조인트의 속성. 엔진에 있는 값만 보여 준다.
class FPhysicsAssetDetailsPanel : public FPhysicsAssetEditorPanel
{
public:
	explicit FPhysicsAssetDetailsPanel(FPhysicsAssetEditor& InEditor);

protected:
	void DrawContent() override;

private:
	bool DrawBodyDetails(UBodySetup& Body);
	bool DrawPrimitives(UBodySetup& Body);
	bool DrawConstraintDetails(UPhysicsConstraintTemplate& Template);
	// 같은 종류를 여러 개 골랐을 때: 공통 속성만 보여 주고, 바꾸면 고른 것 모두에 넣는다. 값이 다르면 "Multiple Values"
	bool DrawMultiBodyDetails(const TArray<UBodySetup*>& Bodies);
	bool DrawMultiConstraintDetails(const TArray<UPhysicsConstraintTemplate*>& Templates);

	// 쿼터니언은 입력한 오일러 값을 기억하지 못하므로 칸마다 마지막 변환 쌍을 보관한다
	FRotationConversionCache ChildFrameRotation;
	FRotationConversionCache ParentFrameRotation;
	TArray<FRotationConversionCache> PrimitiveRotations;

	// 선택이 바뀌면 위 캐시를 비운다
	int32 LastSelectionType = -1;
	int32 LastSelectionIndex = -1;
};
