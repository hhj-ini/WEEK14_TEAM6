#pragma once

#include "Core/Types.h"
#include "Clothing/ClothingTypes.h"
#include "Clothing/ClothingAsset.h"

namespace nv
{
	namespace cloth
	{
		class Fabric;
		class Cloth;
		class Solver;
	}
}

class FClothingSimulationNv
{
public:
	FClothingSimulationNv() = default;
	~FClothingSimulationNv() { Shutdown(); }

	FClothingSimulationNv(const FClothingSimulationNv&) = delete;
	FClothingSimulationNv& operator=(const FClothingSimulationNv&) = delete;

	bool CreateCloth(const FClothPhysicalMeshData& MeshData);
	bool CreateClothFromAsset(const UClothingAssetCommon& Asset);
	void Shutdown();

	void UpdateSkeletalPose(const TArray<FMatrix>& RefToLocal, const FTransform& ComponentToWorld, bool bTeleport = false);
	void UpdateCollisions(const TArray<FVector4>& Spheres, const TArray<uint32>& CapsuleIndices);
	void Simulate(float DeltaTime);
	void GetSimulationData(FClothingSimulationData& OutData) const;

	void SetGravity(const FVector& InGravity);
	void SetWindVelocity(const FVector& InWindVelocity, float InDrag, float InLift);
	void ApplyConfig(const FClothConfigNv& InConfig);

	bool IsValid() const { return Cloth != nullptr && Solver != nullptr; }

private:
	nv::cloth::Fabric* Fabric = nullptr;
	nv::cloth::Cloth* Cloth = nullptr;
	nv::cloth::Solver* Solver = nullptr;

	FClothPhysicalMeshData CachedMeshData;
	FClothConfigNv CachedConfig;
	TArray<FVector> SkinnedPositions;
	bool bLocalMeshIsCm = false;
};
