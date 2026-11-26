#pragma once

#include "Core/Delegate/Delegate.h"
#include "Core/Types.h"
#include "PhysicsQueryTypes.h"

namespace physx
{
	class PxScene;
}

class UWorld;
class UPrimitiveComponent;
class FPhysXSimulationEventCallback;
class FPhysXSimulationFilterCallback;

// onContact에서 모은 충돌 한 건. 디스패치 때 양쪽에 DispatchHit을 보낸다.
struct FCollisionNotifyInfo
{
	UPrimitiveComponent* ComponentA = nullptr;
	UPrimitiveComponent* ComponentB = nullptr;
	FVector ImpactPoint;
	FVector Normal;          // A가 B에서 빠져나가는 방향
	float ImpulseSize = 0.0f;
};

// onTrigger에서 모은 들어옴/나감 한 건.
struct FTriggerNotifyInfo
{
	UPrimitiveComponent* TriggerComponent = nullptr;
	UPrimitiveComponent* OtherComponent = nullptr;
	bool bBegin = true;
};

// UWorld 하나가 소유하는 PhysX 씬. 생성자에서 PxScene을 만들고 소멸자에서 해제한다.
class FPhysScene
{
public:
	explicit FPhysScene(UWorld* InOwningWorld);
	~FPhysScene();

	FPhysScene(const FPhysScene&) = delete;
	FPhysScene& operator=(const FPhysScene&) = delete;

	bool IsValid() const { return Scene != nullptr; }
	physx::PxScene* GetPxScene() const { return Scene; }
	UWorld* GetOwningWorld() const { return OwningWorld; }

	// 씬 전체 중력 (기본 0, 0, -9.81)
	void SetGravity(const FVector& Gravity);
	FVector GetGravity() const;

	// 고정 간격으로 simulate → fetchResults를 반복하고, 움직인 바디를 컴포넌트에 반영한다.
	void Tick(float DeltaTime);
	// 메인 스레드에서 씬 쓰기 락을 잡은 채 각 고정 스텝 직전에 호출한다.
	TDelegate<float> OnPreSimulate;

	// 가장 가까운 blocking hit 하나. 맞으면 true.
	bool RaycastSingle(const FVector& Start, const FVector& End, FHitResult& OutHit, const FCollisionQueryParams& Params = FCollisionQueryParams()) const;
	bool SweepSingle(const FCollisionShape& Shape, const FQuat& Rotation, const FVector& Start, const FVector& End,
		FSweepResult& OutHit, const FCollisionQueryParams& Params = FCollisionQueryParams()) const;
	// 겹친 것 전부. 하나라도 있으면 true.
	bool OverlapMulti(const FCollisionShape& Shape, const FVector& Position, const FQuat& Rotation,
		TArray<FOverlapResult>& OutOverlaps, const FCollisionQueryParams& Params = FCollisionQueryParams()) const;

	// 시뮬레이션 이벤트 콜백이 fetchResults 도중 쌓는다. 디스패치는 Tick이 fetchResults 뒤에 한다.
	void AddCollisionNotify(const FCollisionNotifyInfo& Info) { PendingCollisionNotifies.Add(Info); }
	void AddTriggerNotify(const FTriggerNotifyInfo& Info) { PendingTriggerNotifies.Add(Info); }

private:
	void DispatchPendingNotifies();

	TUniquePtr<FPhysXSimulationEventCallback> EventCallback;
	TArray<FCollisionNotifyInfo> PendingCollisionNotifies;
	TArray<FTriggerNotifyInfo> PendingTriggerNotifies;

	void SyncActiveActors();

	static constexpr float FixedTimeStep = 1.0f / 60.0f;
	static constexpr int32 MaxSubSteps = 4;        // 한 프레임에 이보다 많이 밀리면 남은 시간은 버린다

	UWorld* OwningWorld = nullptr;
	physx::PxScene* Scene = nullptr;
	float TimeAccumulator = 0.0f;

	TUniquePtr<FPhysXSimulationFilterCallback> FilterCallback;
};
