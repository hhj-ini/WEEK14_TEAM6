#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"

class UPhysicsAsset;
class USkeletalMesh;

enum class EPhysAssetFitVertWeight : uint8
{
	AnyWeight,
	DominantWeight,
};

struct FPhysAssetCreateParams
{
	float MinBoneSizeRatio = 0.05f;

	int32 MinVertsPreBody = 4;

	EPhysAssetFitVertWeight VertWeight = EPhysAssetFitVertWeight::DominantWeight;
};

namespace FPhysicsAssetUtils
{
	bool CreateFromSkeletalMesh(UPhysicsAsset& Asset, const USkeletalMesh& Mesh,
		const FPhysAssetCreateParams& Params, FString& OutError);
}