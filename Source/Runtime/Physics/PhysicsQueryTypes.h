#pragma once

#include "Core/Types.h"
#include "Math/Vector.h"
#include "Collision/HitResult.h"
#include "PhysicsFiltering.h"

class UPrimitiveComponent;
class AActor;

// PxSweepHit 대응. Raycast와 같은 정보(위치·법선·거리 + 시작 겹침)라 FHitResult를 그대로 쓴다.
using FSweepResult = FHitResult;

// PxOverlapHit 대응. 위치·법선이 없고 "무엇과 겹쳤나"만 담는다.
struct FOverlapResult
{
	UPrimitiveComponent* Component = nullptr;
	int32 FaceIndex = -1;
};

// Sweep·Overlap에 쓰는 모양. 크기는 월드 단위. 캡슐은 Z축으로 선다.
struct FCollisionShape
{
	enum class EType : uint8 { Sphere, Box, Capsule };

	EType Type = EType::Sphere;
	FVector HalfExtent = FVector(0.5f, 0.5f, 0.5f);   // Box
	float Radius = 0.5f;                               // Sphere, Capsule
	float HalfHeight = 0.5f;                           // Capsule: 반구 끝까지 (CapsuleComponent와 같은 규약)

	static FCollisionShape MakeSphere(float InRadius) { FCollisionShape S; S.Type = EType::Sphere; S.Radius = InRadius; return S; }
	static FCollisionShape MakeBox(const FVector& InHalfExtent) { FCollisionShape S; S.Type = EType::Box; S.HalfExtent = InHalfExtent; return S; }
	static FCollisionShape MakeCapsule(float InRadius, float InHalfHeight) { FCollisionShape S; S.Type = EType::Capsule; S.Radius = InRadius; S.HalfHeight = InHalfHeight; return S; }

	// 부피가 없는 모양(반지름 0인 구 등). Sweep은 이 경우 레이로 처리한다.
	bool IsNearlyZero() const
	{
		constexpr float Tolerance = 1.e-4f;
		switch (Type)
		{
		case EType::Box:     return HalfExtent.X <= Tolerance && HalfExtent.Y <= Tolerance && HalfExtent.Z <= Tolerance;
		case EType::Capsule: return Radius <= Tolerance && HalfHeight <= Tolerance;
		default:             return Radius <= Tolerance;
		}
	}
};

// 무엇을 맞힐지. 기본은 모든 종류.
struct FCollisionQueryParams
{
	uint32 ObjectTypesMask = AllCategoriesMask;   // 맞힐 ECollisionCategory (CategoryBit OR)
	uint32 RequiredFlags = 0;                     // shape의 word3에 모두 있어야 맞는다 (예: Drivable)
	const UPrimitiveComponent* IgnoreComponent = nullptr;   // 자기 자신(차체 등) 제외
	const AActor* IgnoreActor = nullptr;                    // 이 액터의 모든 바디 제외 (이동하는 캐릭터 자신)

	// Sweep 전용. 시작부터 닿아 있던 면 중 이동 방향과 마주하지 않는 것(바닥을 딛고 옆으로 걷기 등)은 막힘으로 치지 않는다.
	// 켜면 겹침 깊이·방향(MTD)을 계산하므로 조금 더 비싸다.
	bool bIgnoreTouchesMovingAway = false;
};