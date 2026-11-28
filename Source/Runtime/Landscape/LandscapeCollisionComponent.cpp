#include "EnginePCH.h"
#include "Landscape/LandscapeCollisionComponent.h"

#include "Landscape/Landscape.h"
#include "Terrain/TerrainData.h"
#include "Engine/World.h"
#include "Physics/PhysScene.h"

ALandscape* ULandscapeCollisionComponent::GetLandscape() const
{
	return Cast<ALandscape>(GetOwner());
}

void ULandscapeCollisionComponent::RecreateCollision()
{
	DestroyPhysicsState();
	CreatePhysicsState();
}

bool ULandscapeCollisionComponent::ShouldCreatePhysicsState() const
{
	if (BodyInstance.CollisionEnabled == ECollisionEnabled::NoCollision)
		return false;

	UWorld* World = GetWorld();
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	if (!Scene || !Scene->IsValid())
		return false;

	const ALandscape* Landscape = GetLandscape();
	if (!Landscape)
		return false;

	const FTerrainData& Data = Landscape->GetTerrainData();
	return Data.SizeX >= 2 && Data.SizeY >= 2;
}

void ULandscapeCollisionComponent::OnCreatePhysicsState()
{
	UActorComponent::OnCreatePhysicsState();

	const ALandscape* Landscape = GetLandscape();
	if (!Landscape)
		return;

	BodyInstance.ObjectType = ECollisionCategory::WorldStatic;
	BodyInstance.bDrivable = true;

	LandscapeCollision.Create(Landscape->GetTerrainData(), BodyInstance, this, GetWorld()->GetPhysicsScene());
}

void ULandscapeCollisionComponent::OnDestroyPhysicsState()
{
	LandscapeCollision.Destroy(BodyInstance);   // BodyInstance.TermBody + PxHeightField 해제
	ClearComponentOverlaps();
	UActorComponent::OnDestroyPhysicsState();
}
