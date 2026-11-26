#pragma once

#include "UObject/Object.h"
#include "PhysicalMaterial.generated.h"

namespace physx { class PxMaterial; }

// 두 머티리얼이 닿았을 때 값을 합치는 방법. PxCombineMode와 순서·의미가 같다.
// 서로 다르면 PhysX는 우선순위가 높은 쪽(Average < Min < Multiply < Max)을 따른다.
UENUM()
enum class EPhysicsCombineMode : uint8
{
	Average,
	Min,
	Multiply,
	Max,
};

// 표면 물성 에셋(.pmat). PxMaterial 하나를 소유하고, 값을 바꾸면 PxMaterial에 바로 반영한다.
// 같은 머티리얼을 쓰는 모든 shape가 함께 바뀐다 (PhysX가 shape마다 복사하지 않고 참조한다).
UCLASS()
class UPhysicalMaterial : public UObject
{
	GENERATED_BODY()
public:
	UPhysicalMaterial() = default;
	virtual ~UPhysicalMaterial() override;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.01))
	float Friction = 0.7f;           // 미끄러지는 중의 마찰 (동마찰)
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.01))
	float StaticFriction = 0.7f;     // 멈춰 있다가 움직이기 시작할 때의 마찰 (정지마찰)
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	float Restitution = 0.3f;        // 반발. 0이면 튀지 않고, 1이면 거의 같은 속도로 튄다
	UPROPERTY(EditAnywhere, meta=(ClampMin=1, Delta=10))
	float Density = 1000.0f;         // kg/m³. 바디에 질량 오버라이드가 없으면 이 값으로 질량을 계산한다

	UPROPERTY(EditAnywhere)
	EPhysicsCombineMode FrictionCombineMode = EPhysicsCombineMode::Average;
	UPROPERTY(EditAnywhere)
	EPhysicsCombineMode RestitutionCombineMode = EPhysicsCombineMode::Average;

	// 처음 불릴 때 PxMaterial을 만든다. PhysX가 꺼져 있으면 nullptr.
	physx::PxMaterial* GetPxMaterial();

	virtual void PostEditChangeProperty(const FProperty& Property) override;

	// 에셋 파일 키. 디테일 창 표시와 씬 저장(참조를 경로로 저장)에 쓴다.
	void SetAssetPath(const FString& Path) { AssetPath = Path; }
	const FString& GetAssetPath() const { return AssetPath; }

private:
	void UpdatePxMaterial();

	physx::PxMaterial* PxMaterial = nullptr;   // 저장·복제하지 않는다
	FString AssetPath;
};
