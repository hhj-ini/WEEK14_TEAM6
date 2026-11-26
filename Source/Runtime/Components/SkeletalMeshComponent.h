#pragma once

#include "SkinnedMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Clothing/ClothingTypes.h"
#include "SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimSequence;
class UBlendSpace1D;
class UWorld;
class FLineBatcher;
class FClothingSimulationNv;

// World의 SkeletalMeshComponent마다 root 디버그를 그린다 (DrawCollisionShapes와 같은 방식). PIE의 Show Root에서 호출
void DrawSkeletalRootDebug(UWorld* World, FLineBatcher* LineBatcher);
void DrawPhysicsBodiesDebug(UWorld* World, FLineBatcher* LineBatcher);

class UAnimationAsset;
class UAnimSingleNodeInstance;
class UAnimGraphInstance;

struct FBodyInstance;
struct FConstraintInstance;
class UPhysicsAsset;

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()

public:
    USkeletalMeshComponent();
    ~USkeletalMeshComponent() override;
    bool SetAnimBlueprint(const FString& Path);
    bool ReloadAnimBlueprint();
    UAnimGraphInstance* CreateAnimGraph();
    const FString& GetAnimBlueprintPath() const;
    const FString& GetAnimBlueprintError() const;
	void SetAnimInstance(UAnimInstance* InInstance);
	UAnimSingleNodeInstance* GetSingleNodeInstance() const;
	UAnimInstance* GetAnimInstance() const { return AnimInstance; }
	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;

	const TArray<FBoneInfo>& GetBones() const;
	int32 FindBoneIndex(const FString& Name) const;
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local);
	const TArray<FTransform>& GetBoneSpaceTransforms() const { return BoneSpaceTransforms; }
	void SetBoneSpaceTransforms(const struct FCompactPose& Pose); // 애니메이션 평가 결과를 받는 입구
	FMatrix GetBoneComponentMatrix(int32 BoneIndex) const;
	FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;

	void ResetPose();
	void RefreshBoneTransformsNow();

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	void SetAnimation(UAnimationAsset* NewAnimToPlay);
	void Play(bool bLooping = true);
	void Stop();
	bool IsPlaying() const;
	void PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLoop = true);
	void RefreshAnimationPose();

	UPROPERTY()
	float BlendSpaceInput = 300.0f;

	// 루트 모션: 이번 Tick까지 모인 이동량(월드 공간). CharacterMovement가 가져가 Actor를 움직인다
	bool HasPendingRootMotion() const { return bHasPendingRootMotion; }
	FTransform ConsumeRootMotion();

	// root 부모 공간 delta → 월드 공간 delta (Unreal ConvertLocalRootMotionToWorld + Armature 변환). 시퀀스 뷰어 미리보기도 쓴다
	FTransform ConvertRootMotionToWorld(const FTransform& ParentSpaceDelta) const;

	// 루트 모션이 뽑히는 본. "root"를 먼저, 없으면 첫 본 (UAnimSequence::FindRootTrack과 같은 규칙)
	int32 GetRootMotionBoneIndex() const;

	// 디버그: root 본 구체 + 액터 위치(root 높이)에서 root까지의 선
	void DrawRootDebug(FLineBatcher* LineBatcher) const;

	// Cloth 시뮬레이션 (언리얼 USkeletalMeshComponent 구조와 동일)
	void CreateClothingSimulation();
	void DestroyClothingSimulation();
	void ForceClothNextUpdateTeleport() { bClothTeleportPending = true; }
	void SetClothSimulationSuspended(bool bSuspend);
	bool IsClothSimulationSuspended() const { return bSuspendClothingSimulation; }
	FClothingSimulationNv* GetClothingSimulation() const { return ClothingSimulation.get(); }
	const FClothingSimulationData& GetClothSimData() const { return ClothSimData; }
	static void RefreshClothingOnAllComponents(USkeletalMesh* TargetMesh);

	UPROPERTY()
	bool bEnableClothingSimulation = true;

	// Physics Asset 추가
	UPhysicsAsset* GetPhysicsAsset() const;

	int32 FindBodyIndexByBone(const FString& BoneName) const;
	FBodyInstance* GetBodyInstanceByBone(const FString& BoneName) const;

	TArray<FBodyInstance*> Bodies;
	TArray<FConstraintInstance*> Constraints;

	bool CanBodiesCollide(const FBodyInstance& A, const FBodyInstance& B) const override;

	bool IsRagdollSimulating() const;

	void SetSimulatePhysics(bool bSimulate) override;

	// 바디(PhysX 실제 포즈)와 조인트를 와이어로 그린다
	void DrawPhysicsDebug(FLineBatcher* LineBatcher) const;

protected:
	// 단일 애니메이션 에셋 경로/루프/재생 속도를 씬에 저장·복원한다 (Serialize에서 부른다)
	void SerializeSingleAnimation(FStructuredArchive::FRecord& Record);

	void RefreshBoneTransforms() override;
	void OnSkeletalMeshChanged() override;

	// Physics Asset
	virtual bool ShouldCreatePhysicsState() const override;
	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;

private:
	void UpdateClothingSimulation(float DeltaTime);
    UPROPERTY()
    FString AnimBlueprintPath;

    TUniquePtr<UAnimGraphInstance> ManagedAnimGraph;
    FString AnimBlueprintError;
    bool bBlueprintReloadRequested = false;
    bool bUpdatingAnimation = false;

	UPROPERTY()
	UAnimInstance* AnimInstance = nullptr;

	TArray<FTransform> BoneSpaceTransforms;

	FTransform PendingRootMotion;           // 월드 공간 누적값
	bool bHasPendingRootMotion = false;

	TUniquePtr<FClothingSimulationNv> ClothingSimulation;
	FClothingSimulationData ClothSimData;
	bool bClothTeleportPending = true;
	bool bSuspendClothingSimulation = false;

	// Physics Asset
	void InstantiatePhysicsAssetBodies_Internal();
	void TermArticulated();

	void UpdateKinematicBonesToAnim();
	bool bTeleportBodiesNextUpdate = true;

	void ApplyPhysicsToBonePose();
};
