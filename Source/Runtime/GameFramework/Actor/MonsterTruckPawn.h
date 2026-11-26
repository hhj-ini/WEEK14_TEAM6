#pragma once

#include "Components/ActorComponent.h"
#include "GameFramework/Pawn.h"
#include "MonsterTruckPawn.generated.h"

class UBoxComponent;
class USpringArmComponent;
class UStaticMeshComponent;
struct FMonsterTruckPhysics;

UCLASS()
class UMonsterTruckDriveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMonsterTruckDriveComponent();
	void TickComponent(float DeltaTime) override;
};

UCLASS()
class AMonsterTruckPawn : public APawn
{
	GENERATED_BODY()

public:
	AMonsterTruckPawn();
	~AMonsterTruckPawn() override;
	void BeginPlay() override;
	void EndPlay() override;
	void Tick(float DeltaTime) override;
	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;
	FRotator GetMovementInputRotation() const override { return FRotator::Identitiy; }

private:
	friend class UMonsterTruckDriveComponent;

	bool SetupVehicle();
	void UpdateVehicle(float DeltaTime);

	UBoxComponent* Chassis = nullptr;
	USceneComponent* Presentation = nullptr;
	UStaticMeshComponent* Wheels[4] = {};
	UStaticMeshComponent* Axles[2] = {};
	USpringArmComponent* CameraArm = nullptr;
	TUniquePtr<FMonsterTruckPhysics> Physics;
	FTransform SpawnPose;
	float Steering = 0.0f;
	bool bResetKeyDown = false;
	bool bResetCameraLag = false;
};
