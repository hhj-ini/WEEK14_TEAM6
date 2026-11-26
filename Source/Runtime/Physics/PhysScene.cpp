#include "EnginePCH.h"
#include "PhysScene.h"
#include "PhysicsCore.h"
#include "PhysXIncludes.h"
#include "BodyInstance.h"
#include "PhysXConversions.h"
#include "PhysXFilterShader.h"
#include "PhysXSimulationEventCallback.h"
#include "PhysicsLock.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "FPhysXSimulationFilterCallback.h"
#include "PhysicsStats.h"

namespace
{
	// 쿼리 후보 shape마다 불린다. 종류·플래그·제외 컴포넌트를 한곳에서 거른다.
	class FKurisuQueryFilterCallback : public physx::PxQueryFilterCallback
	{
	public:
		// bInIgnoreTriggers: 막힘을 찾는 쿼리(Raycast·Sweep)는 트리거를 지나친다. 트리거는 겹침만 알리고 막지 않는다.
		FKurisuQueryFilterCallback(const FCollisionQueryParams& InParams, bool bInIgnoreTriggers, const physx::PxVec3& InSweepDir = physx::PxVec3(0.0f))
			: Params(InParams), SweepDir(InSweepDir), bIgnoreTriggers(bInIgnoreTriggers) {}

		physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData& /*QueryData*/, const physx::PxShape* Shape,
			const physx::PxRigidActor* Actor, physx::PxHitFlags& /*Flags*/) override
		{
			if (bIgnoreTriggers && (Shape->getFlags() & physx::PxShapeFlag::eTRIGGER_SHAPE))
				return physx::PxQueryHitType::eNONE;

			const physx::PxFilterData ShapeData = Shape->getQueryFilterData();
			if ((ShapeData.word0 & Params.ObjectTypesMask) == 0)
				return physx::PxQueryHitType::eNONE;
			if ((ShapeData.word3 & Params.RequiredFlags) != Params.RequiredFlags)
				return physx::PxQueryHitType::eNONE;

			const FBodyInstance* Body = FBodyInstance::FromActor(Actor);
			const UPrimitiveComponent* Component = Body ? Body->OwnerComponent : nullptr;
			if (Component)
			{
				if (Params.IgnoreComponent && Component == Params.IgnoreComponent)
					return physx::PxQueryHitType::eNONE;
				const AActor* Owner = Component->GetOwner();
				if (Params.IgnoreActor && Owner == Params.IgnoreActor)
					return physx::PxQueryHitType::eNONE;
				// 액터 단위로 충돌을 끈 경우(SetActorEnableCollision(false))도 쿼리에서 뺀다.
				if (Owner && !Owner->GetActorEnableCollision())
					return physx::PxQueryHitType::eNONE;
			}
			return physx::PxQueryHitType::eBLOCK;
		}

		// bIgnoreTouchesMovingAway일 때만 불린다 (ePOSTFILTER). 시작부터 닿아 있던 면이 이동 방향을 막지 않으면 버린다.
		physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&, const physx::PxQueryHit& Hit) override
		{
			const physx::PxSweepHit& SweepHit = static_cast<const physx::PxSweepHit&>(Hit);
			// eMTD를 켰으므로 시작 겹침이면 normal은 빠져나가는 방향이다. 이동 방향과 마주보지 않으면(>= 0) 막지 않는다.
			if (SweepHit.hadInitialOverlap() && SweepHit.normal.dot(SweepDir) >= 0.0f)
				return physx::PxQueryHitType::eNONE;
			return physx::PxQueryHitType::eBLOCK;
		}

	private:
		const FCollisionQueryParams& Params;
		physx::PxVec3 SweepDir;
		bool bIgnoreTriggers;
	};

	physx::PxQueryFilterData MakeQueryFilterData()
	{
		// 정적·동적 둘 다 보고, 거르기는 preFilter 콜백이 한다.
		return physx::PxQueryFilterData(physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER);
	}

	// 모양 하나와 그 회전을 함께 만든다. 캡슐만 축 보정이 붙는다.
	physx::PxGeometryHolder MakeGeometry(const FCollisionShape& Shape, physx::PxQuat& InOutRotation)
	{
		switch (Shape.Type)
		{
		case FCollisionShape::EType::Box:
			return physx::PxBoxGeometry(K2PVector(Shape.HalfExtent));
		case FCollisionShape::EType::Capsule:
			InOutRotation = InOutRotation * CapsuleAxisFix();
			return physx::PxCapsuleGeometry(Shape.Radius, std::max(Shape.HalfHeight - Shape.Radius, 0.0f));
		default:
			return physx::PxSphereGeometry(Shape.Radius);
		}
	}

	void FillHitResult(const physx::PxLocationHit& Hit, const FVector& Start, const FVector& End, const FVector& Dir, FHitResult& OutHit)
	{
		const float MaxDistance = (End - Start).Size();
		OutHit = FHitResult();
		OutHit.TraceStart = Start;
		OutHit.TraceEnd = End;
		OutHit.bBlockingHit = true;
		OutHit.bStartPenetrating = Hit.hadInitialOverlap();
		// MTD를 켠 스윕이 시작부터 겹쳐 있으면 distance는 -(겹친 깊이)다.
		const float HitDistance = std::max(Hit.distance, 0.0f);
		OutHit.Distance = HitDistance;
		OutHit.Time = MaxDistance > 0.0f ? HitDistance / MaxDistance : 0.0f;
		OutHit.ImpactPoint = P2KVector(Hit.position);
		OutHit.ImpactNormal = P2KVector(Hit.normal);
		OutHit.Location = Start + Dir * HitDistance;   // 레이는 ImpactPoint와 같고, 스윕은 모양 중심 위치
		OutHit.Normal = OutHit.ImpactNormal;
		OutHit.PenetrationDepth = Hit.distance < 0.0f ? -Hit.distance : 0.0f;
		OutHit.FaceIndex = static_cast<int32>(Hit.faceIndex);
		const FBodyInstance* Body = FBodyInstance::FromActor(Hit.actor);
		OutHit.HitComponent = Body ? Body->OwnerComponent : nullptr;
	}
}

FPhysScene::FPhysScene(UWorld* InOwningWorld)
	: OwningWorld(InOwningWorld)
{
	if (!FPhysicsCore::Get().IsInitialized())
	{
		KRS_LOG(Error, "[Physics] FPhysScene created before FPhysicsCore initialized. Physics disabled.");
		return;
	}

	physx::PxSceneDesc Desc(FPhysicsCore::Get().GetPhysics()->getTolerancesScale());
	Desc.gravity = physx::PxVec3(0, 0, -9.81f);
	Desc.cpuDispatcher = FPhysicsCore::Get().GetDispatcher();
	Desc.filterShader = KurisuSimulationFilterShader;
	Desc.flags	|= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS 
				| physx::PxSceneFlag::eENABLE_PCM
				| physx::PxSceneFlag::eENABLE_CCD
				| physx::PxSceneFlag::eREQUIRE_RW_LOCK;
	EventCallback = MakeUnique<FPhysXSimulationEventCallback>(*this);
	Desc.simulationEventCallback = EventCallback.get();
	FilterCallback = MakeUnique<FPhysXSimulationFilterCallback>();
	Desc.filterCallback = FilterCallback.get();

	if (!Desc.isValid())
	{
		KRS_LOG(Error, "[Physics] FPhysScene PxSceneDesc is invalid. Physics disabled.");
		return;
	}

	Scene = FPhysicsCore::Get().GetPhysics()->createScene(Desc);
	if (!Scene)
	{
		KRS_LOG(Error, "[Physics] FPhysScene PxScene creation failed. Physics disabled.");
		return;
	}

	Scene->userData = this;

	{
		SCOPED_SCENE_WRITE_LOCK(Scene);
		if (physx::PxPvdSceneClient* PvdClient = Scene->getScenePvdClient())
		{
			PvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
			PvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
			PvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
		}
	}

	FPhysicsCore::Get().OnSceneCreated();
}

FPhysScene::~FPhysScene()
{
	if(!Scene)
		return;

	{
		// release() 전에 락을 풀어야 하므로 블록으로 감싼다.
		SCOPED_SCENE_READ_LOCK(Scene);
		const physx::PxU32 NumActors = Scene->getNbActors(physx::PxActorTypeFlag::eRIGID_STATIC | physx::PxActorTypeFlag::eRIGID_DYNAMIC);
		if (NumActors != 0)
		{
			KRS_LOG(Warning, "[Physics] FPhysScene destroyed with {} actors still in the scene.", NumActors);
		}
	}

	Scene->release();
	Scene = nullptr;
	FPhysicsCore::Get().OnSceneDestroyed();
}

void FPhysScene::SetGravity(const FVector& Gravity)
{
	if (!Scene)
		return;
	SCOPED_SCENE_WRITE_LOCK(Scene);
	Scene->setGravity(K2PVector(Gravity));
}

FVector FPhysScene::GetGravity() const
{
	return Scene ? P2KVector(Scene->getGravity()) : FVector(0.0f, 0.0f, 0.0f);
}

void FPhysScene::Tick(float DeltaTime)
{
	if (!Scene || DeltaTime <= 0.0f)
		return;

	TimeAccumulator += DeltaTime;

	int32 Steps = 0;
	{
		SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysSimulate);
		while (TimeAccumulator >= FixedTimeStep && Steps < MaxSubSteps)
		{
			// 동기 실행: simulate가 워커 스레드에 일을 나누고, fetchResults(true)가 끝날 때까지 기다린다.
			// 이 둘 사이에는 씬을 건드리는 API(InitBody, setGlobalPose 등)를 부르면 안 된다.
			// eREQUIRE_RW_LOCK 씬에서는 simulate·fetchResults도 쓰기 호출이라 쓰기 락이 필요하다.
			// fetchResults 안에서 불리는 이벤트 콜백은 이 락 안에서 돌므로 콜백에서 락을 다시 잡지 않는다.
			{
				SCOPED_SCENE_WRITE_LOCK(Scene);
				OnPreSimulate.Broadcast(FixedTimeStep);
				Scene->simulate(FixedTimeStep);
				Scene->fetchResults(true);
			}
			TimeAccumulator -= FixedTimeStep;
			++Steps;
		}

	}

	// 브레이크포인트 등으로 몇 초가 한꺼번에 들어오면 따라잡지 않고 버린다 (무한히 밀리는 것 방지).
	if (Steps == MaxSubSteps)
		TimeAccumulator = 0.0f;

	if (Steps > 0)
	{
		{
			SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysSyncActors);
			SyncActiveActors();
		}
		{
			SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysNotifies);
			DispatchPendingNotifies();
		}
	}

	// simulate 중 워커 스레드에서 쌓인 PhysX 로그를 메인 스레드에서 내보낸다.
	FPhysicsCore::Get().FlushPendingLogs();
}

void FPhysScene::SyncActiveActors()
{
	// 읽기 락 안에서 결과만 모으고, 컴포넌트 갱신은 락을 푼 뒤 한다.
	// 컴포넌트를 옮기면 자식 컴포넌트의 OnTransformDirty가 Kinematic 바디에 쓰기 락을 잡는데,
	// 읽기 락 안에서 쓰기 락을 잡으면 데드락이다.
	struct FPendingPose
	{
		UPrimitiveComponent* Component;
		physx::PxTransform Pose;
	};
	TArray<FPendingPose> Poses;
	{
		SCOPED_SCENE_READ_LOCK(Scene);
		// eENABLE_ACTIVE_ACTORS 덕분에 이번 스텝에 움직인(깨어 있는) 액터만 돌아온다. 잠든 바디는 건너뛴다.
		physx::PxU32 NumActive = 0;
		physx::PxActor** ActiveActors = Scene->getActiveActors(NumActive);

		for (physx::PxU32 Index = 0; Index < NumActive; ++Index)
		{
			physx::PxRigidActor* RigidActor = ActiveActors[Index]->is<physx::PxRigidActor>();
			FBodyInstance* Body = FBodyInstance::FromActor(RigidActor);
			if (!Body || !Body->OwnerComponent || !Body->IsSimulatingPhysics())
				continue;   // Kinematic은 컴포넌트가 주인이다. 물리 결과로 덮어쓰지 않는다

			if (Body->InstanceBoneIndex >= 0)
			{
				continue;
			}

			Poses.Add(FPendingPose{ Body->OwnerComponent, RigidActor->getGlobalPose() });
		}
	}

	for (const FPendingPose& Pending : Poses)
	{
		Pending.Component->SetWorldLocationAndRotation(P2KVector(Pending.Pose.p), P2KQuat(Pending.Pose.q).ToFRotator(), false);
	}
}

bool FPhysScene::RaycastSingle(const FVector& Start, const FVector& End, FHitResult& OutHit, const FCollisionQueryParams& Params) const
{
	const FVector Delta = End - Start;
	const float Distance = Delta.Size();
	if (!Scene || Distance <= 0.0f)
		return false;
	const FVector Dir = Delta / Distance;

	SCOPED_SCENE_READ_LOCK(Scene);

	FKurisuQueryFilterCallback Filter(Params, true);
	physx::PxRaycastBuffer Buffer;
	const bool bHit = Scene->raycast(K2PVector(Start), K2PVector(Dir), Distance, Buffer,
		physx::PxHitFlag::eDEFAULT, MakeQueryFilterData(), &Filter);
	if (!bHit || !Buffer.hasBlock)
		return false;

	FillHitResult(Buffer.block, Start, End, Dir, OutHit);
	return true;
}

bool FPhysScene::SweepSingle(const FCollisionShape& Shape, const FQuat& Rotation, const FVector& Start, const FVector& End,
	FSweepResult& OutHit, const FCollisionQueryParams& Params) const
{
	// 크기가 없는 모양은 PhysX가 유효하지 않은 지오메트리로 거부한다. 점을 쓸어 보는 것과 같으므로 레이로 바꾼다.
	if (Shape.IsNearlyZero())
		return RaycastSingle(Start, End, OutHit, Params);

	const FVector Delta = End - Start;
	const float Distance = Delta.Size();
	if (!Scene || Distance <= 0.0f)
		return false;
	const FVector Dir = Delta / Distance;

	SCOPED_SCENE_READ_LOCK(Scene);

	physx::PxQuat Rot = K2PQuat(Rotation);
	const physx::PxGeometryHolder Geometry = MakeGeometry(Shape, Rot);

	FKurisuQueryFilterCallback Filter(Params, true, K2PVector(Dir));
	physx::PxQueryFilterData FilterData = MakeQueryFilterData();
	physx::PxHitFlags HitFlags = physx::PxHitFlag::eDEFAULT;
	if (Params.bIgnoreTouchesMovingAway)
	{
		FilterData.flags |= physx::PxQueryFlag::ePOSTFILTER;
		HitFlags |= physx::PxHitFlag::eMTD;   // 시작 겹침의 빠져나갈 방향(법선)과 깊이를 계산한다
	}

	physx::PxSweepBuffer Buffer;
	const bool bHit = Scene->sweep(Geometry.any(), physx::PxTransform(K2PVector(Start), Rot), K2PVector(Dir), Distance, Buffer,
		HitFlags, FilterData, &Filter);
	if (!bHit || !Buffer.hasBlock)
		return false;

	FillHitResult(Buffer.block, Start, End, Dir, OutHit);
	return true;
}

bool FPhysScene::OverlapMulti(const FCollisionShape& Shape, const FVector& Position, const FQuat& Rotation,
	TArray<FOverlapResult>& OutOverlaps, const FCollisionQueryParams& Params) const
{
	OutOverlaps.Empty();
	if (!Scene)
		return false;

	SCOPED_SCENE_READ_LOCK(Scene);

	physx::PxQuat Rot = K2PQuat(Rotation);
	const physx::PxGeometryHolder Geometry = MakeGeometry(Shape, Rot);

	// 겹친 것을 모두 받으려면 eBLOCK이 아닌 eTOUCH로 모아야 한다. 최대 256개까지 받는다.
	constexpr physx::PxU32 MaxHits = 256;
	physx::PxOverlapHit Hits[MaxHits];
	physx::PxOverlapBuffer Buffer(Hits, MaxHits);

	physx::PxQueryFilterData FilterData = MakeQueryFilterData();
	FilterData.flags |= physx::PxQueryFlag::eNO_BLOCK;   // 모든 후보를 touch로 취급

	FKurisuQueryFilterCallback Filter(Params, false);   // 겹침 쿼리는 트리거도 찾는다
	Scene->overlap(Geometry.any(), physx::PxTransform(K2PVector(Position), Rot), Buffer, FilterData, &Filter);

	for (physx::PxU32 Index = 0; Index < Buffer.getNbTouches(); ++Index)
	{
		const physx::PxOverlapHit& Hit = Buffer.getTouch(Index);
		const FBodyInstance* Body = FBodyInstance::FromActor(Hit.actor);
		OutOverlaps.Add(FOverlapResult{ Body ? Body->OwnerComponent : nullptr, static_cast<int32>(Hit.faceIndex) });
	}
	return !OutOverlaps.IsEmpty();
}

void FPhysScene::DispatchPendingNotifies()
{
	// 디스패치 중 게임 코드가 액터를 지우거나 새 바디를 만들 수 있으므로 큐를 먼저 꺼내 온다.
	TArray<FCollisionNotifyInfo> Collisions = std::move(PendingCollisionNotifies);
	TArray<FTriggerNotifyInfo> Triggers = std::move(PendingTriggerNotifies);
	PendingCollisionNotifies.Empty();
	PendingTriggerNotifies.Empty();

	for (const FCollisionNotifyInfo& Info : Collisions)
	{
		// 앞선 이벤트 처리에서 컴포넌트가 등록 해제됐을 수 있다.
		if (!Info.ComponentA->IsRegistered() || !Info.ComponentB->IsRegistered())
			continue;
		Info.ComponentA->DispatchHit(Info.ComponentB);
		if (Info.ComponentB->IsRegistered())
			Info.ComponentB->DispatchHit(Info.ComponentA);
	}

	for (const FTriggerNotifyInfo& Info : Triggers)
	{
		if (!Info.TriggerComponent->IsRegistered() || !Info.OtherComponent->IsRegistered())
			continue;
		if (Info.bBegin)
		{
			// 상대가 겹침 이벤트를 껐거나, 같은 액터의 컴포넌트끼리거나, 액터 충돌이 꺼져 있으면 시작하지 않는다.
			// 나감은 조건 없이 보낸다 (EndComponentOverlap은 겹쳐 있던 쌍에만 반응한다).
			const AActor* TriggerOwner = Info.TriggerComponent->GetOwner();
			const AActor* OtherOwner = Info.OtherComponent->GetOwner();
			if (!Info.OtherComponent->bGenerateOverlapEvents ||
				!TriggerOwner || !OtherOwner || TriggerOwner == OtherOwner ||
				!TriggerOwner->GetActorEnableCollision() || !OtherOwner->GetActorEnableCollision())
				continue;
			Info.TriggerComponent->BeginComponentOverlap(Info.OtherComponent);
			Info.OtherComponent->BeginComponentOverlap(Info.TriggerComponent);
		}
		else
		{
			Info.TriggerComponent->EndComponentOverlap(Info.OtherComponent);
			Info.OtherComponent->EndComponentOverlap(Info.TriggerComponent);
		}
	}
}
