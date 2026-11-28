#include "EnginePCH.h"
#include "Landscape/LandscapeComponent.h"
#include "Landscape/LandscapeActor.h"
#include "Landscape/LandscapeRender.h"
#include "Landscape/LandscapeSharedBuffers.h"
#include "Asset/AssetManager.h"

ALandscapeActor* ULandscapeComponent::GetLandscapeActor() const
{
	return Cast<ALandscapeActor>(GetOwner());
}

FPrimitiveSceneProxy* ULandscapeComponent::CreateSceneProxy()
{
	return new FLandscapeComponentSceneProxy(this);
}

FBox ULandscapeComponent::CalcLocalBounds() const
{
	// 로컬 공간 상자. 월드 트랜스폼(스케일 포함)을 거치면 월드 AABB가 된다
	const float Q = static_cast<float>(LandscapeComponentQuads);
	return FBox{ FVector(0.0f, 0.0f, HeightMin01), FVector(Q, Q, HeightMax01) };
}

UMaterial* ULandscapeComponent::GetLandscapeMaterial() const
{
	if (ALandscapeActor* Actor = GetLandscapeActor(); Actor && Actor->LandscapeMaterial)
	{
		return Actor->LandscapeMaterial;
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}
