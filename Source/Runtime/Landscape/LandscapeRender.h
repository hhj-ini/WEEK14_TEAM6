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
	void BuildHeightTexture(const ULandscapeComponent& Component, const FTerrainData& Data);

	TSharedPtr<FLandscapeSharedBuffers> SharedBuffers;   // 액터보다 오래 살아도 안전하도록 공유 소유
	TUniquePtr<FTexture2D> HeightTexture;                // 이 조각 전용 R16 텍스처
	FLandscapeVertexFactory VertexFactory;
	UMaterial* Material = nullptr;
	int32 BuiltBaseX = -1;
	int32 BuiltBaseY = -1;
};
