#pragma once

#include "Engine/PrimitiveSceneProxy.h"
#include "Landscape/LandscapeVertexFactory.h"

class ULandscapeComponent;
class FLandscapeSharedBuffers;
class FTexture2D;
class UMaterial;
struct FTerrainData;

// ULandscapeComponent의 렌더 쪽 짝. UE FLandscapeComponentSceneProxy 대응.
// 조각 하이트맵 텍스처를 만들어 들고, 매 프레임 드로우 1개를 낸다.
class FLandscapeComponentSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FLandscapeComponentSceneProxy(ULandscapeComponent* InComponent);
	~FLandscapeComponentSceneProxy() override;

	void UpdateRenderState() override;
	bool HasDynamicMeshElements() const override { return true; }
	void GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View) override;

private:
	// 64x64 BGRA8 + 밉 5장 (UE 배치). R·G = 높이 16bit, B·A = 구워 둔 월드 법선 X·Y
	void BuildHeightmapTexture(const ULandscapeComponent& Component, const FTerrainData& Data);

	TSharedPtr<FLandscapeSharedBuffers> SharedBuffers;   // 액터보다 오래 살아도 안전하도록 공유 소유
	TUniquePtr<FTexture2D> HeightTexture;                // 이 조각 전용 하이트맵 (밉 6단계)
	FLandscapeVertexFactory VertexFactory;
	UMaterial* Material = nullptr;
	int32 BuiltBaseX = -1;
	int32 BuiltBaseY = -1;
	float BuiltCellSize = 0.0f;      // 법선을 구울 때 쓴 값. 바뀌면 다시 굽는다
	float BuiltHeightScale = 0.0f;
};
