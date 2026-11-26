#include "EnginePCH.h"
#include "PhysXSimulationEventCallback.h"
#include "PhysScene.h"
#include "BodyInstance.h"
#include "PhysXConversions.h"

void FPhysXSimulationEventCallback::onContact(const physx::PxContactPairHeader& PairHeader, const physx::PxContactPair* Pairs, physx::PxU32 NumPairs)
{
	// 이번 스텝에 삭제된 액터가 낀 쌍은 건너뛴다 (userData가 이미 무효일 수 있다).
	if (PairHeader.flags & (physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_0 | physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
		return;

	const FBodyInstance* BodyA = FBodyInstance::FromActor(PairHeader.actors[0]);
	const FBodyInstance* BodyB = FBodyInstance::FromActor(PairHeader.actors[1]);
	if (!BodyA || !BodyB || !BodyA->OwnerComponent || !BodyB->OwnerComponent)
		return;

	for (physx::PxU32 PairIndex = 0; PairIndex < NumPairs; ++PairIndex)
	{
		const physx::PxContactPair& Pair = Pairs[PairIndex];
		if (!(Pair.events & physx::PxPairFlag::eNOTIFY_TOUCH_FOUND))
			continue;

		FCollisionNotifyInfo Info;
		Info.ComponentA = BodyA->OwnerComponent;
		Info.ComponentB = BodyB->OwnerComponent;

		// 접촉점 하나를 대표로 쓴다 (Filter Shader가 eNOTIFY_CONTACT_POINTS를 켜 둬야 채워진다).
		physx::PxContactPairPoint Points[4];
		const physx::PxU32 NumPoints = Pair.extractContacts(Points, 4);
		if (NumPoints > 0)
		{
			Info.ImpactPoint = P2KVector(Points[0].position);
			Info.Normal = P2KVector(Points[0].normal);
			for (physx::PxU32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
				Info.ImpulseSize += Points[PointIndex].impulse.magnitude();
		}
		Owner.AddCollisionNotify(Info);
		break;   // 액터 쌍마다 한 번만 알린다 (shape 쌍이 여러 개여도)
	}
}

void FPhysXSimulationEventCallback::onTrigger(physx::PxTriggerPair* Pairs, physx::PxU32 Count)
{
	for (physx::PxU32 Index = 0; Index < Count; ++Index)
	{
		const physx::PxTriggerPair& Pair = Pairs[Index];
		if (Pair.flags & (physx::PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | physx::PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
			continue;

		const FBodyInstance* Trigger = FBodyInstance::FromActor(Pair.triggerActor);
		const FBodyInstance* Other = FBodyInstance::FromActor(Pair.otherActor);
		if (!Trigger || !Other || !Trigger->OwnerComponent || !Other->OwnerComponent)
			continue;

		FTriggerNotifyInfo Info;
		Info.TriggerComponent = Trigger->OwnerComponent;
		Info.OtherComponent = Other->OwnerComponent;
		Info.bBegin = (Pair.status == physx::PxPairFlag::eNOTIFY_TOUCH_FOUND);
		Owner.AddTriggerNotify(Info);
	}
}
