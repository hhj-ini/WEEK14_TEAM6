#pragma once

#include "PhysXIncludes.h"

class FPhysXSimulationFilterCallback : public physx::PxSimulationFilterCallback
{
public:
	physx::PxFilterFlags pairFound(physx::PxU32 PairID,
		physx::PxFilterObjectAttributes Attributes0, physx::PxFilterData Filter0, const physx::PxActor* A0, const physx::PxShape* S0,
		physx::PxFilterObjectAttributes Attributes1, physx::PxFilterData Filter1, const physx::PxActor* A1, const physx::PxShape* S1,
		physx::PxPairFlags& PairFlags) override;

	// eNOTIFY를 쓰지 않으므로 추적할 쌍이 없다
	void pairLost(physx::PxU32, physx::PxFilterObjectAttributes, physx::PxFilterData,
		physx::PxFilterObjectAttributes, physx::PxFilterData, bool) override { }

	virtual bool statusChange(physx::PxU32&, physx::PxPairFlags&, physx::PxFilterFlags&) override { return false; }
};