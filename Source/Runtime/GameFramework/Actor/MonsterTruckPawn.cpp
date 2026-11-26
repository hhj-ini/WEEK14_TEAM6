#include "EnginePCH.h"
#include "GameFramework/Actor/MonsterTruckPawn.h"
#include "Asset/AssetManager.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/Misc/Paths.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsLock.h"
#include "Physics/PhysXConversions.h"
#include "Physics/PhysXFilterShader.h"
#include "Serialization/StructuredArchiveSlots.h"
#include <geometry/PxConvexMesh.h>
#include <vehicle/PxVehicleNoDrive.h>
#include <vehicle/PxVehicleTireFriction.h>
#include <vehicle/PxVehicleUpdate.h>
#include <vehicle/PxVehicleUtilSetup.h>
#include <cmath>

using namespace physx;

namespace
{
	constexpr PxU32 WheelCount = 4;
	constexpr PxU16 HitsPerWheel = 6;
	constexpr float PhysicsStep = 1.0f / 60.0f;
	constexpr PxU32 MaxPhysicsStepsPerFrame = 4;
	constexpr float WheelRadius = 0.827f;
	constexpr float WheelWidth = 1.052f;
	constexpr float HardLandingSpeed = 6.0f;
	constexpr float LandingUprightThreshold = 0.80f;
	constexpr float LandingSkin = 0.015f;
	constexpr PxU32 MaxLandingSweepSegments = 4;
	constexpr float TruckMass = 3280.0f;
	constexpr float DefaultTireGrip = 1.20f;
	constexpr char StuntTrackMaterial[] = "FBX/Truck/Arena/LoopStuntSurface.pmat";
	constexpr char TreadTexturePath[] = "FBX/Truck/Effects/TireTreadColor.png";
	constexpr char DustTemplatePath[] = "FBX/Truck/Effects/WheelDust.particle";
	constexpr int32 MaxTreadDecals = 256;
	constexpr float TreadSpacing = 1.45f;
	const FVector BedLocation(-1.10f, 0.0f, 0.30f);
	const PxVec3 CenterOfMass(-0.16f, 0.0f, -0.15f);
	const PxVec3 WheelCenters[WheelCount] = {
		{ 1.798f, -1.411f, -0.754f }, { 1.798f, 1.414f, -0.754f },
		{ -1.511f, -1.411f, -0.754f }, { -1.511f, 1.414f, -0.754f }
	};

	struct FTruckVisualPose
	{
		FTransform Chassis;
		FTransform Wheels[WheelCount];
	};

	FTransform InterpolateVisualPose(const FTransform& Previous, const FTransform& Current, float Alpha)
	{
		return FTransform(
			FMath::Slerp(Previous.Rotation, Current.Rotation, Alpha),
			FMath::Lerp(Previous.Location, Current.Location, Alpha),
			Current.Scale);
	}

	PxQueryHitType::Enum FilterSuspensionSweep(PxFilterData, PxFilterData Surface, const void*, PxU32, PxHitFlags&)
	{
		return (Surface.word3 & ECollisionFlags::Drivable) ? PxQueryHitType::eTOUCH : PxQueryHitType::eNONE;
	}

	struct FLandingSweepFilter : PxQueryFilterCallback
	{
		FLandingSweepFilter(const PxVec3& Up, const PxMaterial* StuntSurface) : Up(Up), StuntSurface(StuntSurface) {}

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor*, PxHitFlags&) override
		{
			const PxFilterData Surface = Shape->getQueryFilterData();
			return (Surface.word0 & CategoryBit(ECollisionCategory::WorldStatic)) && (Surface.word3 & ECollisionFlags::Drivable)
				&& Shape->getFlags().isSet(PxShapeFlag::eSIMULATION_SHAPE) ? PxQueryHitType::eBLOCK : PxQueryHitType::eNONE;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit& QueryHit) override
		{
			const auto& Hit = static_cast<const PxSweepHit&>(QueryHit);
			if (Hit.normal.dot(Up) < 0.30f ||
				(StuntSurface && Hit.shape->getMaterialFromInternalFaceIndex(Hit.faceIndex) == StuntSurface))
				return PxQueryHitType::eNONE;
			return PxQueryHitType::eBLOCK;
		}

		const PxVec3 Up;
		const PxMaterial* StuntSurface;
	};

	float WheelSupportProjection(const PxConvexMeshGeometry& Geometry, const PxTransform& Pose, const PxVec3& Normal)
	{
		const PxVec3 LocalNormal = Pose.q.rotateInv(Normal);
		const PxVec3* Vertices = Geometry.convexMesh->getVertices();
		float Minimum = PX_MAX_F32;
		for (PxU32 Index = 0; Index < Geometry.convexMesh->getNbVertices(); ++Index)
			Minimum = PxMin(Minimum, LocalNormal.dot(Geometry.scale.transform(Vertices[Index])));
		return Normal.dot(Pose.p) + Minimum;
	}

	PxConvexMesh* CookWheel()
	{
		constexpr PxU32 Sides = 24;
		PxVec3 Points[Sides * 2];
		for (PxU32 Index = 0; Index < Sides; ++Index)
		{
			const float Angle = PxTwoPi * static_cast<float>(Index) / static_cast<float>(Sides);
			const float X = WheelRadius * std::cos(Angle);
			const float Z = WheelRadius * std::sin(Angle);
			Points[Index] = PxVec3(X, -WheelWidth * 0.5f, Z);
			Points[Index + Sides] = PxVec3(X, WheelWidth * 0.5f, Z);
		}
		PxConvexMeshDesc Desc;
		Desc.points.count = Sides * 2;
		Desc.points.stride = sizeof(PxVec3);
		Desc.points.data = Points;
		Desc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
		auto& Core = FPhysicsCore::Get();
		return Core.GetCooking()->createConvexMesh(Desc, Core.GetPhysics()->getPhysicsInsertionCallback());
	}

	PxVehicleDrivableSurfaceToTireFrictionPairs* CreateTireFrictionPairs(PxPhysics& Physics, PxMaterial& DefaultMaterial)
	{
		constexpr PxU32 MaxSurfaces = PxVehicleDrivableSurfaceToTireFrictionPairs::eMAX_NB_SURFACE_TYPES;
		PxMaterial* RegisteredMaterials[MaxSurfaces];
		const PxU32 RegisteredCount = Physics.getMaterials(RegisteredMaterials, MaxSurfaces);

		// PhysX는 등록되지 않은 표면에 0번 항목을 쓰므로 기본 재질을 먼저 넣는다.
		const PxMaterial* Materials[MaxSurfaces] = { &DefaultMaterial };
		PxVehicleDrivableSurfaceType Types[MaxSurfaces] = {};
		PxU32 Count = 1;
		for (PxU32 Index = 0; Index < RegisteredCount && Count < MaxSurfaces; ++Index)
		{
			if (RegisteredMaterials[Index] == &DefaultMaterial)
				continue;
			Materials[Count] = RegisteredMaterials[Index];
			Types[Count].mType = Count;
			++Count;
		}

		auto* Pairs = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(1, Count);
		if (!Pairs)
			return nullptr;
		Pairs->setup(1, Count, Materials, Types);
		Pairs->setTypePairFriction(0, 0, DefaultTireGrip);
		const float DefaultFriction = DefaultMaterial.getDynamicFriction();
		const float Scale = DefaultFriction > 0.0f ? DefaultTireGrip / DefaultFriction : DefaultTireGrip;
		for (PxU32 Index = 1; Index < Count; ++Index)
			Pairs->setTypePairFriction(Index, 0, Scale * Materials[Index]->getDynamicFriction());
		return Pairs;
	}

	PxShape* AttachCollisionBox(FBodyInstance& Body, PxMaterial& Material, const PxVec3& HalfSize, const PxVec3& Offset)
	{
		PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Body.Actor, PxBoxGeometry(HalfSize), Material);
		if (Shape)
		{
			Shape->setLocalPose(PxTransform(Offset));
			const PxFilterData Filter = K2PFilterData(Body.MakeMaskFilter());
			Shape->setSimulationFilterData(Filter);
			Shape->setQueryFilterData(Filter);
			Shape->userData = &Body;
		}
		return Shape;
	}
}

struct FMonsterTruckPhysics
{
	struct FWheelEffect
	{
		UParticleSystemComponent* DustEmitter = nullptr;
		PxVec3 Point = PxVec3(0);
		PxVec3 Normal = PxVec3(0, 0, 1);
		PxVec3 Velocity = PxVec3(0);
		PxVec3 TreadStart = PxVec3(0);
		PxVec3 PreviousNormal = PxVec3(0, 0, 1);
		float DustCarry = 0.0f;
		bool bOnGround = false;
		bool bDustSurface = false;
		bool bHasTreadStart = false;
	};

	~FMonsterTruckPhysics()
	{
		if (PreSimulateHandle) PhysScene->OnPreSimulate.Remove(PreSimulateHandle);
		if (Vehicle) Vehicle->free();
		if (Batch) Batch->release();
		if (Friction) Friction->release();
		for (PxShape* Shape : WheelShapes)
			if (Shape) Truck->detachShape(*Shape);
	}

	FTruckVisualPose ReadVisualPose(const FVector& ChassisScale) const
	{
		FTruckVisualPose Pose;
		Pose.Chassis = P2KTransform(Truck->getGlobalPose(), ChassisScale);
		for (PxU32 Index = 0; Index < WheelCount; ++Index)
			Pose.Wheels[Index] = P2KTransform(WheelShapes[Index]->getLocalPose());
		return Pose;
	}

	void InitializeWheelEffects(AMonsterTruckPawn& Owner)
	{
		const auto& Content = FPaths::ProjectContentDir();
		std::error_code Error;
		if (std::filesystem::is_regular_file(Content / TreadTexturePath, Error))
			TreadTexture = UAssetManager::Get().LoadTexture(TreadTexturePath);
		if (!std::filesystem::is_regular_file(Content / DustTemplatePath, Error)) return;
		UParticleSystem* DustTemplate = UAssetManager::LoadParticleSystem(DustTemplatePath);
		if (!DustTemplate) return;
		for (FWheelEffect& Wheel : WheelEffects)
		{
			Wheel.DustEmitter = Cast<UParticleSystemComponent>(Owner.AddComponent(UParticleSystemComponent::StaticClass(), NAME_None));
			if (!Wheel.DustEmitter) continue;
			Wheel.DustEmitter->DetachFromParent(EAttachmentRule::KeepRelative);
			Wheel.DustEmitter->SetFlags(EObjectFlags::RF_Transient);
			Wheel.DustEmitter->SetTemplate(DustTemplate);
		}
	}

	void RemoveWheelEffects(AMonsterTruckPawn& Owner)
	{
		for (UDecalComponent* Decal : TreadDecals) Owner.DestroyComponent(Decal);
		for (const FWheelEffect& Wheel : WheelEffects)
			if (Wheel.DustEmitter) Owner.DestroyComponent(Wheel.DustEmitter);
	}

	void CaptureWheelEffects()
	{
		const PxVec3 CenterOfMassWorld = Truck->getGlobalPose().transform(Truck->getCMassLocalPose().p);
		const PxVec3 Linear = Truck->getLinearVelocity();
		const PxVec3 Angular = Truck->getAngularVelocity();
		for (PxU32 Index = 0; Index < WheelCount; ++Index)
		{
			const PxWheelQueryResult& Contact = WheelResults[Index];
			FWheelEffect& Effect = WheelEffects[Index];
			Effect.bOnGround = !Contact.isInAir && Contact.tireContactActor && Contact.tireContactActor->is<PxRigidStatic>();
			if (!Effect.bOnGround) continue;
			Effect.Point = Contact.tireContactPoint;
			Effect.Normal = Contact.tireContactNormal;
			const PxVec3 ContactVelocity = Linear + Angular.cross(Effect.Point - CenterOfMassWorld);
			Effect.Velocity = ContactVelocity - Effect.Normal * ContactVelocity.dot(Effect.Normal);
			Effect.bDustSurface = Effect.Normal.z > 0.45f && Contact.tireSurfaceMaterial != StuntSurface;
		}
	}

	struct FLandingContact
	{
		PxVec3 Normal = PxVec3(0);
		PxVec3 Point = PxVec3(0);
		float Gap = 0.0f;
		float InitialDepth = 0.0f;
	};

	bool SweepWheelForLanding(PxU32 Index, const PxTransform* BodyPoses, PxU32 Segments,
		FLandingSweepFilter& Filter, FLandingContact& OutContact) const
	{
		PxConvexMeshGeometry Geometry;
		WheelShapes[Index]->getConvexMeshGeometry(Geometry);
		const PxTransform LocalPose = WheelShapes[Index]->getLocalPose();
		const PxTransform Start = BodyPoses[0] * LocalPose;
		const PxTransform End = BodyPoses[Segments] * LocalPose;
		const PxQueryFilterData QueryFlags(PxQueryFlag::eSTATIC | PxQueryFlag::ePREFILTER | PxQueryFlag::ePOSTFILTER);
		const PxHitFlags HitFlags = PxHitFlag::eDEFAULT | PxHitFlag::eMTD;
		const PxWheelQueryResult& Suspension = WheelResults[Index];
		for (PxU32 Segment = 0; Segment <= Segments; ++Segment)
		{
			const PxTransform From = BodyPoses[Segment] * LocalPose;
			const PxVec3 Delta = Segment < Segments ? BodyPoses[Segment + 1].transform(LocalPose.p) - From.p : PxVec3(0);
			const float Distance = Delta.magnitude();
			const PxVec3 Direction = Distance > 1.e-5f ? Delta / Distance : -Filter.Up;
			PxSweepBuffer Result;
			// 마지막의 거리 0인 스윕도 검사해 회전으로만 생긴 겹침을 찾는다.
			if (!Truck->getScene()->sweep(Geometry, From, Direction, Distance, Result, HitFlags, QueryFlags, &Filter) || !Result.hasBlock)
				continue;

			const PxSweepHit& Hit = Result.block;
			const float Depth = PxMax(-Hit.distance, 0.0f);
			const float SurfaceProjection = Depth > 0.0f ? WheelSupportProjection(Geometry, From, Hit.normal) + Depth : Hit.normal.dot(Hit.position);
			const float EndGap = WheelSupportProjection(Geometry, End, Hit.normal) - SurfaceProjection;
			const float InitialDepth = Segment == 0 ? Depth : 0.0f;
			const bool bSuspensionSupportsHit = !Suspension.isInAir && Suspension.tireContactActor == Hit.actor &&
				Suspension.tireContactNormal.dot(Hit.normal) > 0.9f;
			const float AllowedDepth = bSuspensionSupportsHit ? 0.09f : LandingSkin;
			if (EndGap >= -AllowedDepth && InitialDepth <= AllowedDepth) continue;

			OutContact.Normal = Hit.normal;
			OutContact.Point = Hit.position;
			OutContact.Gap = WheelSupportProjection(Geometry, Start, Hit.normal) - SurfaceProjection;
			OutContact.InitialDepth = InitialDepth;
			return true;
		}
		return false;
	}

	void ResolveLanding(float DeltaTime)
	{
		if (Truck->isSleeping()) return;
		const PxVec3 Gravity = Truck->getScene()->getGravity();
		const PxVec3 Up = -Gravity.getNormalized();
		if (Up.isZero()) return;
		const PxTransform Body = Truck->getGlobalPose();
		const PxVec3 Linear = Truck->getLinearVelocity();
		const PxVec3 Angular = Truck->getAngularVelocity();
		if (!bLandingRecovery && Linear.dot(Up) >= -HardLandingSpeed && Body.q.getBasisVector2().dot(Up) >= LandingUprightThreshold)
			return;

		const PxVec3 GravityDelta = Truck->getActorFlags().isSet(PxActorFlag::eDISABLE_GRAVITY) ? PxVec3(0) : Gravity * DeltaTime;
		const PxTransform MassPose = Body * Truck->getCMassLocalPose();
		const float AngularSpeed = Angular.magnitude();
		const float Rotation = AngularSpeed * DeltaTime;
		const PxU32 Segments = PxClamp(static_cast<PxU32>(std::ceil(Rotation / 0.075f)), 1u, MaxLandingSweepSegments);
		PxTransform BodyPoses[MaxLandingSweepSegments + 1];
		BodyPoses[0] = Body;
		for (PxU32 Segment = 1; Segment <= Segments; ++Segment)
		{
			const float Fraction = static_cast<float>(Segment) / Segments;
			const PxQuat Turn = AngularSpeed > 1.e-5f ? PxQuat(Rotation * Fraction, Angular / AngularSpeed) : PxQuat(PxIdentity);
			const PxVec3 MassPosition = MassPose.p + (Linear + GravityDelta) * (DeltaTime * Fraction);
			BodyPoses[Segment] = PxTransform(MassPosition + Turn.rotate(Body.p - MassPose.p), Turn * Body.q);
		}

		FLandingSweepFilter Filter(Up, StuntSurface);
		FLandingContact Contacts[WheelCount];
		PxU32 Count = 0;
		for (PxU32 Index = 0; Index < WheelCount; ++Index)
			if (SweepWheelForLanding(Index, BodyPoses, Segments, Filter, Contacts[Count])) ++Count;
		bLandingRecovery = Count != 0;
		if (!bLandingRecovery) return;

		// 이미 겹친 바퀴는 여러 바퀴의 MTD를 합쳐 차체를 필요한 만큼만 이동시킨다.
		PxVec3 Correction(0);
		for (PxU32 Iteration = 0; Iteration < 2; ++Iteration)
			for (PxU32 Index = 0; Index < Count; ++Index)
			{
				const FLandingContact& Contact = Contacts[Index];
				Correction += Contact.Normal * PxMax(0.0f, Contact.InitialDepth - LandingSkin - Correction.dot(Contact.Normal));
			}
		Correction *= 1.0f / PxMax(1.0f, Correction.magnitude() / 0.25f);
		if (!Correction.isZero()) Truck->setGlobalPose(PxTransform(Body.p + Correction, Body.q));

		// 남은 거리만큼은 내려가도록 허용하고, 접촉점의 법선 방향 속도만 제한한다.
		const float InvMass = Truck->getInvMass();
		const PxVec3 InvInertia = Truck->getMassSpaceInvInertiaTensor();
		PxVec3 NextLinear = Linear + GravityDelta;
		PxVec3 NextAngular = Angular;
		for (PxU32 Iteration = 0; Iteration < 4; ++Iteration)
			for (PxU32 Index = 0; Index < Count; ++Index)
			{
				const FLandingContact& Contact = Contacts[Index];
				const PxVec3 Arm = Contact.Point - (MassPose.p + Correction);
				const PxVec3 Torque = Arm.cross(Contact.Normal);
				const PxVec3 AngularPerImpulse = MassPose.q.rotate(MassPose.q.rotateInv(Torque).multiply(InvInertia));
				const float Gap = Contact.Gap + Correction.dot(Contact.Normal);
				const float TargetSpeed = -PxMax(Gap - LandingSkin, 0.0f) / DeltaTime;
				const float ContactSpeed = Contact.Normal.dot(NextLinear) + Torque.dot(NextAngular);
				const float Impulse = PxMax(TargetSpeed - ContactSpeed, 0.0f) / (InvMass + Torque.dot(AngularPerImpulse));
				NextLinear += Contact.Normal * (InvMass * Impulse);
				NextAngular += AngularPerImpulse * Impulse;
			}
		Truck->setLinearVelocity(NextLinear - GravityDelta);
		Truck->setAngularVelocity(NextAngular);
	}

	void StampTread(AMonsterTruckPawn& Owner, const PxVec3& Start, const PxVec3& End, const PxVec3& Normal)
	{
		const PxVec3 Delta = End - Start;
		const PxVec3 Tangent = (Delta - Normal * Delta.dot(Normal)).getNormalized();
		if (Tangent.isZero()) return;
		if (TreadDecals.Num() >= MaxTreadDecals)
		{
			Owner.DestroyComponent(TreadDecals[0]);
			TreadDecals.RemoveAt(0);
		}
		auto* Decal = Cast<UDecalComponent>(Owner.AddComponent(UDecalComponent::StaticClass(), NAME_None));
		if (!Decal) return;
		Decal->DetachFromParent(EAttachmentRule::KeepRelative);
		Decal->SetFlags(EObjectFlags::RF_Transient);
		const PxVec3 Forward = -Normal;
		const PxQuat Rotation(PxMat33(Forward, Tangent.cross(Forward), Tangent));
		Decal->SetTransform(FTransform(P2KQuat(Rotation), P2KVector((Start + End) * 0.5f)));
		const float HalfLength = Delta.magnitude() * 0.5f;
		Decal->SetExtent(FVector(0.26f + HalfLength * 0.18f, WheelWidth * 0.49f, HalfLength + 0.11f));
		Decal->SetDecalTexture(TreadTexture);
		Decal->SetFadeTimes(0.0f, 0.0f, 1.5f, 1.7f);
		Decal->TickComponent(0.0f);
		TreadDecals.Add(Decal);
	}

	void UpdateWheelEffects(AMonsterTruckPawn& Owner, float DeltaTime, bool bReset)
	{
		for (int32 Index = TreadDecals.Num() - 1; Index >= 0; --Index)
		{
			if (TreadDecals[Index]->GetFade() > 0.0f) continue;
			Owner.DestroyComponent(TreadDecals[Index]);
			TreadDecals.RemoveAt(Index);
		}

		for (FWheelEffect& Effect : WheelEffects)
		{
			const float Speed = Effect.Velocity.magnitude();
			if (bReset || !Effect.bOnGround || Speed < 0.8f)
			{
				Effect.bHasTreadStart = false;
				Effect.DustCarry = 0.0f;
				continue;
			}
			if (TreadTexture)
			{
				const float Distance = (Effect.Point - Effect.TreadStart).magnitude();
				const bool bContinuous = Effect.bHasTreadStart && Distance < 3.7f && Effect.Normal.dot(Effect.PreviousNormal) > 0.70f;
				if (!bContinuous || Distance >= TreadSpacing)
				{
					if (bContinuous)
						StampTread(Owner, Effect.TreadStart, Effect.Point, (Effect.Normal + Effect.PreviousNormal).getNormalized());
					Effect.TreadStart = Effect.Point;
					Effect.PreviousNormal = Effect.Normal;
					Effect.bHasTreadStart = true;
				}
			}
			if (!Effect.DustEmitter || !Effect.bDustSurface || Speed < 1.6f)
			{
				Effect.DustCarry = 0.0f;
				continue;
			}
			Effect.DustCarry += FMath::Lerp(6.0f, 20.0f, FMath::Clamp((Speed - 1.6f) / 17.0f, 0.0f, 1.0f)) * DeltaTime;
			const int32 Count = static_cast<int32>(Effect.DustCarry);
			Effect.DustCarry -= Count;
			if (Count == 0) continue;
			const FVector Origin = P2KVector(Effect.Point + Effect.Normal * 0.10f - Effect.Velocity.getNormalized() * 0.16f);
			const FVector Velocity = P2KVector(Effect.Velocity * 0.075f + Effect.Normal * (0.5f + Speed * 0.018f));
			for (FParticleEmitterInstance* Emitter : Effect.DustEmitter->GetEmitterInstances())
				Emitter->ForceSpawn(Count, Origin, Velocity);
		}
	}

	void UpdateStuntFlip(PxScene& Scene, PxU32 GroundedWheels)
	{
		if (!StuntSurface) return;
		const PxTransform Pose = Truck->getGlobalPose();
		const PxVec3 Up = Pose.q.rotate(PxVec3(0, 0, 1));
		const PxVec3 Right = Pose.q.rotate(PxVec3(0, 1, 0));
		if (bStuntFlipInAir)
		{
			if (Up.z > 0.998f)
			{
				const PxVec3 Spin = Truck->getAngularVelocity();
				Truck->setAngularVelocity(Spin - Right * Spin.dot(Right));
				bStuntFlipInAir = false;
			}
			else if (GroundedWheels > 0)
			{
				bStuntFlipInAir = false;
			}
			return;
		}
		if (Up.z > -0.75f)
		{
			bStuntCrest = false;
			return;
		}
		if (GroundedWheels > 0)
		{
			for (const PxWheelQueryResult& Wheel : WheelResults)
			{
				if (Wheel.tireSurfaceMaterial == StuntSurface)
				{
					bStuntCrest = true;
					break;
				}
			}
			return;
		}
		if (!bStuntCrest) return;
		bStuntCrest = false;

		// 반원 꼭대기에서 이탈하면 낙하 시간에 맞춰 남은 피치 회전을 이어 간다.
		const PxVec3 Velocity = Truck->getLinearVelocity();
		const PxVec3 LandingProbe = Pose.p + PxVec3(Velocity.x, Velocity.y, 0) * 2.0f;
		PxRaycastBuffer Landing;
		const PxReal Gravity = -Scene.getGravity().z;
		if (Gravity <= 0 || !Scene.raycast(LandingProbe, PxVec3(0, 0, -1), 90.0f, Landing)) return;
		const float Height = Landing.block.distance - 1.6f;
		if (Height < 4.0f) return;
		const float AirTime = (Velocity.z + std::sqrt(Velocity.z * Velocity.z + 2 * Gravity * Height)) / Gravity;
		const PxVec3 Forward = Pose.q.rotate(PxVec3(1, 0, 0));
		const float Pitch = std::atan2(Forward.z, Up.z);
		const float Remaining = Pitch >= 0 ? PxTwoPi - Pitch : -Pitch;
		const float SpinRate = Remaining / FMath::Max(0.35f, AirTime - 0.30f);
		Truck->setAngularVelocity(-Right * FMath::Min(SpinRate, 4.0f));
		bStuntFlipInAir = true;
	}

	PxRigidDynamic* Truck = nullptr;
	FPhysScene* PhysScene = nullptr;
	FDelegateHandle PreSimulateHandle = 0;
	PxVehicleNoDrive* Vehicle = nullptr;
	PxBatchQuery* Batch = nullptr;
	PxVehicleDrivableSurfaceToTireFrictionPairs* Friction = nullptr;
	const PxMaterial* StuntSurface = nullptr;
	PxSweepQueryResult SweepResults[WheelCount] = {};
	PxSweepHit SweepHits[WheelCount * HitsPerWheel] = {};
	PxWheelQueryResult WheelResults[WheelCount] = {};
	PxShape* WheelShapes[WheelCount] = {};
	FTruckVisualPose PreviousVisualPose;
	FTruckVisualPose CurrentVisualPose;
	PxU32 VisualPoseTimestamp = 0;
	float VisualPoseInterval = PhysicsStep;
	float TimeSincePhysicsStep = 0.0f;
	bool bStuntCrest = false;
	bool bStuntFlipInAir = false;
	bool bLandingRecovery = false;
	FWheelEffect WheelEffects[WheelCount];
	TArray<UDecalComponent*> TreadDecals;
	UTexture2D* TreadTexture = nullptr;
};

UMonsterTruckDriveComponent::UMonsterTruckDriveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::PrePhysics;
}

void UMonsterTruckDriveComponent::TickComponent(float DeltaTime)
{
	if (auto* Truck = Cast<AMonsterTruckPawn>(GetOwner()))
		Truck->UpdateVehicle(DeltaTime);
}

AMonsterTruckPawn::AMonsterTruckPawn()
{
	CreateDefaultSubobject<UMonsterTruckDriveComponent>("TruckDrive");
	// 물리가 차체 포즈를 동기화한 뒤 스프링 암 틱 전에 바퀴를 갱신한다.
	PrimaryActorTick.TickGroup = ETickingGroup::DuringPhysics;
	Chassis = CreateDefaultSubobject<UBoxComponent>("Chassis");
	SetRootComponent(Chassis);
	Chassis->BoxExtent = FVector(2.12f, 0.53f, 0.20f);
	Chassis->BodyInstance.bSimulatePhysics = true;
	Chassis->BodyInstance.bUseCCD = true;
	Chassis->BodyInstance.MassInKgOverride = TruckMass;
	Chassis->BodyInstance.ObjectType = ECollisionCategory::Vehicle;
	Chassis->bDrawOnlyIfSelected = true;

	Presentation = CreateDefaultSubobject<USceneComponent>("TruckPresentation");
	Presentation->SetupAttachment(Chassis);
	const auto CreateVisual = [&](const char* Name, const char* MeshPath, const FVector& Location) {
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Presentation);
		Part->SetRelativeLocation(Location);
		Part->BodyInstance.CollisionEnabled = ECollisionEnabled::NoCollision;
		Part->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>(MeshPath));
		return Part;
	};
	CreateVisual("CabAndFrame", "FBX/Truck/Cab.obj", FVector::ZeroVector);
	const char* WheelNames[] = { "WheelFL", "WheelFR", "WheelRL", "WheelRR" };
	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		const char* MeshPath = Index % 2 == 0 ? "FBX/Truck/WheelLeft.obj" : "FBX/Truck/WheelRight.obj";
		Wheels[Index] = CreateVisual(WheelNames[Index], MeshPath, P2KVector(WheelCenters[Index]));
	}
	Axles[0] = CreateVisual("FrontAxle", "FBX/Truck/AxleFront.obj", FVector(WheelCenters[0].x, 0.0f, WheelCenters[0].z));
	Axles[1] = CreateVisual("RearAxle", "FBX/Truck/AxleRear.obj", FVector(WheelCenters[2].x, 0.0f, WheelCenters[2].z));

	CreateVisual("DumpBed", "FBX/Truck/DumpBed.obj", BedLocation);

	CameraArm = CreateDefaultSubobject<USpringArmComponent>("CameraArm");
	CameraArm->SetupAttachment(Presentation);
	CameraArm->SetRelativeLocation(FVector(0.0f, 0.0f, 1.17f));
	CameraArm->SetRelativeRotation(FRotator(17.0f, -12.0f, 0.0f));
	CameraArm->TargetArmLength = 8.8f;
	CameraArm->bInheritPitch = false;
	CameraArm->bInheritRoll = false;
	CameraArm->bEnableCameraLag = true;
	CameraArm->bEnableCameraRotationLag = true;
	CameraArm->CameraLagSpeed = 7.0f;
	CameraArm->CameraRotationLagSpeed = 4.0f;
	CameraArm->CameraLagMaxDistance = 1.75f;
	CameraArm->bDrawDebug = false;
	auto* Camera = CreateDefaultSubobject<UCameraComponent>("Camera");
	Camera->SetupAttachment(CameraArm);
	Camera->SetExternalInputManaged(true);
	Camera->SetFieldOfView(66.0f);
	Camera->PostProcessSettings.bOverride_bAutoExposure = true;
	Camera->PostProcessSettings.bAutoExposure = false;
}

AMonsterTruckPawn::~AMonsterTruckPawn() = default;

void AMonsterTruckPawn::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	const FArchive& Archive = Record.GetUnderlyingArchive();
	if (!Archive.IsLoading() || Archive.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate) || Physics)
		return;

	if (UActorComponent* LegacyBed = FindComponentByName(FName("BedBody")))
	{
		if (auto* Mesh = Cast<USceneComponent>(FindComponentByName(FName("DumpBed"))))
		{
			Mesh->SetupAttachment(Chassis, EAttachmentRule::KeepRelative);
			Mesh->SetRelativeLocation(BedLocation);
		}
		DestroyComponent(LegacyBed);
	}

	if (!Chassis->IsRegistered()) return;

	// 씬 로더는 저장값을 읽기 전에 기본 컴포넌트를 등록한다. 트럭의 바디와 메시만 갱신한다.
	Chassis->DestroyPhysicsState();
	Chassis->MarkTransformDirty();
	Chassis->CreatePhysicsState();
	for (UActorComponent* Component : GetComponents())
	{
		if (auto* Mesh = Cast<UStaticMeshComponent>(Component))
			Mesh->MarkRenderStateDirty();
	}
}

void AMonsterTruckPawn::BeginPlay()
{
	if (HasBegunPlay()) return;
	Super::BeginPlay();
	SpawnPose = GetActorTransform();
	CameraArm->ResetCameraLag();
	if (!SetupVehicle())
		KRS_LOG(Error, "MonsterTruck: vehicle setup failed.");
	else
		Physics->InitializeWheelEffects(*this);
}

void AMonsterTruckPawn::EndPlay()
{
	if (Physics)
	{
		Physics->RemoveWheelEffects(*this);
		SCOPED_SCENE_WRITE_LOCK(Chassis->BodyInstance.Scene->GetPxScene());
		Physics.reset();
	}
	Super::EndPlay();
}

bool AMonsterTruckPawn::SetupVehicle()
{
	auto& ChassisBody = Chassis->BodyInstance;
	if (!ChassisBody.IsSimulatingPhysics()) return false;
	auto& Core = FPhysicsCore::Get();
	PxMaterial* DefaultMaterial = Core.GetDefaultPhysMaterial()->GetPxMaterial();
	if (!DefaultMaterial) return false;
	// Vehicle SDK의 축은 전역 설정이다. 차량을 준비할 때 엔진의 Z-up, X-forward 기준을 지정한다.
	PxVehicleSetBasisVectors(PxVec3(0, 0, 1), PxVec3(1, 0, 0));

	auto* Scene = ChassisBody.Scene->GetPxScene();
	SCOPED_SCENE_WRITE_LOCK(Scene);
	auto State = MakeUnique<FMonsterTruckPhysics>();
	State->Truck = ChassisBody.Actor->is<PxRigidDynamic>();
	std::error_code AssetError;
	if (std::filesystem::is_regular_file(FPaths::ProjectContentDir() / StuntTrackMaterial, AssetError))
	{
		if (UPhysicalMaterial* Material = UAssetManager::LoadPhysicalMaterial(StuntTrackMaterial))
			State->StuntSurface = Material->GetPxMaterial();
	}
	PxConvexMesh* WheelMesh = CookWheel();
	if (!WheelMesh) return false;
	const PxShapeFlags WheelFlags = PxShapeFlag::eVISUALIZATION | PxShapeFlag::eSIMULATION_SHAPE;
	FMaskFilter WheelFilter = ChassisBody.MakeMaskFilter();
	WheelFilter.CollidesWith &= CategoryBit(ECollisionCategory::PhysicsBody);
	const PxFilterData WheelSimulationFilter = K2PFilterData(WheelFilter);
	const PxU32 ExistingShapes = State->Truck->getNbShapes();
	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		PxShape* Shape = PxRigidActorExt::createExclusiveShape(*State->Truck,
			PxConvexMeshGeometry(WheelMesh), *DefaultMaterial, WheelFlags);
		State->WheelShapes[Index] = Shape;
		if (Shape)
		{
			Shape->setLocalPose(PxTransform(WheelCenters[Index]));
			Shape->setSimulationFilterData(WheelSimulationFilter);
			Shape->userData = &ChassisBody;
		}
	}
	WheelMesh->release();
	for (PxShape* Shape : State->WheelShapes)
		if (!Shape) return false;
	if (!AttachCollisionBox(ChassisBody, *DefaultMaterial, PxVec3(0.7f, 0.75f, 0.50f), PxVec3(0.88f, 0, 0.70f)) ||
		!AttachCollisionBox(ChassisBody, *DefaultMaterial, PxVec3(1.15f, 0.92f, 0.41f), K2PVector(BedLocation) + PxVec3(0, 0, 0.32f)))
		return false;

	State->Truck->setMass(TruckMass);
	State->Truck->setCMassLocalPose(PxTransform(CenterOfMass));
	State->Truck->setMassSpaceInertiaTensor(PxVec3(2700, 5800, 6200));
	State->Truck->setSolverIterationCounts(10, 3);

	PxVehicleWheelsSimData* Sim = PxVehicleWheelsSimData::allocate(WheelCount);
	if (!Sim) return false;
	PxReal SprungMasses[WheelCount];
	PxVehicleComputeSprungMasses(WheelCount, WheelCenters, CenterOfMass, TruckMass, 2, SprungMasses);
	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		PxVehicleWheelData Wheel;
		Wheel.mRadius = WheelRadius;
		Wheel.mWidth = WheelWidth;
		Wheel.mMass = 92.0f;
		Wheel.mMOI = 0.5f * Wheel.mMass * WheelRadius * WheelRadius;
		Wheel.mDampingRate = 0.38f;
		Wheel.mMaxBrakeTorque = 5800.0f;
		Wheel.mMaxSteer = 0.55f;
		Sim->setWheelData(Index, Wheel);

		PxVehicleSuspensionData Suspension;
		Suspension.mSprungMass = SprungMasses[Index];
		Suspension.mMaxCompression = 0.34f;
		Suspension.mMaxDroop = 0.40f;
		Suspension.mSpringStrength = 42000.0f;
		Suspension.mSpringDamperRate = 6200.0f;
		Sim->setSuspensionData(Index, Suspension);

		PxVehicleTireData Tire;
		Tire.mType = 0;
		Tire.mFrictionVsSlipGraph[0][1] = 0.94f;
		Tire.mFrictionVsSlipGraph[1][0] = 0.16f;
		Tire.mFrictionVsSlipGraph[1][1] = 1.0f;
		Tire.mFrictionVsSlipGraph[2][0] = 0.75f;
		Tire.mFrictionVsSlipGraph[2][1] = 0.70f;
		Sim->setTireData(Index, Tire);
		const PxVec3 Offset = WheelCenters[Index] - CenterOfMass;
		const PxVec3 ForcePoint(Offset.x, Offset.y, -0.28f);
		Sim->setWheelCentreOffset(Index, Offset);
		Sim->setSuspTravelDirection(Index, PxVec3(0, 0, -1));
		Sim->setSuspForceAppPointOffset(Index, ForcePoint);
		Sim->setTireForceAppPointOffset(Index, ForcePoint);
		Sim->setWheelShapeMapping(Index, static_cast<PxI32>(ExistingShapes + Index));
	}
	Sim->setSubStepCount(7.0f, 3, 2);
	Sim->setMinLongSlipDenominator(2.5f);
	for (PxU32 Axle = 0; Axle < 2; ++Axle)
	{
		PxVehicleAntiRollBarData Bar;
		Bar.mWheel0 = Axle * 2;
		Bar.mWheel1 = Axle * 2 + 1;
		Bar.mStiffness = 5600.0f;
		Sim->addAntiRollBarData(Bar);
	}
	State->Vehicle = PxVehicleNoDrive::create(Core.GetPhysics(), State->Truck, *Sim);
	Sim->free();
	if (!State->Vehicle) return false;
	State->Vehicle->setToRestState();

	PxBatchQueryDesc BatchDesc(0, WheelCount, 0);
	BatchDesc.queryMemory.userSweepResultBuffer = State->SweepResults;
	BatchDesc.queryMemory.userSweepTouchBuffer = State->SweepHits;
	BatchDesc.queryMemory.sweepTouchBufferSize = WheelCount * HitsPerWheel;
	BatchDesc.preFilterShader = FilterSuspensionSweep;
	State->Batch = Scene->createBatchQuery(BatchDesc);
	State->Friction = CreateTireFrictionPairs(*Core.GetPhysics(), *DefaultMaterial);
	if (!State->Batch || !State->Friction) return false;

	State->VisualPoseTimestamp = Scene->getTimestamp();
	State->PreviousVisualPose = State->CurrentVisualPose = State->ReadVisualPose(ChassisBody.Scale3D);
	State->PhysScene = ChassisBody.Scene;
	State->PreSimulateHandle = State->PhysScene->OnPreSimulate.AddRaw(State.get(), &FMonsterTruckPhysics::ResolveLanding);
	Physics = std::move(State);
	return true;
}

void AMonsterTruckPawn::UpdateVehicle(float DeltaTime)
{
	const FVector DriveInput = ConsumeMovementInputVector();
	if (!Physics || DeltaTime <= 0.0f) return;

	const auto* Controller = GetController();
	const bool bBrake = Controller && Controller->IsKeyDown(static_cast<int32>(EKeyCode::Space));
	const bool bBoostHeld = Controller && Controller->IsKeyDown(static_cast<int32>(EKeyCode::Shift));
	const bool bResetDown = Controller && Controller->IsKeyDown(static_cast<int32>(EKeyCode::R));
	const bool bReset = bResetDown && !bResetKeyDown;
	bResetKeyDown = bResetDown;
	if (bReset)
		Chassis->SetWorldLocationAndRotation(SpawnPose.Location, SpawnPose.Rotation.ToFRotator(), false);

	// 현재 씬이 한 프레임에 처리할 수 있는 물리 시간만큼만 차량을 적분한다.
	const float Step = FMath::Min(DeltaTime, PhysicsStep * MaxPhysicsStepsPerFrame);

	auto& State = *Physics;
	auto* Scene = Chassis->BodyInstance.Scene->GetPxScene();
	SCOPED_SCENE_WRITE_LOCK(Scene);
	if (bReset)
	{
		State.Truck->setGlobalPose(K2PTransform(SpawnPose));
		State.Vehicle->setToRestState();
		State.bStuntCrest = false;
		State.bStuntFlipInAir = false;
		State.bLandingRecovery = false;
		Steering = 0.0f;
		bResetCameraLag = true;
	}

	const float ForwardSpeed = State.Vehicle->computeForwardSpeed();
	const float Speed = std::abs(ForwardSpeed);
	const float SpeedBlend = FMath::Clamp((Speed - 4.0f) / 15.0f, 0.0f, 1.0f);
	Steering = FMath::Lerp(Steering, FMath::Clamp(DriveInput.Y, -1.0f, 1.0f), 1.0f - std::exp(-7.0f * Step));
	const float FrontSteer = Steering * FMath::Lerp(0.47f, 0.20f, SpeedBlend);
	const float RearSteer = -FrontSteer * FMath::Lerp(0.56f, 0.16f, SpeedBlend);
	const float Throttle = FMath::Clamp(DriveInput.X, -1.0f, 1.0f);
	const bool bDirectionBrake = Speed > 0.7f && Throttle * ForwardSpeed < 0.0f;
	const bool bBraking = bBrake || bDirectionBrake;
	const bool bBoosting = bBoostHeld && Throttle > 0.0f && !bBraking;
	float SpeedLimit = 23.0f;
	if (Throttle < 0.0f) SpeedLimit = 6.0f;
	if (bBoosting) SpeedLimit = 36.0f;
	const float DriveTorque = bBoosting ? 5200.0f : 3100.0f;
	const float TorqueFactor = FMath::Clamp((SpeedLimit - Speed) / 3.0f, 0.0f, 1.0f);
	PxU32 GroundedWheels = 0;
	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		const PxWheelQueryResult& Contact = State.WheelResults[Index];
		GroundedWheels += !Contact.isInAir;
		const float Slip = std::abs(Contact.longitudinalSlip);
		const float GripControl = Contact.isInAir ? 0.25f :
			1.0f - 0.62f * FMath::Clamp((Slip - 0.18f) / 0.50f, 0.0f, 1.0f);
		State.Vehicle->setDriveTorque(Index, bBraking ? 0.0f : Throttle * DriveTorque * TorqueFactor * GripControl);
		State.Vehicle->setBrakeTorque(Index, bBraking ? 5800.0f : Throttle == 0.0f ? 190.0f : 0.0f);
		State.Vehicle->setSteerAngle(Index, Index < 2 ? FrontSteer : RearSteer);
	}
	if (bBoosting && GroundedWheels >= 2 && TorqueFactor > 0.0f)
	{
		const PxVec3 Forward = State.Truck->getGlobalPose().q.rotate(PxVec3(1, 0, 0));
		State.Truck->addForce(Forward * (2.15f * Throttle * TorqueFactor), PxForceMode::eACCELERATION);
	}
	State.UpdateStuntFlip(*Scene, GroundedWheels);

	PxVehicleWheels* Vehicles[] = { State.Vehicle };
	PxVehicleSuspensionSweeps(State.Batch, 1, Vehicles, WheelCount, State.SweepResults, HitsPerWheel, nullptr, 0.98f, 1.0f);
	PxVehicleWheelQueryResult Results{ State.WheelResults, WheelCount };
	PxVehicleUpdates(Step, Scene->getGravity(), *State.Friction, 1, Vehicles, &Results);
	State.CaptureWheelEffects();
}

void AMonsterTruckPawn::Tick(float DeltaTime)
{
	if (!Physics) return;

	auto& State = *Physics;
	const FBodyInstance& Body = Chassis->BodyInstance;
	auto* Scene = Body.Scene->GetPxScene();
	PxU32 CompletedSteps = 0;
	{
		SCOPED_SCENE_READ_LOCK(Scene);
		const PxU32 Timestamp = Scene->getTimestamp();
		CompletedSteps = Timestamp - State.VisualPoseTimestamp;
		if (CompletedSteps > 0 || bResetCameraLag)
		{
			State.PreviousVisualPose = State.CurrentVisualPose;
			State.CurrentVisualPose = State.ReadVisualPose(Body.Scale3D);
			State.VisualPoseTimestamp = Timestamp;
		}
	}

	const float CompletedTime = static_cast<float>(CompletedSteps) * PhysicsStep;
	if (CompletedSteps > 0 || bResetCameraLag)
		State.UpdateWheelEffects(*this, CompletedTime, bResetCameraLag);
	if (CompletedSteps > 0)
		State.VisualPoseInterval = CompletedTime;
	// PhysScene은 최대 스텝 수에 도달하면 해당 프레임의 남은 누적 시간을 비운다.
	State.TimeSincePhysicsStep = CompletedSteps >= MaxPhysicsStepsPerFrame ? 0.0f :
		FMath::Clamp(State.TimeSincePhysicsStep + DeltaTime - CompletedTime, 0.0f, PhysicsStep);
	if (bResetCameraLag)
		State.PreviousVisualPose = State.CurrentVisualPose;

	// 화면을 한 물리 스텝 지연해 같은 시점의 차체와 바퀴를 함께 보간한다.
	const float Alpha = FMath::Clamp(1.0f - (PhysicsStep - State.TimeSincePhysicsStep) / State.VisualPoseInterval,
		0.0f, 1.0f);
	const FTransform ChassisPose = InterpolateVisualPose(State.PreviousVisualPose.Chassis, State.CurrentVisualPose.Chassis, Alpha);
	Presentation->SetTransform(ChassisPose.GetRelativeTransform(GetActorTransform()));
	for (PxU32 Index = 0; Index < WheelCount; ++Index)
		Wheels[Index]->SetTransform(InterpolateVisualPose(
			State.PreviousVisualPose.Wheels[Index], State.CurrentVisualPose.Wheels[Index], Alpha));
	for (PxU32 Axle = 0; Axle < 2; ++Axle)
	{
		const FVector A = Wheels[Axle * 2]->GetRelativeLocation();
		const FVector B = Wheels[Axle * 2 + 1]->GetRelativeLocation();
		const float Roll = std::atan2(B.Z - A.Z, B.Y - A.Y);
		Axles[Axle]->SetTransform(FTransform(P2KQuat(PxQuat(Roll, PxVec3(1, 0, 0))), (A + B) * 0.5f));
	}
	if (bResetCameraLag)
	{
		bResetCameraLag = false;
		CameraArm->ResetCameraLag();
	}
}
