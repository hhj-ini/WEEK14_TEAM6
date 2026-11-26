#include "EnginePCH.h"
#include "PhysicsFiltering.h"
#include "PhysXFilterShader.h"

physx::PxFilterData K2PFilterData(const FMaskFilter& Filter)
{
	return physx::PxFilterData(Filter.Category, Filter.CollidesWith, Filter.OwnerId, Filter.Flags);
}

FMaskFilter P2KFilterData(const physx::PxFilterData& Data)
{
	return FMaskFilter{ Data.word0, Data.word1, Data.word2, Data.word3 };
}

// 전역 상태·userData·엔진 객체를 건드리면 안 된다. FilterData만 보고 판단한다.
physx::PxFilterFlags KurisuSimulationFilterShader(
	physx::PxFilterObjectAttributes Attributes0, physx::PxFilterData Filter0,
	physx::PxFilterObjectAttributes Attributes1, physx::PxFilterData Filter1,
	physx::PxPairFlags& PairFlags, const void* /*ConstantBlock*/, physx::PxU32 /*ConstantBlockSize*/)
{
	// 양쪽 모두 상대 종류를 받아 줄 때만 충돌한다.
	if ((Filter0.word0 & Filter1.word1) == 0 || (Filter1.word0 & Filter0.word1) == 0)
		return physx::PxFilterFlag::eSUPPRESS;

	// 같은 랙돌(소유자)의 본끼리. 셰이더는 userData·에셋을 볼 수 없으므로 판정을 필터 콜백에 넘긴다.
	// 콜백이 eDEFAULT를 돌려주면 이 PairFlags로 충돌한다.
	if (Filter0.word2 != 0 && Filter0.word2 == Filter1.word2)
	{
		PairFlags = physx::PxPairFlag::eCONTACT_DEFAULT;   // 같은 랙돌끼리는 접촉 알림(onContact)이 필요 없다
		return physx::PxFilterFlag::eCALLBACK;
	}

	// 트리거는 접촉 해석 없이 들어옴/나감만 알린다.
	// 트리거 쌍은 Kinematic·Static 상대도 이 셰이더까지 온다. 트리거끼리는 PhysX가 먼저 버린다.
	if (physx::PxFilterObjectIsTrigger(Attributes0) || physx::PxFilterObjectIsTrigger(Attributes1))
	{
		PairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT;
		return physx::PxFilterFlag::eDEFAULT;
	}

	PairFlags = physx::PxPairFlag::eCONTACT_DEFAULT
		| physx::PxPairFlag::eDETECT_CCD_CONTACT      // CCD는 바디 플래그(A9)가 켜진 경우에만 실제로 동작한다
		| physx::PxPairFlag::eNOTIFY_TOUCH_FOUND      // A8 onContact용
		| physx::PxPairFlag::eNOTIFY_CONTACT_POINTS;
	return physx::PxFilterFlag::eDEFAULT;
}
