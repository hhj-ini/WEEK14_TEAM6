#pragma once

#include "SceneComponent.h"
#include "../Render/Shader.h"
#include "../Render/Mesh.h"
#include "Render/RenderPacket.h"
#include "Render/GeometryGenerator.h"
#include "Collision/HitResult.h"
#include "Core/Delegate/Delegate.h"
#include "Physics/BodyInstance.h"
#include "PrimitiveComponent.generated.h"

class FPrimitiveSceneProxy;
class UBodySetup;
class AActor;
class UPrimitiveComponent;

// (Self, Other, OtherComp). 담당: C
DECLARE_DELEGATE(FComponentOverlapSignature, UPrimitiveComponent*, AActor*, UPrimitiveComponent*);
DECLARE_DELEGATE(FComponentHitSignature, UPrimitiveComponent*, AActor*, UPrimitiveComponent*);

struct FOverlapInfo
{
	UPrimitiveComponent* OtherComp = nullptr;
};

enum class EPrimitiveType
{
	Sphere,
	Cube,
	Cone,
	Plane
};

UCLASS()
class UPrimitiveComponent :public USceneComponent
{
	GENERATED_BODY()

	// Material은 여기서 등록하지 않는다. 컴포넌트마다 머티리얼을 다루는 방식이 달라서
	// (빌보드는 단일 머티리얼, 스태틱 메시는 슬롯별 덮어쓰기) 각자 등록한다.
public:
	UPrimitiveComponent();
	virtual ~UPrimitiveComponent();

	virtual void BeginPlay() override;

	// Todo: subuv
	//void SubmitToRenderQueue(TQueue<FRenderPacket>& RenderQueue);
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext);

	virtual const FStaticMeshData* GetMeshData() const { return nullptr; }

	// Outline처럼 컴포넌트를 통째로 한 번 더 그릴 때 쓰는 GPU 메시.
	// 메시가 없거나(텍스트) View에 따라 형상이 정해지는 컴포넌트(빌보드·파티클)는
	// 컴포넌트 월드 행렬만으로 같은 그림을 못 만들므로 nullptr을 돌려준다.
	virtual UStaticMesh* GetRenderMesh() const { return nullptr; }

	virtual int32 GetNumMaterials() const { return 0; }
	virtual UMaterial* GetMaterial(int32 SlotIndex) const { return nullptr; }
	virtual void SetMaterial(int32 SlotIndex, UMaterial* InMaterial) {}
	//
	// FShader* GetShader() const { return Shader.get(); };

	void SetVisible(bool bInVisible) { SetVisibility(bInVisible); }

	virtual bool LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit);
	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT);
	virtual FBox CalcLocalBounds() const override
	{
		const FStaticMeshData* Data = GetMeshData();
		return Data ? Data->AABB : Super::CalcLocalBounds();
	}

	virtual FPrimitiveSceneProxy* CreateSceneProxy();
	FPrimitiveSceneProxy* GetSceneProxy() const { return SceneProxy; }

	void MarkRenderStateDirty() override;

	virtual void OnTransformDirty() override;

	virtual bool MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit = nullptr) override;

	virtual void PostEditChangeProperty(const FProperty& Property) override;

	void SetCastShadow(bool NewValue);
	void SetReceiveShadow(bool NewValue);

	UPROPERTY(EditAnywhere)
	bool bCastShadow = true;
	UPROPERTY(EditAnywhere)
	bool bReceiveShadow = true;
	UPROPERTY(EditAnywhere)
	bool bReceivesDecals = true;

	// ---- 충돌 이벤트 (담당: C) ----
	FComponentOverlapSignature OnComponentBeginOverlap;
	FComponentOverlapSignature OnComponentEndOverlap;
	FComponentHitSignature OnComponentHit;

	UPROPERTY(EditAnywhere)
	bool bGenerateOverlapEvents = true;

	bool IsOverlappingComponent(const UPrimitiveComponent* Other) const;
	const TArray<FOverlapInfo>& GetOverlapInfos() const { return OverlapInfos; }

	// FPhysScene이 트리거 이벤트를 디스패치할 때 호출한다
	void BeginComponentOverlap(UPrimitiveComponent* OtherComp);
	void EndComponentOverlap(UPrimitiveComponent* OtherComp);
	void DispatchHit(UPrimitiveComponent* OtherComp);

	// 이 컴포넌트의 물리 바디. 설정(UPROPERTY)만 저장·복제되고 PhysX 액터 포인터는 컴포넌트마다 새로 만든다.
	UPROPERTY(EditAnywhere)
	FBodyInstance BodyInstance;

	// 바디를 만들 형상. 없으면 물리 바디를 만들지 않는다. StaticMesh·Shape 컴포넌트가 오버라이드한다.
	virtual UBodySetup* GetBodySetup() { return nullptr; }

	// 시뮬레이트 여부를 바꾼다. 바디가 있으면 바로 Kinematic ↔ Dynamic 전환한다.
	virtual void SetSimulatePhysics(bool bSimulate);

	// 두 바디가 충돌할 지
	virtual bool CanBodiesCollide(const FBodyInstance& A, const FBodyInstance& B) const { return false; }

protected:
	virtual void OnUnregister() override;

	virtual bool ShouldCreatePhysicsState() const override;
	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	TArray<FOverlapInfo> OverlapInfos;

	// 남은 겹침 쌍에 모두 End를 보낸다. 바디가 사라지면 PhysX가 나감을 알려 주지 않으므로 직접 정리한다.
	void ClearComponentOverlaps();

	virtual bool ShouldCreateRenderState() const override
	{
		return true;
	}

	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;


	bool TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult);
	bool TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT);

private:
	friend class FScene;
	FPrimitiveSceneProxy* SceneProxy = nullptr;
};
