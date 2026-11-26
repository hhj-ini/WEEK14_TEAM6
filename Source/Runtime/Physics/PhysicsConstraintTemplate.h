#pragma once

#include "UObject/Object.h"
#include "ConstraintInstance.h"
#include "PhysicsConstraintTemplate.generated.h"

UCLASS()
class UPhysicsConstraintTemplate : public UObject
{
	GENERATED_BODY()
public:
	
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	UPROPERTY(EditAnywhere)
	FConstraintInstance DefaultInstance;
};