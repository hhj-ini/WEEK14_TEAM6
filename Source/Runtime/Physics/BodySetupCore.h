#pragma once

#include "UObject/Object.h"
#include "Core/NameTypes.h"
#include "BodySetupCore.generated.h"

// 이 Body를 물리 시뮬레이션으로 움직일 지, 애니메이션을 따라가게 할지 설정
UENUM()
enum class EPhysicsType : uint8
{
	Default,	// 컴포넌트 설정을 따름
	Kinematic,	// 항상 애니메이션 포즈를 따라간다
	Simulated,	// 항상 시뮬레이션 한다
};


UCLASS()
class UBodySetupCore : public UObject
{
	GENERATED_BODY()
public:

	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	FName BoneName;            // PhysicsAsset에서 이 바디가 붙는 본. StaticMesh에서는 비워 둔다.

	UPROPERTY(EditAnywhere)
	EPhysicsType PhysicsType = EPhysicsType::Default;
};