#include "EnginePCH.h"
#include "Landscape/LandscapeComponent.h"
#include "Landscape/Landscape.h"
#include "Landscape/LandscapeRender.h"
#include "Landscape/LandscapeSharedBuffers.h"

ALandscape* ULandscapeComponent::GetLandscape() const
{
	return Cast<ALandscape>(GetOwner());
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
