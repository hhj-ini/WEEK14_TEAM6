#pragma once

#include "Core/Types.h"
#include "Core/NameTypes.h"
#include "Math/Vector.h"
#include "Core/EngineString.h"
#include "ConstraintInstance.generated.h"

namespace physx { class PxD6Joint; }

UENUM()
enum class EAngularConstraintMotion : uint8
{
	Free,   
	Limited,
	Locked,
};

struct FBodyInstance;

USTRUCT()
struct FConstraintInstance
{
	GENERATED_BODY()


	FConstraintInstance() = default;
	~FConstraintInstance();
	FConstraintInstance(const FConstraintInstance&) = delete;
	FConstraintInstance& operator=(const FConstraintInstance&) = delete;

	bool InitConstraint(FBodyInstance* Body1, FBodyInstance* Body2);

	void TermConstraint();
	bool IsValidConstraintInstance() const { return ConstraintData != nullptr; }

	FName ConstraintBone1;   // 자식 본
	FName ConstraintBone2;   // 부모 본 (바디가 있는 가장 가까운 조상)

	UPROPERTY()
	FVector Pos1 = FVector(0, 0, 0);
	UPROPERTY()
	FVector PriAxis1 = FVector(1, 0, 0);
	UPROPERTY()
	FVector SecAxis1 = FVector(0, 1, 0);
	UPROPERTY()
	FVector Pos2 = FVector(0, 0, 0);
	UPROPERTY()
	FVector PriAxis2 = FVector(1, 0, 0);
	UPROPERTY()
	FVector SecAxis2 = FVector(0, 1, 0);

	UPROPERTY(EditAnywhere)
	EAngularConstraintMotion Swing1Motion = EAngularConstraintMotion::Limited;
	UPROPERTY(EditAnywhere)
	EAngularConstraintMotion Swing2Motion = EAngularConstraintMotion::Limited;
	UPROPERTY(EditAnywhere)
	EAngularConstraintMotion TwistMotion = EAngularConstraintMotion::Limited;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0, ClampMax = 179, Delta = 1))
	float Swing1LimitDegrees = 45.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = 0, ClampMax = 179, Delta = 1))
	float Swing2LimitDegrees = 45.0f;
	UPROPERTY(EditAnywhere, meta = (ClampMin = 0, ClampMax = 179, Delta = 1))
	float TwistLimitDegrees = 45.0f;   // ±이 값

	physx::PxD6Joint* ConstraintData = nullptr;

	void CopyConstraintParamsFrom(const FConstraintInstance& From);

	// 조인트의 부모 쪽·자식 쪽 앵커 월드 위치. 둘이 벌어져 있으면 조인트가 늘어난 것이다 (디버그용)
	bool GetJointWorldPositions(FVector& OutParent, FVector& OutChild) const;
};