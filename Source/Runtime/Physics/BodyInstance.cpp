#include "EnginePCH.h"
#include "BodyInstance.h"
#include "BodySetup.h"
#include "PhysScene.h"
#include "PhysicsCore.h"
#include "PhysXConversions.h"
#include "PhysXIncludes.h"
#include "PhysicsLock.h"
#include "PhysicalMaterial.h"
#include "Components/PrimitiveComponent.h"

namespace
{
	// Kinematic에 CCD를 켜면 PhysX가 경고한다. 시뮬레이트 중일 때만 켠다.
	void ApplyDynamicFlags(physx::PxRigidDynamic& Dynamic, bool bSimulate, bool bUseCCD)
	{
		if (!bSimulate)
			Dynamic.setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, false);   // KINEMATIC을 켜기 전에 먼저 끈다
		Dynamic.setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, !bSimulate);
		if (bSimulate)
			Dynamic.setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, bUseCCD);
	}
}

FBodyInstance::~FBodyInstance()
{
	TermBody();
}

FMaskFilter FBodyInstance::MakeMaskFilter() const
{
	FMaskFilter Filter;
	Filter.Category = CategoryBit(ObjectType);
	Filter.CollidesWith = CollidesWith;
	Filter.OwnerId = OwnerId;
	Filter.Flags = bDrivable ? ECollisionFlags::Drivable : 0u;
	return Filter;
}

bool FBodyInstance::IsTriggerBody() const
{
	return CollisionEnabled == ECollisionEnabled::QueryOnly && OwnerComponent && OwnerComponent->bGenerateOverlapEvents;
}

UPhysicalMaterial* FBodyInstance::GetPhysicalMaterial() const
{
	return PhysMaterialOverride ? PhysMaterialOverride : FPhysicsCore::Get().GetDefaultPhysMaterial();
}

bool FBodyInstance::InitBody(UBodySetup* Setup, const FTransform& WorldTransform, UPrimitiveComponent* PrimComp, FPhysScene* InScene)
{
	TermBody();

	if (!Setup || !InScene || !InScene->IsValid() || !FPhysicsCore::Get().IsInitialized())
		return false;
	if (Setup->AggGeom.GetElementCount() == 0)
	{
		KRS_LOG(Warning, "[Physics] InitBody skipped: BodySetup has no shapes.");
		return false;
	}

	physx::PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();
	const physx::PxTransform Pose = K2PTransform(WorldTransform);

	if (Mobility == EBodyMobility::Static)
	{
		Actor = Physics->createRigidStatic(Pose);
	}
	else
	{
		physx::PxRigidDynamic* Dynamic = Physics->createRigidDynamic(Pose);
		if (Dynamic)
		{
			Dynamic->setLinearDamping(LinearDamping);
			Dynamic->setAngularDamping(AngularDamping);
			ApplyDynamicFlags(*Dynamic, bSimulatePhysics, bUseCCD);
		}
		Actor = Dynamic;
	}
	if (!Actor)
	{
		KRS_LOG(Error, "[Physics] Failed to create rigid actor.");
		return false;
	}

	OwnerComponent = PrimComp;
	Scene = InScene;
	Scale3D = WorldTransform.Scale;
	Actor->userData = this;

	// 여기까지는 씬에 들어가기 전이라 락이 필요 없다. 씬을 바꾸는 addActor만 잠근다.
	Setup->AddShapesToRigidActor(this, *Actor, Scale3D);
	UpdateMassProperties();   // Static 바디는 질량이 없어 아무 일도 하지 않는다
	{
		SCOPED_SCENE_WRITE_LOCK(Scene->GetPxScene());
		Scene->GetPxScene()->addActor(*Actor);
	}
	return true;
}

void FBodyInstance::TermBody()
{
	if (!Actor)
		return;

	// 씬에서 먼저 뺀다. exclusive shape는 액터와 함께 해제된다. 빠진 뒤의 release는 락이 필요 없다.
	if (physx::PxScene* PxScene = Actor->getScene())
	{
		SCOPED_SCENE_WRITE_LOCK(PxScene);
		PxScene->removeActor(*Actor);
	}

	Actor->userData = nullptr;
	Actor->release();
	Actor = nullptr;
	Scene = nullptr;
	OwnerComponent = nullptr;
}

bool FBodyInstance::IsDynamic() const
{
	return Actor && Actor->is<physx::PxRigidDynamic>() != nullptr;
}

bool FBodyInstance::IsSimulatingPhysics() const
{
	const physx::PxRigidDynamic* Dynamic = Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
	if (!Dynamic)
		return false;
	SCOPED_SCENE_READ_LOCK(Dynamic->getScene());
	return !(Dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC);
}

void FBodyInstance::SetSimulatePhysics(bool bSimulate)
{
	bSimulatePhysics = bSimulate;
	physx::PxRigidDynamic* Dynamic = Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
	if (!Dynamic)
		return;   // Static 바디는 바꿀 수 없다. 아직 InitBody 전이면 설정값만 바뀐다.

	SCOPED_SCENE_WRITE_LOCK(Dynamic->getScene());
	ApplyDynamicFlags(*Dynamic, bSimulate, bUseCCD);
	if (bSimulate)
		Dynamic->wakeUp();   // 잠든 채로 전환하면 그 자리에 굳어 있는다
}

void FBodyInstance::SetMassOverride(float MassInKg)
{
	MassInKgOverride = MassInKg;
	UpdateMassProperties();
}

void FBodyInstance::UpdateMassProperties()
{
	physx::PxRigidDynamic* Dynamic = Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
	if (!Dynamic)
		return;

	// InitBody 중(씬에 들어가기 전)에는 getScene()이 null이라 잠그지 않는다.
	SCOPED_SCENE_WRITE_LOCK(Dynamic->getScene());
	// 호출하지 않으면 질량 1, 관성 (1,1,1)로 남아 회전이 이상해진다.
	// 기본은 시뮬레이션 shape만 질량에 넣는다. QueryOnly는 시뮬레이션 shape가 없으므로 쿼리 shape로 계산한다.
	const bool bIncludeNonSimShapes = (CollisionEnabled == ECollisionEnabled::QueryOnly);
	if (MassInKgOverride > 0.0f)
		physx::PxRigidBodyExt::setMassAndUpdateInertia(*Dynamic, MassInKgOverride, nullptr, bIncludeNonSimShapes);
	else
		physx::PxRigidBodyExt::updateMassAndInertia(*Dynamic, GetPhysicalMaterial()->Density, nullptr, bIncludeNonSimShapes);
}

FTransform FBodyInstance::GetWorldTransform() const
{
	if (!Actor)
		return FTransform::Identity;
	SCOPED_SCENE_READ_LOCK(Actor->getScene());
	return P2KTransform(Actor->getGlobalPose(), Scale3D);
}

void FBodyInstance::SetWorldTransform(const FTransform& NewTransform, bool bTeleport)
{
	if (!Actor)
		return;

	physx::PxRigidDynamic* Dynamic = Actor->is<physx::PxRigidDynamic>();
	const physx::PxTransform Pose = K2PTransform(NewTransform);

	SCOPED_SCENE_WRITE_LOCK(Actor->getScene());
	// Kinematic은 target으로 옮겨야 다음 simulate에서 부딪힌 물체를 밀어낸다. teleport면 즉시 옮긴다.
	if (Dynamic && !bTeleport && (Dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC))
		Dynamic->setKinematicTarget(Pose);
	else
		Actor->setGlobalPose(Pose);
}

FBodyInstance* FBodyInstance::FromActor(const physx::PxRigidActor* InActor)
{
	return InActor ? static_cast<FBodyInstance*>(InActor->userData) : nullptr;
}

void FBodyInstance::CopyBodyInstancePropertiesFrom(const FBodyInstance& From)
{
	Mobility = From.Mobility;
	bSimulatePhysics = From.bSimulatePhysics;
	MassInKgOverride = From.MassInKgOverride;
	LinearDamping = From.LinearDamping;
	AngularDamping = From.AngularDamping;
	CollisionEnabled = From.CollisionEnabled;
	ObjectType = From.ObjectType;
	bDrivable = From.bDrivable;
	bUseCCD = From.bUseCCD;
	CollidesWith = From.CollidesWith;
	OwnerId = From.OwnerId;
	PhysMaterialOverride = From.PhysMaterialOverride;
}

void FBodyInstance::AddImpulseAtLocation(const FVector& Impulse, const FVector& WorldLocation)
{
	physx::PxRigidDynamic* Dynamic = Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
	if (!Dynamic)
		return;

	SCOPED_SCENE_WRITE_LOCK(Dynamic->getScene());
	if (Dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)
		return;   // Kinematic은 힘을 받지 않는다
	// 중심이 아닌 점을 밀면 회전도 생긴다 (모서리를 밀면 돈다).
	physx::PxRigidBodyExt::addForceAtPos(*Dynamic, K2PVector(Impulse), K2PVector(WorldLocation), physx::PxForceMode::eIMPULSE);
}
