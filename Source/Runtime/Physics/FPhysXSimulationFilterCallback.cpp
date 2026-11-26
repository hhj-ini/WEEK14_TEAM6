#include "EnginePCH.h"
#include "FPhysXSimulationFilterCallback.h"
#include "BodyInstance.h"
#include "Components/PrimitiveComponent.h"

physx::PxFilterFlags FPhysXSimulationFilterCallback::pairFound(physx::PxU32 PairID, physx::PxFilterObjectAttributes Attributes0, physx::PxFilterData Filter0, const physx::PxActor* A0, const physx::PxShape* S0, physx::PxFilterObjectAttributes Attributes1, physx::PxFilterData Filter1, const physx::PxActor* A1, const physx::PxShape* S1, physx::PxPairFlags& PairFlags)
{
	const FBodyInstance* Body0 = A0 ? FBodyInstance::FromActor(A0->is<physx::PxRigidActor>()) : nullptr;
	const FBodyInstance* Body1 = A1 ? FBodyInstance::FromActor(A1->is<physx::PxRigidActor>()) : nullptr;

	// 판단할 수 없으면 기존 규칙(같은 OwnerId → 충돌 안 함)을 따른다
	if (!Body0 || !Body1 || !Body0->OwnerComponent || Body0->OwnerComponent != Body1->OwnerComponent)
		return physx::PxFilterFlag::eSUPPRESS;

	// eSUPPRESS: 쌍은 유지하되 충돌하지 않는다. 필터 데이터가 바뀌면(resetFiltering) 다시 판정된다
	return Body0->OwnerComponent->CanBodiesCollide(*Body0, *Body1)
		? physx::PxFilterFlags(physx::PxFilterFlag::eDEFAULT)
		: physx::PxFilterFlags(physx::PxFilterFlag::eSUPPRESS);
}
