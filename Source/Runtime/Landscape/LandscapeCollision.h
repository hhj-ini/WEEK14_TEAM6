#pragma once

// PhysX 헤더를 끌어오지 않도록 전방 선언만 둔다. (EnginePCH는 .cpp 첫 줄에서만 include한다)
namespace physx
{
	class PxHeightField;
}

struct FTerrainData;
struct FBodyInstance;
class UPrimitiveComponent;
class FPhysScene;

class FLandscapeCollision
{
public:
	FLandscapeCollision() = default;
	~FLandscapeCollision() = default;

	FLandscapeCollision(const FLandscapeCollision&) = delete;
	FLandscapeCollision& operator=(const FLandscapeCollision&) = delete;

	bool Create(const FTerrainData& Data, FBodyInstance& Body, UPrimitiveComponent* Owner, FPhysScene* Scene);
	void Destroy(FBodyInstance& Body);

	bool IsValid() const { return HeightField != nullptr; }
	physx::PxHeightField* GetHeightField() const { return HeightField; }

private:
	physx::PxHeightField* HeightField = nullptr;
};
