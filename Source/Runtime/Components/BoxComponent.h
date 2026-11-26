#pragma once

#include "ShapeComponent.h"
#include "BoxComponent.generated.h"

UCLASS()
class UBoxComponent : public UShapeComponent
{
	GENERATED_BODY()
public:
	// 로컬 공간 반 크기. 월드 크기는 GetScaledBoxExtent
	UPROPERTY(EditAnywhere)
	FVector BoxExtent = FVector(0.5f, 0.5f, 0.5f);

	FVector GetScaledBoxExtent() const;
	virtual FCollisionShape GetCollisionShape() const override { return FCollisionShape::MakeBox(GetScaledBoxExtent()); }
	virtual FBox CalcLocalBounds() const override;
	virtual void DrawDebug(FLineBatcher*, const FVector4&) const override;

protected:
	virtual void UpdateBodySetup() override;
};
