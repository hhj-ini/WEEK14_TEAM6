#pragma once

#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "StaticMeshActor.generated.h"

UCLASS()
class AStaticMeshActor :public AActor
{
	GENERATED_BODY()
public:
	AStaticMeshActor();
	void Serialize(FStructuredArchive::FRecord Record) override;
	virtual ~AStaticMeshActor() = default;

	virtual void BeginPlay() override;

	UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }
	void SetPrimitiveType(EPrimitiveType Type);

private:
	UStaticMeshComponent* StaticMeshComponent;
};

