#pragma once

// 엔진 수학 타입 <-> PhysX 타입 변환. K2P = Kurisu → PhysX, P2K = PhysX → Kurisu.

#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Physics/PhysXIncludes.h"

// FVector <-> PxVec3
inline physx::PxVec3 K2PVector(const FVector& V)
{
	return physx::PxVec3(V.X, V.Y, V.Z);
}

inline FVector P2KVector(const physx::PxVec3& V)
{
	return FVector(V.x, V.y, V.z);
}

// FQuat <-> PxQuat
// PhysX는 단위 쿼터니언이 아니면 checked/debug 빌드에서 에러를 낸다. 넘기기 전에 정규화한다.
inline physx::PxQuat K2PQuat(const FQuat& Q)
{
	const FQuat N = Q.Normalized();
	return physx::PxQuat(N.X, N.Y, N.Z, N.W);
}

inline FQuat P2KQuat(const physx::PxQuat& Q)
{
	return FQuat(Q.x, Q.y, Q.z, Q.w);
}

// PhysX 캡슐은 로컬 X축으로 눕고 엔진 캡슐은 로컬 Z축으로 선다. Y축 기준 90도 돌려 X를 Z에 맞춘다.
// 바디 shape의 로컬 포즈와 캡슐 쿼리의 회전에 오른쪽으로 곱한다.
inline physx::PxQuat CapsuleAxisFix()
{
	return physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0, 1, 0));
}

// FTransform <-> PxTransform
// PxTransform에는 스케일이 없다. 스케일은 shape 크기(반지름, 반높이, extent)에 곱해서 반영한다 (A4/A5).
inline physx::PxTransform K2PTransform(const FTransform& T)
{
	return physx::PxTransform(K2PVector(T.Location), K2PQuat(T.Rotation));
}

// 물리 결과를 컴포넌트에 되돌릴 때는 원래 스케일을 유지해야 하므로 스케일을 따로 받는다.
inline FTransform P2KTransform(const physx::PxTransform& T, const FVector& Scale = FVector(1.0f, 1.0f, 1.0f))
{
	return FTransform(P2KQuat(T.q), P2KVector(T.p), Scale);
}