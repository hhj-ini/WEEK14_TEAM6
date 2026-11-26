#pragma once

// PhysX 타입이 필요한 필터 선언. PhysXIncludes.h를 끌어오므로 .cpp에서만 include한다.
#include "PhysicsFiltering.h"
#include "PhysXIncludes.h"

physx::PxFilterData K2PFilterData(const FMaskFilter& Filter);
FMaskFilter P2KFilterData(const physx::PxFilterData& Data);

// PxSceneDesc::filterShader. 쌍이 새로 생길 때(또는 필터 데이터가 바뀌어 다시 판정할 때) 워커 스레드에서 불린다.
physx::PxFilterFlags KurisuSimulationFilterShader(
	physx::PxFilterObjectAttributes Attributes0, physx::PxFilterData Filter0,
	physx::PxFilterObjectAttributes Attributes1, physx::PxFilterData Filter1,
	physx::PxPairFlags& PairFlags, const void* ConstantBlock, physx::PxU32 ConstantBlockSize);
