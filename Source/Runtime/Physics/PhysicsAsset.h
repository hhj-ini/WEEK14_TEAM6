#pragma once

#include "UObject/Object.h"
#include "Core/NameTypes.h"
#include "BodySetup.h"
#include "Containers/Array.h"
#include "PhysicsConstraintTemplate.h"
#include "PhysicsAsset.generated.h"

struct FPhysicsAssetCollisionPair
{
	FName BoneA;
	FName BoneB;

	bool Matches(FName A, FName B) const
	{
		return (BoneA == A && BoneB == B) || (BoneA == B && BoneB == A);
	}
};

UCLASS()
class UPhysicsAsset : public UObject
{
	GENERATED_BODY()

public:
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	TArray<UBodySetup*> BodySetup;

	TArray<UPhysicsConstraintTemplate*> ConstraintSetup;

	int32 FindBodyIndex(FName BoneName) const;
	UBodySetup* FindBodySetup(FName BoneName) const;

	int32 FindConstraintIndex(FName ChildBone) const;
	UPhysicsConstraintTemplate* AddConstraint(FName ChildBone, FName ParentBone);
	void ClearAllConstraints();

	UBodySetup* AddBodySetup(FName BoneName);
	void RemoveBodySetup(FName BoneName);
	void ClearAllBodies();

	void SetAssetPath(const FString& Path) { AssetPath = Path; }
	const FString& GetAssetPath() const { return AssetPath; }

	TArray<FPhysicsAssetCollisionPair> CollisionDisableTable;

	void DisableCollision(FName BoneA, FName BoneB);
	void EnableCollision(FName BoneA, FName BoneB);

	bool IsCollisionEnabled(FName BoneA, FName BoneB) const;

private:
	FString AssetPath;
};