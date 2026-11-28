#pragma once

#include "Components/PrimitiveComponent.h"
#include "Landscape/LandscapeCollision.h"
#include "LandscapeCollisionComponent.generated.h"

class ALandscapeActor;

UCLASS()
class ULandscapeCollisionComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	ALandscapeActor* GetLandscape() const;

	void RecreateCollision();
	bool HasCollision() const { return LandscapeCollision.IsValid(); }

protected:
	bool ShouldCreatePhysicsState() const override;
	void OnCreatePhysicsState() override;
	void OnDestroyPhysicsState() override;

	bool ShouldCreateRenderState() const override { return false; }

private:
	FLandscapeCollision LandscapeCollision;
};
