#pragma once

#include "UObject/Object.h"
#include "Math/Vector.h"
#include "Math/Quat.h"
#include "BodySetupCore.h"
#include "BodyInstance.h"   // UPROPERTY 구조체 멤버는 리플렉션 등록 코드가 완전한 타입을 요구한다
#include "BodySetup.generated.h"

namespace physx
{
	class PxConvexMesh;
	class PxTriangleMesh;
	class PxRigidActor;
}

// 충돌 형상 하나. Center/Rotation은 바디(컴포넌트 또는 본) 로컬 공간 기준이다.
struct FKShapeElem
{
	FVector Center = FVector(0, 0, 0);
	FQuat Rotation = FQuat::Identity();
};

struct FKSphereElem : FKShapeElem
{
	float Radius = 0.5f;
};

struct FKBoxElem : FKShapeElem
{
	FVector Extent = FVector(0.5f, 0.5f, 0.5f);   // 반 크기
};

// 캡슐. 로컬 Z축 방향으로 눕지 않고 선다. Length는 양 끝 반구를 뺀 원통 길이.
struct FKSphylElem : FKShapeElem
{
	float Radius = 0.25f;
	float Length = 1.0f;
};

struct FKConvexElem : FKShapeElem
{
	TArray<FVector> VertexData;
};

struct FKAggregateGeom
{
	TArray<FKSphereElem> SphereElems;
	TArray<FKBoxElem> BoxElems;
	TArray<FKSphylElem> SphylElems;
	TArray<FKConvexElem> ConvexElems;

	int32 GetElementCount() const { return SphereElems.Num() + BoxElems.Num() + SphylElems.Num() + ConvexElems.Num(); }
};

// 바디를 단순 형상(AggGeom)과 삼각형 메시(Complex) 중 무엇으로 만들지.
// 씬 쿼리에 Simple/Complex 구분이 없으므로, 앞의 두 값은 지금은 같게 동작한다 (단순 형상만 만든다).
enum class ECollisionTraceFlag : uint8
{
	UseSimpleAndComplex,   // 단순 형상으로 충돌·쿼리한다. 삼각형은 Complex 쿼리용으로 남겨 둔다
	UseSimpleAsComplex,    // 단순 형상만 쓴다
	UseComplexAsSimple,    // 삼각형 메시로 충돌·쿼리한다. Static 바디만 가능하고, Movable·트리거 바디는 단순 형상으로 만든다
};

// 바디의 형상 정의. StaticMesh는 하나, PhysicsAsset은 본마다 하나 갖는다.
UCLASS()
class UBodySetup : public UBodySetupCore
{
	GENERATED_BODY()
public:
	virtual ~UBodySetup() override;   // Cooking한 PxConvexMesh·PxTriangleMesh를 해제한다

	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	FKAggregateGeom AggGeom;

	// 이 바디를 인스턴스화할 때 복사해 갈 기본 설정 (질량, 댐핑, 충돌, 물리 머티리얼).
	// PhysicsAsset 바디마다 값이 다르다. 시뮬레이션 여부는 여기가 아니라 PhysicsType이 정한다.
	UPROPERTY(EditAnywhere)
	FBodyInstance DefaultInstance;

	// 형상을 만드는 쪽(StaticMesh 등)이 정한다. 저장하지 않는다.
	ECollisionTraceFlag CollisionTraceFlag = ECollisionTraceFlag::UseSimpleAndComplex;

	// ConvexElems를 Cooking해 PxConvexMesh로 만든다. 이미 있으면 그대로 둔다. 하나라도 실패하면 false.
	bool CreatePhysicsMeshes();
	// Cooking 결과를 버린다. VertexData를 바꿨으면 부른 뒤 다시 CreatePhysicsMeshes한다.
	void ClearPhysicsMeshes();
	// ConvexElems[Index]의 Cooking 결과. 실패했거나 아직 안 만들었으면 nullptr.
	physx::PxConvexMesh* GetConvexMesh(int32 Index) const;

	// 삼각형 그대로의 충돌(Complex). Static 바디만 쓴다. PhysX는 동적 시뮬레이션에 TriangleMesh를 허용하지 않는다.
	TArray<FVector> TriMeshVertices;
	TArray<uint32> TriMeshIndices;          // 3개씩 삼각형
	bool bFlipTriMeshNormals = false;       // 엔진 감김 순서가 PhysX와 반대면 true (메시를 넣는 쪽이 판정한다)

	// 처음 불릴 때 Cooking한다. 데이터가 없거나 실패하면 nullptr.
	physx::PxTriangleMesh* GetOrCreateTriMesh();

	// 형상마다 PxShape를 만들어 Actor에 붙인다. Scale3D는 형상 크기에 곱하고,
	// 충돌 플래그·필터·머티리얼은 OwningInstance의 설정을 따른다. Actor가 씬에 들어가기 전에 부른다.
	void AddShapesToRigidActor(FBodyInstance* OwningInstance, physx::PxRigidActor& Actor, const FVector& Scale3D);

private:
	// ConvexElems와 인덱스가 같다. 같은 BodySetup을 쓰는 모든 바디가 이 메시를 공유한다 (PhysX 참조 수).
	TArray<physx::PxConvexMesh*> CookedConvexMeshes;

	physx::PxTriangleMesh* CookedTriMesh = nullptr;
	bool bTriMeshCookAttempted = false;     // 실패한 메시를 매번 다시 Cooking하지 않는다
};