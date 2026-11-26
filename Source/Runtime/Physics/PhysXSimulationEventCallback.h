#pragma once

// PhysX 타입을 상속하므로 PhysXIncludes.h를 끌어온다. .cpp에서만 include한다.
#include "PhysXIncludes.h"

class FPhysScene;

// fetchResults 안에서 불린다. 여기서 씬을 바꾸거나(액터 추가·삭제, setGlobalPose) 엔진 이벤트를 바로 보내면 안 된다.
// 정보만 FPhysScene의 큐에 쌓고, 디스패치는 fetchResults가 끝난 뒤 FPhysScene이 한다.
class FPhysXSimulationEventCallback : public physx::PxSimulationEventCallback
{
public:
	explicit FPhysXSimulationEventCallback(FPhysScene& InOwner) : Owner(InOwner) {}

	void onContact(const physx::PxContactPairHeader& PairHeader, const physx::PxContactPair* Pairs, physx::PxU32 NumPairs) override;
	void onTrigger(physx::PxTriggerPair* Pairs, physx::PxU32 Count) override;

	void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}
	void onWake(physx::PxActor**, physx::PxU32) override {}
	void onSleep(physx::PxActor**, physx::PxU32) override {}
	void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, const physx::PxU32) override {}

private:
	FPhysScene& Owner;
};
