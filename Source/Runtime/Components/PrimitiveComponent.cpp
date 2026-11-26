#include "EnginePCH.h"
#include "PrimitiveComponent.h"
#include "../Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"


#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "Physics/PhysScene.h"
#include "Physics/BodySetup.h"

namespace
{
	FString PrimitiveTypeToString(EPrimitiveType Type)
	{
		switch (Type)
		{
		case EPrimitiveType::Sphere:
			return "Sphere";
			break;
		case EPrimitiveType::Cube:
			return "Cube";
			break;
		case EPrimitiveType::Cone:
			return "Cone";
			break;
		case EPrimitiveType::Plane:
			return "Plane";
			break;
		default:
			return "";
			break;
		}
	}
}

UPrimitiveComponent::UPrimitiveComponent()
{
	//SetMaterial(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
}

UPrimitiveComponent::~UPrimitiveComponent()
{

}

void UPrimitiveComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UPrimitiveComponent::OnUnregister()
{
	Super::OnUnregister();
	// 삭제/등록 해제된 도형을 상대의 OverlapInfos에 남기지 않는다.
	ClearComponentOverlaps();
}

void UPrimitiveComponent::ClearComponentOverlaps()
{
	while (!OverlapInfos.IsEmpty())
	{
		UPrimitiveComponent* Other = OverlapInfos[OverlapInfos.Num() - 1].OtherComp;
		EndComponentOverlap(Other);
		if (Other) Other->EndComponentOverlap(this);
	}
}


void UPrimitiveComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
}

bool UPrimitiveComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMesh(WorldRay, *Mesh, GetWorldMatrix(), OutHit);
}

FPrimitiveSceneProxy* UPrimitiveComponent::CreateSceneProxy()
{
	return new FPrimitiveSceneProxy(this);
}

void UPrimitiveComponent::MarkRenderStateDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkRenderStateDirty(SceneProxy);
}

bool UPrimitiveComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMeshLocal(LocalRay, *Mesh, OutT);
}

void UPrimitiveComponent::OnTransformDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkDirty(SceneProxy);

	// 시뮬레이트 중인 바디는 물리가 주인이다. 여기서 되돌려 보내면 SyncActiveActors와 서로 덮어써 속도가 망가진다.
	if (BodyInstance.IsValidBodyInstance() && !BodyInstance.IsSimulatingPhysics())
	{
		const FTransform WorldTransform(GetWorldRotation().Quaternion(), GetWorldLocation(), GetWorldScale3D());
		// 게임 월드에서는 Kinematic target으로 옮겨 부딪힌 물체를 밀어낸다. 시뮬레이트하지 않는 월드(에디터)는 즉시 옮긴다.
		const bool bTeleport = !(GetWorld() && GetWorld()->IsGameWorld());
		BodyInstance.SetWorldTransform(WorldTransform, bTeleport);
	}
}

bool UPrimitiveComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
	return Super::MoveComponent(Delta, NewRotation, bSweep, Hit);
}

void UPrimitiveComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "bCastShadow" ||
		Property.Name == "bReceiveShadow" || 
		Property.Name == "bReceivesDecals")
	{
		MarkRenderStateDirty();
	}

	// 디테일 창은 구조체 멤버가 바뀌어도 바깥 프로퍼티(BodyInstance)를 알린다.
	// Static 바디는 시뮬레이트할 수 없으므로, Simulate Physics가 켜져 있으면 Movable로 바꾼다 (UE 에디터와 같은 동작).
	// 어느 멤버가 바뀌었는지는 알 수 없으므로 둘 다 켜진 상태면 시뮬레이트 쪽을 따른다.
	if (Property.Name == "BodyInstance" && BodyInstance.bSimulatePhysics && BodyInstance.Mobility == EBodyMobility::Static)
	{
		BodyInstance.Mobility = EBodyMobility::Movable;
	}

	// Mobility(Static↔Movable)는 액터 종류가 달라지므로 바디를 새로 만든다.
	// bGenerateOverlapEvents는 shape의 트리거 여부를 바꾸므로 같이 다시 만든다.
	if ((Property.Name == "BodyInstance" || Property.Name == "bGenerateOverlapEvents") && IsPhysicsStateCreated())
	{
		DestroyPhysicsState();
		CreatePhysicsState();
	}
}

void UPrimitiveComponent::SetCastShadow(bool NewValue)
{
	if (bCastShadow != NewValue)
	{
		bCastShadow = NewValue;
	}
}

void UPrimitiveComponent::SetReceiveShadow(bool NewValue)
{
	if (bReceiveShadow != NewValue)
	{
		bReceiveShadow = NewValue;
	}
}

void UPrimitiveComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddPrimitive(this);
}

void UPrimitiveComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemovePrimitive(this);
	Super::DestroyRenderState();
}

bool UPrimitiveComponent::TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult)
{
	FBox MeshBox = Mesh.AABB.GetWorldAABB(WorldMatrix);
	float BoxT;
	if (!RayIntersectsAABB(WorldRay, MeshBox.Min, MeshBox.Max, BoxT))
	{
		return false;
	}

	FRay LocalRay = ToLocalRay(WorldRay, WorldMatrix);
	float T = FLT_MAX;

	if (!RayIntersectsMesh(LocalRay, Mesh, T)) return false;

	OutResult.HitComponent = this;
	OutResult.Distance = T;
	OutResult.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;

	return true;
}



bool UPrimitiveComponent::TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT)
{
	return RayIntersectsMesh(LocalRay, Mesh, OutT);
}

bool UPrimitiveComponent::IsOverlappingComponent(const UPrimitiveComponent* Other) const
{
	if (!Other) return false;

	for (const FOverlapInfo& Info : OverlapInfos)
	{
		if (Info.OtherComp == Other)
			return true;
	}

	return false;
}

void UPrimitiveComponent::BeginComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp || OtherComp == this || IsOverlappingComponent(OtherComp))
	{
		return;
	}

	OverlapInfos.Add({ OtherComp });

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentBeginOverlap.Broadcast(this, OtherActor, OtherComp);
	if (Owner && OtherActor)
	{
		Owner->NotifyActorBeginOverlap(OtherActor);
	}
}

void UPrimitiveComponent::EndComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp) return;

	bool bRemoved = false;

	for (int32 Index = 0;Index < OverlapInfos.Num(); ++Index)
	{
		if (OverlapInfos[Index].OtherComp == OtherComp)
		{
			OverlapInfos.RemoveAt(Index, 1);
			bRemoved = true;
			break;
		}
	}

	if (!bRemoved)
		return;


	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentEndOverlap.Broadcast(this, OtherActor, OtherComp);

	if (Owner && OtherActor)
	{
		Owner->NotifyActorEndOverlap(OtherActor);
	}
}

void UPrimitiveComponent::DispatchHit(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp) 
		return;

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentHit.Broadcast(this, OtherActor, OtherComp);

	if (Owner && OtherActor)
	{
		Owner->NotifyHit(OtherActor);
	}
}

bool UPrimitiveComponent::ShouldCreatePhysicsState() const
{
	UWorld* World = GetWorld();
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	// NoCollision은 바디를 만들지 않는다 (렌더링만 한다).
	if (BodyInstance.CollisionEnabled == ECollisionEnabled::NoCollision)
		return false;
	// GetBodySetup은 const가 아니다 (형상을 지연 생성하는 컴포넌트가 있다).
	return Scene && Scene->IsValid() && const_cast<UPrimitiveComponent*>(this)->GetBodySetup() != nullptr;
}

void UPrimitiveComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();

	const FTransform WorldTransform(GetWorldRotation().Quaternion(), GetWorldLocation(), GetWorldScale3D());
	BodyInstance.InitBody(GetBodySetup(), WorldTransform, this, GetWorld()->GetPhysicsScene());
}

void UPrimitiveComponent::OnDestroyPhysicsState()
{
	BodyInstance.TermBody();
	// 트리거가 씬에서 빠질 때의 나감 이벤트는 PhysX가 삭제된 shape로 표시해 버리므로 여기서 End를 보낸다.
	ClearComponentOverlaps();
	Super::OnDestroyPhysicsState();
}

void UPrimitiveComponent::SetSimulatePhysics(bool bSimulate)
{
	BodyInstance.SetSimulatePhysics(bSimulate);
}
