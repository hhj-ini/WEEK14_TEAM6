#include "EnginePCH.h"
#include "ConstraintInstance.h"
#include "BodyInstance.h"
#include "PhysicsCore.h"
#include "PhysicsLock.h"
#include "PhysXConversions.h"
#include "PhysXIncludes.h"
#include "Math/EngineMath.h"

namespace
{
	// 축 두 개 + 위치 → PxTransform
	physx::PxTransform MakeJointFrame(const FVector& Pos, const FVector& Pri, const FVector& Sec, const FVector& Scale)
	{
		// Sec가 Pri와 정확히 직교하지 않을 수 있으니 다시 직교화한다 (그람-슈미트)
		const physx::PxVec3 X = K2PVector(Pri).getNormalized();
		physx::PxVec3 Y = K2PVector(Sec);
		Y = (Y - X * X.dot(Y)).getNormalized();
		const physx::PxVec3 Z = X.cross(Y);   // 오른손 좌표계 기저 → 행렬식 +1인 순수 회전

		// PxMat33(열0, 열1, 열2) — 각 열이 조인트 축이 된다
		const physx::PxQuat Q = physx::PxQuat(physx::PxMat33(X, Y, Z)).getNormalized();

		// 바디 형상이 Scale3D만큼 커졌으니 조인트 위치도 같이 늘린다. 축 방향은 스케일을 무시한다.
		const FVector ScaledPos(Pos.X * Scale.X, Pos.Y * Scale.Y, Pos.Z * Scale.Z);
		return physx::PxTransform(K2PVector(ScaledPos), Q);
	}

	// Limited인데 각도가 0이면 PhysX가 거부한다 (lower < upper, cone > 0 필요) → Locked로 본다
	EAngularConstraintMotion Resolve(EAngularConstraintMotion Motion, float Degrees)
	{
		if (Motion == EAngularConstraintMotion::Limited && Degrees < 0.5f)
			return EAngularConstraintMotion::Locked;
		return Motion;
	}

	physx::PxD6Motion::Enum ToPxMotion(EAngularConstraintMotion Motion)
	{
		switch (Motion)
		{
		case EAngularConstraintMotion::Free:    return physx::PxD6Motion::eFREE;
		case EAngularConstraintMotion::Limited: return physx::PxD6Motion::eLIMITED;
		default:                                return physx::PxD6Motion::eLOCKED;
		}
	}

	// 원뿔 각도는 (0, π) 사이여야 한다
	float ConeRadians(float Degrees)
	{
		return FMath::DegreesToRadians(std::clamp(Degrees, 0.5f, 179.0f));
	}
}

FConstraintInstance::~FConstraintInstance()
{
	TermConstraint();
}

bool FConstraintInstance::InitConstraint(FBodyInstance* Body1, FBodyInstance* Body2)
{
	TermConstraint();

	if (!Body1 || !Body2 || !Body1->IsValidBodyInstance() || !Body2->IsValidBodyInstance())
		return false;

	physx::PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();
	if (!Physics)
		return false;

	const physx::PxTransform Frame1 = MakeJointFrame(Pos1, PriAxis1, SecAxis1, Body1->Scale3D);
	const physx::PxTransform Frame2 = MakeJointFrame(Pos2, PriAxis2, SecAxis2, Body2->Scale3D);

	// 씬에 들어간 액터에 조인트를 붙이는 것도 쓰기다
	SCOPED_SCENE_WRITE_LOCK(Body1->Actor->getScene());

	// actor0 = 부모, actor1 = 자식 (UE·예제 코드와 같은 순서). 각자 자기 로컬 프레임을 받는다.
	physx::PxD6Joint* Joint = physx::PxD6JointCreate(*Physics, Body2->Actor, Frame2, Body1->Actor, Frame1);
	if (!Joint)
	{
		KRS_LOG(Warning, "[Physics] PxD6JointCreate failed ({} - {})", ConstraintBone2.ToString(), ConstraintBone1.ToString());
		return false;
	}
	// 위치 3축은 잠근다 (D6 기본값도 LOCKED지만 의도를 드러내려고 명시한다)
	Joint->setMotion(physx::PxD6Axis::eX, physx::PxD6Motion::eLOCKED);
	Joint->setMotion(physx::PxD6Axis::eY, physx::PxD6Motion::eLOCKED);
	Joint->setMotion(physx::PxD6Axis::eZ, physx::PxD6Motion::eLOCKED);

	const EAngularConstraintMotion Twist = Resolve(TwistMotion, TwistLimitDegrees);
	const EAngularConstraintMotion Swing1 = Resolve(Swing1Motion, Swing1LimitDegrees);
	const EAngularConstraintMotion Swing2 = Resolve(Swing2Motion, Swing2LimitDegrees);

	Joint->setMotion(physx::PxD6Axis::eTWIST, ToPxMotion(Twist));
	Joint->setMotion(physx::PxD6Axis::eSWING1, ToPxMotion(Swing1));
	Joint->setMotion(physx::PxD6Axis::eSWING2, ToPxMotion(Swing2));

	if (Twist == EAngularConstraintMotion::Limited)
	{
		const float T = FMath::DegreesToRadians(TwistLimitDegrees);
		Joint->setTwistLimit(physx::PxJointAngularLimitPair(-T, T));
	}

	// Swing은 원뿔 하나(Y각, Z각)로 같이 설정한다. 한쪽만 Limited여도 두 값 모두 유효해야 한다.
	if (Swing1 == EAngularConstraintMotion::Limited || Swing2 == EAngularConstraintMotion::Limited)
	{
		Joint->setSwingLimit(physx::PxJointLimitCone(ConeRadians(Swing1LimitDegrees), ConeRadians(Swing2LimitDegrees)));
	}

	// PVD에서 조인트 프레임·한계를 볼 수 있게 한다
	Joint->setConstraintFlag(physx::PxConstraintFlag::eVISUALIZATION, true);
	// eCOLLISION_ENABLED는 기본으로 꺼져 있다 → 조인트로 이어진 두 바디는 서로 충돌하지 않는다 (작업 6과 연결)

	ConstraintData = Joint;
	return true;
}

void FConstraintInstance::TermConstraint()
{
	if (!ConstraintData)
		return;

	physx::PxRigidActor* Actor0 = nullptr;
	physx::PxRigidActor* Actor1 = nullptr;
	ConstraintData->getActors(Actor0, Actor1);
	physx::PxScene* Scene = Actor0 ? Actor0->getScene() : (Actor1 ? Actor1->getScene() : nullptr);

	SCOPED_SCENE_WRITE_LOCK(Scene);   // Scene이 null이면 잠그지 않는다
	ConstraintData->release();
	ConstraintData = nullptr;
}

void FConstraintInstance::CopyConstraintParamsFrom(const FConstraintInstance& From)
{
	ConstraintBone1 = From.ConstraintBone1;
	ConstraintBone2 = From.ConstraintBone2;
	Pos1 = From.Pos1; PriAxis1 = From.PriAxis1; SecAxis1 = From.SecAxis1;
	Pos2 = From.Pos2; PriAxis2 = From.PriAxis2; SecAxis2 = From.SecAxis2;
	Swing1Motion = From.Swing1Motion;
	Swing2Motion = From.Swing2Motion;
	TwistMotion = From.TwistMotion;
	Swing1LimitDegrees = From.Swing1LimitDegrees;
	Swing2LimitDegrees = From.Swing2LimitDegrees;
	TwistLimitDegrees = From.TwistLimitDegrees;
	// ConstraintData는 복사하지 않는다 — 같은 조인트를 두 번 release하게 된다
}

bool FConstraintInstance::GetJointWorldPositions(FVector& OutParent, FVector& OutChild) const
{
	if (!ConstraintData)
		return false;

	physx::PxRigidActor* Actor0 = nullptr;   // 부모
	physx::PxRigidActor* Actor1 = nullptr;   // 자식
	ConstraintData->getActors(Actor0, Actor1);
	if (!Actor0 || !Actor1)
		return false;

	SCOPED_SCENE_READ_LOCK(Actor0->getScene());
	// 액터 월드 포즈 × 조인트 로컬 프레임 = 조인트 프레임의 월드 포즈
	const physx::PxTransform World0 = Actor0->getGlobalPose() * ConstraintData->getLocalPose(physx::PxJointActorIndex::eACTOR0);
	const physx::PxTransform World1 = Actor1->getGlobalPose() * ConstraintData->getLocalPose(physx::PxJointActorIndex::eACTOR1);
	OutParent = P2KVector(World0.p);
	OutChild = P2KVector(World1.p);
	return true;
}
