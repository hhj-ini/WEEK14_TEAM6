#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Serialization/MathSerialization.h"
#include "Animation/BonePose.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimBlueprintCompiler.h"
#include "Animation/AnimBlueprintFile.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Casts.h"
#include "UObject/UObjectIterator.h"
#include "Render/LineBatcher.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/BlendSpaceFile.h"
#include "Asset/AssetManager.h"
#include "Clothing/ClothingAsset.h"
#include "Clothing/ClothingSimulationNv.h"
#include "Render/SkeletalMeshObject.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodyInstance.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysXConversions.h"
#include "Physics/PhysXIncludes.h"
#include "Engine/World.h"
#include "Physics/PhysicsLock.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Physics/PhysicsStats.h"

namespace
{
	FTransform MatrixToTransform(const FMatrix& M)
	{
		FVector X(M[0][0], M[0][1], M[0][2]);
		FVector Y(M[1][0], M[1][1], M[1][2]);
		FVector Z(M[2][0], M[2][1], M[2][2]);
		const FVector Scale(X.Size(), Y.Size(), Z.Size());
		X = X.GetSafeNormal(); Y = Y.GetSafeNormal(); Z = Z.GetSafeNormal();
		const physx::PxQuat Q(physx::PxMat33(K2PVector(X), K2PVector(Y), K2PVector(Z)));
		return FTransform(P2KQuat(Q.getNormalized()), FVector(M[3][0], M[3][1], M[3][2]), Scale);
	}

	// 바디 하나가 시뮬레이트할지. Default는 컴포넌트 설정을 따르고, Kinematic·Simulated는 그 값으로 고정한다
	bool ShouldBodySimulate(const UBodySetup* Setup, bool bComponentSimulate)
	{
		if (!Setup)
			return bComponentSimulate;

		switch (Setup->PhysicsType)
		{
		case EPhysicsType::Kinematic: return false;
		case EPhysicsType::Simulated: return true;
		default:                      return bComponentSimulate;
		}
	}
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
	if (AnimInstance == InInstance)
	{
		return;
	}
	if (AnimInstance)
	{
		AnimInstance->UninitializeAnimation();
	}
    AnimInstance = InInstance;

    if (ManagedAnimGraph && ManagedAnimGraph.get() != InInstance)
    {
        ManagedAnimGraph.reset();
    }

    AnimBlueprintPath.clear();
    bBlueprintReloadRequested = false;

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();
	}
}

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();

    if (!AnimBlueprintPath.empty() && (bBlueprintReloadRequested || !ManagedAnimGraph))
    {
        ReloadAnimBlueprint();
    }

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();

		if (auto* Instance = GetSingleNodeInstance())
		{
			if (Instance->GetCurrentAsset())
			{
				Instance->SetPlaying(true);
			}
		}
	}

	CreateClothingSimulation();
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    if (bBlueprintReloadRequested)
    {
        bBlueprintReloadRequested = false;
        ReloadAnimBlueprint();
    }

	const bool bRagdoll = IsRagdollSimulating();

	if (bRagdoll)
	{
		ApplyPhysicsToBonePose();
	}
	else if (AnimInstance && GetSkeletalMesh())
	{
        bUpdatingAnimation = true;
		if (auto* Instance = Cast<UAnimSingleNodeInstance>(AnimInstance))
		{
			Instance->SetBlendSpaceInput(BlendSpaceInput);
		}

		AnimInstance->UpdateAnimation(DeltaTime);
		FPoseContext Pose(AnimInstance, GetBones());
		AnimInstance->EvaluateAnimation(Pose);
		SetBoneSpaceTransforms(Pose.Pose);
		AnimInstance->TriggerAnimNotifies(DeltaTime);

		// 루트 모션: AnimInstance에 모인 delta를 월드 공간으로 바꿔 CharacterMovement가 가져갈 때까지 쌓아 둔다
		const FRootMotionMovementParams RootMotion = AnimInstance->ConsumeExtractedRootMotion();
		if (RootMotion.bHasRootMotion)
		{
			const FTransform WorldDelta = ConvertRootMotionToWorld(RootMotion.GetRootMotionTransform());
			PendingRootMotion.Location = (bHasPendingRootMotion ? PendingRootMotion.Location : FVector::ZeroVector) + WorldDelta.Location;
			PendingRootMotion.Rotation = bHasPendingRootMotion ? (WorldDelta.Rotation * PendingRootMotion.Rotation).Normalized() : WorldDelta.Rotation;
			bHasPendingRootMotion = true;
		}
        bUpdatingAnimation = false;
	}

	// 랙돌이 꺼져 있어도 PhysicsType이 Simulated인 바디는 시뮬레이트한다. 그 본만 물리 결과로 덮는다
	if (!bRagdoll)
	{
		for (const FBodyInstance* Body : Bodies)
		{
			if (Body && Body->IsSimulatingPhysics())
			{
				ApplyPhysicsToBonePose();
				break;
			}
		}
	}

	Super::TickComponent(DeltaTime);
	if (!bRagdoll)
	{
		UpdateKinematicBonesToAnim();
	}
	UpdateClothingSimulation(DeltaTime);
}

const TArray<FBoneInfo>& USkeletalMeshComponent::GetBones() const
{
	static const TArray<FBoneInfo> Empty{};
	return GetSkeletalMesh() ? GetSkeletalMesh()->GetRefSkeleton() : Empty;
}

int32 USkeletalMeshComponent::FindBoneIndex(const FString& Name) const
{
	return GetSkeletalMesh() ? GetSkeletalMesh()->FindBoneIndex(Name) : -1;
}

FTransform USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return FTransform();
	return BoneSpaceTransforms[BoneIndex];
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local)
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return;
	BoneSpaceTransforms[BoneIndex] = Local;
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::SetBoneSpaceTransforms(const FCompactPose& Pose)
{
	if (Pose.GetNumBones() != GetBones().Num())
	{
		KRS_LOG(Warning, "SetBoneSpaceTransforms: pose has {} bones, mesh has {}.", Pose.GetNumBones(), GetBones().Num());
		return;
	}
#if defined(_DEBUG)
	for (int32 i = 0; i < Pose.GetNumBones(); ++i)
	{
		const FQuat& Q = Pose[i].Rotation;
		const float LenSq = Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W;
		if (std::abs(LenSq - 1.0f) > 1e-3f)
			KRS_LOG(Warning, "Pose bone {} rotation not normalized (|q|^2 = {})", i, LenSq);
	}
#endif
	BoneSpaceTransforms = Pose.GetBones();
	MarkBoneTransformsDirty();
}

FMatrix USkeletalMeshComponent::GetBoneComponentMatrix(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= ComponentSpaceTransforms.Num()) return FMatrix::Identity;
	return ComponentSpaceTransforms[BoneIndex];
}

FMatrix USkeletalMeshComponent::GetBoneWorldMatrix(int32 BoneIndex) const
{
	return GetBoneComponentMatrix(BoneIndex) * GetWorldMatrix();
}

void USkeletalMeshComponent::ResetPose()
{
	OnSkeletalMeshChanged();
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::RefreshBoneTransformsNow()
{
	UpdateBoneTransformsNow();
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
	const TArray<FBoneInfo>& Bones = GetBones();
	if (BoneSpaceTransforms.Num() != Bones.Num())
		OnSkeletalMeshChanged();

	ComponentSpaceTransforms.SetNum(Bones.Num());
	for (int i = 0; i < Bones.Num(); ++i)
	{
		const FMatrix Local = BoneSpaceTransforms[i].GetLocalMatrix();
		const int32 Parent = Bones[i].ParentIndex;
		ComponentSpaceTransforms[i] = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
	}
}

void USkeletalMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
    FArchive& Ar = Record.GetUnderlyingArchive();
    UAnimInstance* SavedInstance = AnimInstance;

    if (Ar.IsSaving() && (!AnimBlueprintPath.empty() || ManagedAnimGraph))
    {
        AnimInstance = nullptr;
    }

    Super::Serialize(Record);

    if (Ar.IsSaving())
    {
        AnimInstance = SavedInstance;
    }

	int32 Num = Ar.IsSaving() ? BoneSpaceTransforms.Num() : 0;
	FStructuredArchive::FArray PoseArray = Record.EnterArray("BonePose", Num);

	for (int32 I = 0; I < Num; ++I)
	{
		FStructuredArchive::FRecord BoneRecord = PoseArray.EnterElement().EnterRecord();
		FString Name = Ar.IsSaving() ? GetBones()[I].Name : FString();
		FTransform Local = Ar.IsSaving() ? BoneSpaceTransforms[I] : FTransform();
		BoneRecord << SA_VALUE("Name", Name) << SA_VALUE("Local", Local);

		if (Ar.IsLoading())
		{
			// 메시의 본 순서가 바뀌어도 맞도록 이름으로 찾는다
			const int32 Index = FindBoneIndex(Name);
			if (Index >= 0)
				BoneSpaceTransforms[Index] = Local;
		}
	}

	SerializeSingleAnimation(Record);

    if (Ar.IsLoading())
    {
        if (!AnimBlueprintPath.empty())
        {
            ReloadAnimBlueprint();
        }

        RefreshBoneTransformsNow();
    }
}

bool USkeletalMeshComponent::CanBodiesCollide(const FBodyInstance& A, const FBodyInstance& B) const
{
	UPhysicsAsset* PhysAsset = GetPhysicsAsset();   // 바디가 있으면 이미 로드돼 있다 (지연 로드 안 일어남)
	if (!PhysAsset)
		return false;

	const int32 IA = A.InstanceBodyIndex;
	const int32 IB = B.InstanceBodyIndex;
	const int32 Num = PhysAsset->BodySetup.Num();
	if (IA < 0 || IB < 0 || IA >= Num || IB >= Num)
		return false;

	return PhysAsset->IsCollisionEnabled(PhysAsset->BodySetup[IA]->BoneName, PhysAsset->BodySetup[IB]->BoneName);
}

bool USkeletalMeshComponent::IsRagdollSimulating() const
{
	return BodyInstance.bSimulatePhysics && !Bodies.IsEmpty();
}

void USkeletalMeshComponent::SetSimulatePhysics(bool bSimulate)
{
	BodyInstance.bSimulatePhysics = bSimulate;

	const UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	for (FBodyInstance* Body : Bodies)
	{
		if (Body)
		{
			const int32 BodyIndex = Body->InstanceBodyIndex;
			const UBodySetup* Setup = (PhysAsset && BodyIndex >= 0 && BodyIndex < PhysAsset->BodySetup.Num())
				? PhysAsset->BodySetup[BodyIndex] : nullptr;
			Body->SetSimulatePhysics(ShouldBodySimulate(Setup, bSimulate));
		}
	}

	if (!bSimulate)
	{
		bTeleportBodiesNextUpdate = true;
	}
}

void USkeletalMeshComponent::DrawPhysicsDebug(FLineBatcher* LineBatcher) const
{
	UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	if (!LineBatcher || !PhysAsset || Bodies.IsEmpty())
		return;

	const FVector4 KinematicColor(0.3f, 0.7f, 1.0f, 1.0f);
	const FVector4 SimulatingColor(1.0f, 0.55f, 0.1f, 1.0f);

	for (const FBodyInstance* Body : Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance())
			continue;
		const int32 Index = Body->InstanceBodyIndex;
		if (Index < 0 || Index >= PhysAsset->BodySetup.Num() || !PhysAsset->BodySetup[Index])
			continue;

		// 본 행렬이 아니라 PhysX 액터의 실제 포즈로 그린다 → 바디와 메시가 어긋나면 바로 보인다.
		// GetWorldTransform에 Scale3D가 들어 있어 형상 크기도 맞는다
		const FMatrix BodyToWorld = Body->GetWorldTransform().GetLocalMatrix();
		const FVector4& Color = Body->IsSimulatingPhysics() ? SimulatingColor : KinematicColor;
		DrawAggGeomWire(*LineBatcher, BodyToWorld, PhysAsset->BodySetup[Index]->AggGeom, Color);
	}

	const FVector4 JointColor(1.0f, 1.0f, 0.2f, 1.0f);
	const FVector4 StretchedColor(1.0f, 0.15f, 0.15f, 1.0f);
	constexpr float StretchThreshold = 0.01f;   // m. 이보다 벌어지면 조인트가 늘어난 것으로 본다

	for (const FConstraintInstance* CI : Constraints)
	{
		FVector ParentPos, ChildPos;
		if (!CI || !CI->GetJointWorldPositions(ParentPos, ChildPos))
			continue;

		const bool bStretched = (ChildPos - ParentPos).Size() > StretchThreshold;
		LineBatcher->AddSphere(ParentPos, 0.02f, bStretched ? StretchedColor : JointColor, 6);
		if (bStretched)
			LineBatcher->AddLine(ParentPos, ChildPos, StretchedColor);
	}
}

void USkeletalMeshComponent::SerializeSingleAnimation(FStructuredArchive::FRecord& Record)
{
	// 디테일 패널에서 고른 단일 애니메이션(SingleNodeInstance의 에셋)은 AnimInstance 객체가 아니라
	// 에셋 경로로 저장한다. 없으면 필드를 쓰지 않으므로 예전 씬도 그대로 읽힌다.
	FArchive& Ar = Record.GetUnderlyingArchive();
	UAnimSingleNodeInstance* Instance = GetSingleNodeInstance();
	UAnimationAsset* Asset = Instance ? Instance->GetCurrentAsset() : nullptr;
	const bool bHasAnimation = Ar.IsSaving() && Asset && !Asset->GetAssetPath().empty();

	TOptional<FStructuredArchive::FSlot> AnimSlot = Record.TryEnterField("SingleAnimation", bHasAnimation);
	if (!AnimSlot.IsSet())
		return;

	FStructuredArchive::FRecord AnimRecord = AnimSlot.GetValue().EnterRecord();
	FString Path = Ar.IsSaving() ? Asset->GetAssetPath() : FString();
	bool bLoop = Ar.IsSaving() ? Instance->IsLooping() : true;
	float Rate = Ar.IsSaving() ? Instance->GetPlayRate() : 1.0f;
	AnimRecord << SA_VALUE("Asset", Path) << SA_VALUE("Looping", bLoop) << SA_VALUE("PlayRate", Rate);

	if (!Ar.IsLoading())
		return;

	// AssetManager 캐시로 불러온다 (디테일 패널이 고를 때와 같은 객체)
	UAnimationAsset* Loaded = BlendSpaceFile::IsBlendSpacePath(Path)
		? static_cast<UAnimationAsset*>(UAssetManager::LoadBlendSpace(Path))
		: static_cast<UAnimationAsset*>(UAssetManager::LoadAnimSequence(Path));
	if (!Loaded)
	{
		KRS_LOG(Warning, "SkeletalMeshComponent: single animation not found: {}", Path);
		return;
	}

	// 재생 시작은 BeginPlay가 한다 (에디터 월드에서는 첫 프레임 포즈만 보인다)
	SetAnimation(Loaded);
	if (UAnimSingleNodeInstance* NewInstance = GetSingleNodeInstance())
	{
		NewInstance->SetLooping(bLoop);
		NewInstance->SetPlayRate(Rate);
	}
}

void USkeletalMeshComponent::OnSkeletalMeshChanged()
{
    bBlueprintReloadRequested = !AnimBlueprintPath.empty();
	BoneSpaceTransforms.Reset();
	for (const FBoneInfo& Bone : GetBones())
		BoneSpaceTransforms.Add(Bone.LocalBindPose);
	CreateClothingSimulation();
}
bool USkeletalMeshComponent::ShouldCreatePhysicsState() const
{
	UWorld* World = GetWorld();
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	if (BodyInstance.CollisionEnabled == ECollisionEnabled::NoCollision)
		return false;
	return Scene && Scene->IsValid() && GetPhysicsAsset() != nullptr;
}
void USkeletalMeshComponent::OnCreatePhysicsState()
{
	UActorComponent::OnCreatePhysicsState();
	InstantiatePhysicsAssetBodies_Internal();
}
void USkeletalMeshComponent::OnDestroyPhysicsState()
{
	TermArticulated();
	ClearComponentOverlaps();
	UActorComponent::OnDestroyPhysicsState();
}
UAnimSingleNodeInstance* USkeletalMeshComponent::GetSingleNodeInstance() const
{
	return Cast<UAnimSingleNodeInstance>(AnimInstance);
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset* NewAnimToPlay)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		KRS_LOG(Warning, "SetAnimation: unsupported animation asset type");
		return;
	}
    AnimBlueprintPath.clear();
    AnimBlueprintError.clear();
    bBlueprintReloadRequested = false;
	auto* SingleNodeInstance = Cast<UAnimSingleNodeInstance>(AnimInstance);

	if (!SingleNodeInstance)
	{
		SingleNodeInstance = NewObject<UAnimSingleNodeInstance>(this);
		SetAnimInstance(SingleNodeInstance);
	}

	SingleNodeInstance->SetAnimationAsset(NewAnimToPlay);
	SingleNodeInstance->SetBlendSpaceInput(BlendSpaceInput);

	RefreshAnimationPose();
}

void USkeletalMeshComponent::Play(bool bLooping)
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetLooping(bLooping);
		Instance->SetPlaying(true);
	}
}

void USkeletalMeshComponent::Stop()
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetPlaying(false);
	}
}

bool USkeletalMeshComponent::IsPlaying() const
{
	const auto* Instance = GetSingleNodeInstance();
	return Instance && Instance->IsPlaying();
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		return;
	}

	SetAnimation(NewAnimToPlay);
	Play(bLooping);
}

void USkeletalMeshComponent::RefreshAnimationPose()
{
    if (!AnimInstance)
    {
        ResetPose();
        RefreshBoneTransformsNow();
        return;
    }

	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetBlendSpaceInput(BlendSpaceInput);
	}

	FPoseContext Pose(AnimInstance, GetBones());
	AnimInstance->EvaluateAnimation(Pose);
	SetBoneSpaceTransforms(Pose.Pose);
	RefreshBoneTransformsNow();
}


FTransform USkeletalMeshComponent::ConsumeRootMotion()
{
	const FTransform Result = PendingRootMotion;
	PendingRootMotion = FTransform();
	bHasPendingRootMotion = false;
	return Result;
}

FTransform USkeletalMeshComponent::ConvertRootMotionToWorld(const FTransform& ParentSpaceDelta) const
{
	// delta는 root 본의 부모 공간 값이다. UAL은 Armature → root 구조라 부모(Armature)를 거쳐야 컴포넌트 공간이 된다.
	const TArray<FBoneInfo>& Bones = GetBones();
	const int32 RootIndex = GetRootMotionBoneIndex();
	const int32 ParentIndex = RootIndex >= 0 ? Bones[RootIndex].ParentIndex : -1;

	// 이동: 부모(바인드 포즈) → 컴포넌트 → 월드 행렬로 방향·스케일만 적용 (Armature 스케일, 메시 0.01, yaw −180 모두 여기서)
	const FMatrix ComponentToWorld = GetWorldMatrix();
	const FMatrix ParentToWorld = ParentIndex >= 0
		? Bones[ParentIndex].InverseBindPose.Inverse() * ComponentToWorld
		: ComponentToWorld;

	FTransform Result;
	Result.Location = ParentToWorld.TransformVector(ParentSpaceDelta.Location);

	// 회전: 부모 공간 회전을 월드 공간 회전으로 바꾼다 (P × Δ × P⁻¹, P = 컴포넌트 월드 회전 × 부모 본 회전)
	FQuat ParentRotation = FQuat(GetWorldRotation());
	FQuat BoneChainRotation;
	for (int32 Index = ParentIndex; Index >= 0; Index = Bones[Index].ParentIndex)
	{
		BoneChainRotation = Bones[Index].LocalBindPose.Rotation * BoneChainRotation;
	}
	ParentRotation = (ParentRotation * BoneChainRotation).Normalized();
	Result.Rotation = (ParentRotation * ParentSpaceDelta.Rotation * ParentRotation.Inverse()).Normalized();
	return Result;
}

int32 USkeletalMeshComponent::GetRootMotionBoneIndex() const
{
	const int32 RootIndex = FindBoneIndex("root");
	if (RootIndex >= 0)
	{
		return RootIndex;
	}
	return GetBones().IsEmpty() ? -1 : 0;
}

void USkeletalMeshComponent::DrawRootDebug(FLineBatcher* LineBatcher) const
{
	const int32 RootIndex = GetRootMotionBoneIndex();
	if (!LineBatcher || RootIndex < 0 || RootIndex >= ComponentSpaceTransforms.Num())
	{
		return;
	}

	// root 본 위치에 구체
	const FVector Root = GetBoneWorldMatrix(RootIndex).TransformPosition(FVector::ZeroVector);
	const FVector4 Color(1.0f, 0.3f, 0.8f, 1.0f);
	LineBatcher->AddSphere(Root, 0.08f, Color);

	// 액터 위치를 root 높이로 내린 점 → root. 선 길이 = root가 액터(캡슐)에서 벗어난 거리
	if (const AActor* Owner = GetOwner())
	{
		const FVector ActorLocation = Owner->GetActorLocation();
		const FVector Base(ActorLocation.X, ActorLocation.Y, Root.Z);
		LineBatcher->AddLine(Base, Root, Color);
		LineBatcher->AddSphere(Base, 0.03f, Color);
	}
}
void DrawSkeletalRootDebug(UWorld* World, FLineBatcher* LineBatcher)
{
	if (!World || !LineBatcher) return;
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		const USkeletalMeshComponent* Mesh = *It;
		if (!Mesh || Mesh->GetWorld() != World || !Mesh->IsRegistered()) continue;
		Mesh->DrawRootDebug(LineBatcher);
	}
}

void DrawPhysicsBodiesDebug(UWorld* World, FLineBatcher* LineBatcher)
{
	if (!World || !LineBatcher)
		return;
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Comp = *It;
		if (!Comp || Comp->GetWorld() != World || !Comp->IsRegistered())
			continue;
		Comp->DrawPhysicsDebug(LineBatcher);
	}
}

USkeletalMeshComponent::USkeletalMeshComponent()
{

}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
	TermArticulated();
	DestroyClothingSimulation();
    if (ManagedAnimGraph)
    {
        if (AnimInstance == ManagedAnimGraph.get())
        {
            SetAnimInstance(nullptr);
        }
        else
        {
            ManagedAnimGraph->UninitializeAnimation();
        }
    }
}

void USkeletalMeshComponent::CreateClothingSimulation()
{
	DestroyClothingSimulation();

	USkeletalMesh* Mesh = GetSkeletalMesh();
	if (!Mesh || !bEnableClothingSimulation)
		return;

	UClothingAssetCommon* Asset = Mesh->GetClothingAsset();
	if (!Asset || !Asset->IsValid())
		return;

	TUniquePtr<FClothingSimulationNv> NewSim = MakeUnique<FClothingSimulationNv>();
	if (!NewSim->CreateClothFromAsset(*Asset))
		return;

	ClothingSimulation = std::move(NewSim);
	bClothTeleportPending = true;
}

void USkeletalMeshComponent::DestroyClothingSimulation()
{
	if (ClothingSimulation)
	{
		ClothingSimulation->Shutdown();
		ClothingSimulation.reset();
	}
	ClothSimData.Positions.Empty();
	ClothSimData.Normals.Empty();
	if (FSkeletalMeshObject* MeshObj = GetMeshObject())
	{
		MeshObj->UpdateClothData(nullptr);
	}
}

void USkeletalMeshComponent::SetClothSimulationSuspended(bool bSuspend)
{
	bSuspendClothingSimulation = bSuspend;
	if (bSuspend)
	{
		if (FSkeletalMeshObject* MeshObj = GetMeshObject())
			MeshObj->UpdateClothData(nullptr);
	}
	else
	{
		bClothTeleportPending = true;
	}
}

void USkeletalMeshComponent::RefreshClothingOnAllComponents(USkeletalMesh* TargetMesh)
{
	if (!TargetMesh)
		return;

	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Comp = *It;
		if (Comp && Comp->GetSkeletalMesh() == TargetMesh)
		{
			Comp->CreateClothingSimulation();
		}
	}
}

UPhysicsAsset* USkeletalMeshComponent::GetPhysicsAsset() const
{
	USkeletalMesh* Mesh = GetSkeletalMesh();
	return Mesh ? Mesh->GetPhysicsAsset() : nullptr;
}

int32 USkeletalMeshComponent::FindBodyIndexByBone(const FString& BoneName) const
{
	UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	if (!PhysAsset)
		return -1;
	const int32 Index = PhysAsset->FindBodyIndex(FName(BoneName));
	return (Index >= 0 && Index < Bodies.Num() && Bodies[Index]) ? Index : -1;
}

FBodyInstance* USkeletalMeshComponent::GetBodyInstanceByBone(const FString& BoneName) const
{
	const int32 Index = FindBodyIndexByBone(BoneName);
	return Index >= 0 ? Bodies[Index] : nullptr;
}

void USkeletalMeshComponent::UpdateClothingSimulation(float DeltaTime)
{
	FSkeletalMeshObject* MeshObj = GetMeshObject();
	if (!MeshObj)
		return;

	if (!bEnableClothingSimulation || bSuspendClothingSimulation)
	{
		if (ClothingSimulation)
			MeshObj->UpdateClothData(nullptr);
		return;
	}

	// 에셋에 ClothingAsset이 나중에 바인딩된 경우 자동 생성
	if (!ClothingSimulation)
	{
		USkeletalMesh* Mesh = GetSkeletalMesh();
		if (Mesh && Mesh->GetClothingAsset() && Mesh->GetClothingAsset()->IsValid())
			CreateClothingSimulation();
		if (!ClothingSimulation)
			return;
	}

	const FTransform ComponentToWorld(GetWorldRotation().Quaternion(), GetWorldLocation(), GetWorldScale3D());
	ClothingSimulation->UpdateSkeletalPose(MeshObj->GetRefToLocal(), ComponentToWorld, bClothTeleportPending);
	bClothTeleportPending = false;

	ClothingSimulation->Simulate(DeltaTime);
	ClothingSimulation->GetSimulationData(ClothSimData);
	MeshObj->UpdateClothData(&ClothSimData);
}

void USkeletalMeshComponent::InstantiatePhysicsAssetBodies_Internal()
{
	SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_RagdollInit);
	TermArticulated();

	UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	USkeletalMesh* Mesh = GetSkeletalMesh();
	FPhysScene* Scene = GetWorld()->GetPhysicsScene();
	if (!PhysAsset || !Mesh || !Scene)
		return;

	// 바디를 지금 포즈 위치에 만들려면 본 행렬이 최신이어야 한다
	UpdateBoneTransformsNow();

	// 바디
	Bodies.SetNum(PhysAsset->BodySetup.Num());
	for (int32 i = 0; i < PhysAsset->BodySetup.Num(); ++i)
	{
		Bodies[i] = nullptr;
		UBodySetup* Setup = PhysAsset->BodySetup[i];
		if (!Setup)
			continue;

		// 에셋과 메시의 본 이름이 안 맞으면(다른 스켈레톤용 에셋) 건너뛴다
		const int32 BoneIndex = Mesh->FindBoneIndex(Setup->BoneName.ToString());
		if (BoneIndex < 0)
		{
			KRS_LOG(Warning, "[Physics] PhysicsAsset bone '{}' not found in mesh", Setup->BoneName.ToString());
			continue;
		}

		FBodyInstance* Body = new FBodyInstance();
		// 바디마다 PhysicsAsset에 저장된 설정(질량·댐핑·충돌·머티리얼)을 쓴다
		Body->CopyBodyInstancePropertiesFrom(Setup->DefaultInstance);
		Body->Mobility = EBodyMobility::Movable;   // 랙돌 바디는 항상 움직일 수 있어야 한다
		Body->bSimulatePhysics = ShouldBodySimulate(Setup, BodyInstance.bSimulatePhysics);
		// 컴포넌트에 물리 머티리얼을 지정했으면 모든 바디에 그것을 쓴다
		if (BodyInstance.PhysMaterialOverride)
			Body->PhysMaterialOverride = BodyInstance.PhysMaterialOverride;
		Body->InstanceBoneIndex = BoneIndex;
		Body->InstanceBodyIndex = i;
		Body->OwnerId = GetUUID();

		Body->ObjectType = ECollisionCategory::PhysicsBody;
		Body->CollidesWith = AllCategoriesMask & ~CategoryBit(ECollisionCategory::Pawn);

		// 형상(캡슐)이 본 로컬 기준이므로 바디는 본의 월드 트랜스폼에 놓는다
		const FTransform BoneWorld = MatrixToTransform(GetBoneWorldMatrix(BoneIndex));
		if (!Body->InitBody(Setup, BoneWorld, this, Scene))
		{
			delete Body;
			continue;
		}

		if (physx::PxRigidDynamic* Dynamic = Body->Actor->is<physx::PxRigidDynamic>())
		{
			SCOPED_SCENE_WRITE_LOCK(Scene->GetPxScene());
			// 조인트 사슬은 반복이 적으면 늘어났다가 튕긴다 (기본 4, 1)
			Dynamic->setSolverIterationCounts(8, 2);
			// 겹친 바디를 밀어낼 때의 최대 속도(m/s). 기본값은 사실상 무한대라 겹치면 폭발한다
			Dynamic->setMaxDepenetrationVelocity(1.0f);
		}

		Bodies[i] = Body;
	}

	// 조인트 (두 바디가 모두 있어야 하므로 바디를 전부 만든 뒤에)
	Constraints.SetNum(PhysAsset->ConstraintSetup.Num());
	for (int32 i = 0; i < PhysAsset->ConstraintSetup.Num(); ++i)
	{
		Constraints[i] = nullptr;
		UPhysicsConstraintTemplate* Template = PhysAsset->ConstraintSetup[i];
		if (!Template)
			continue;

		const FConstraintInstance& Def = Template->DefaultInstance;
		const int32 ChildIdx = PhysAsset->FindBodyIndex(Def.ConstraintBone1);
		const int32 ParentIdx = PhysAsset->FindBodyIndex(Def.ConstraintBone2);
		FBodyInstance* Child = ChildIdx >= 0 ? Bodies[ChildIdx] : nullptr;
		FBodyInstance* Parent = ParentIdx >= 0 ? Bodies[ParentIdx] : nullptr;
		if (!Child || !Parent)
			continue;

		FConstraintInstance* CI = new FConstraintInstance();
		CI->CopyConstraintParamsFrom(Def);
		if (!CI->InitConstraint(Child, Parent))
		{
			delete CI;
			continue;
		}
		Constraints[i] = CI;
	}

	bTeleportBodiesNextUpdate = true;
}

void USkeletalMeshComponent::TermArticulated()
{
	// 조인트 먼저: 액터가 먼저 사라지면 조인트가 해제된 액터를 가리킨다
	for (FConstraintInstance* CI : Constraints)
		delete CI;   // 소멸자가 TermConstraint
	Constraints.Empty();

	for (FBodyInstance* Body : Bodies)
		delete Body; // 소멸자가 TermBody
	Bodies.Empty();
}

void USkeletalMeshComponent::UpdateKinematicBonesToAnim()
{
	SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_RagdollKinematic);
	if (Bodies.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	const bool bTeleport = bTeleportBodiesNextUpdate || !(World && World->IsGameWorld());
	bTeleportBodiesNextUpdate = false;

	for (FBodyInstance* Body : Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance() || Body->IsSimulatingPhysics())
		{
			continue;
		}

		const FTransform BoneWorld = MatrixToTransform(GetBoneWorldMatrix(Body->InstanceBoneIndex));
		Body->SetWorldTransform(BoneWorld, bTeleport);
	}
}

void USkeletalMeshComponent::ApplyPhysicsToBonePose()
{
	SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_RagdollBonePose);
	const TArray<FBoneInfo>& Bones = GetBones();
	if (Bodies.IsEmpty() || Bones.IsEmpty())
		return;
	if (BoneSpaceTransforms.Num() != Bones.Num())
		OnSkeletalMeshChanged();

	// 본 인덱스 → 시뮬레이트 중인 바디
	TArray<FBodyInstance*> BoneBodies;
	BoneBodies.Init(nullptr, Bones.Num());
	for (FBodyInstance* Body : Bodies)
	{
		if (Body && Body->InstanceBoneIndex >= 0 && Body->InstanceBoneIndex < Bones.Num() && Body->IsSimulatingPhysics())
			BoneBodies[Body->InstanceBoneIndex] = Body;
	}

	const FMatrix InvComponentWorld = GetWorldMatrix().Inverse();

	// 1. 컴포넌트 공간 행렬을 다시 쌓는다. 부모가 먼저 오므로 앞에서부터 돌면 된다
	TArray<FMatrix> NewCS;
	NewCS.SetNum(Bones.Num());
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		const int32 Parent = Bones[i].ParentIndex;
		if (FBodyInstance* Body = BoneBodies[i])
		{
			// 바디가 있는 본: 물리 결과(월드) → 컴포넌트 공간. Scale3D가 붙어 있어 스케일도 유지된다
			NewCS[i] = Body->GetWorldTransform().GetLocalMatrix() * InvComponentWorld;
		}
		else
		{
			// 바디가 없는 본: 기존 로컬 포즈 그대로 부모를 따라간다
			const FMatrix Local = BoneSpaceTransforms[i].GetLocalMatrix();
			NewCS[i] = Parent < 0 ? Local : Local * NewCS[Parent];
		}
	}

	// 2. 바디가 있는 본만 부모 기준 로컬로 되돌린다 (없는 본은 로컬이 바뀌지 않았다)
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (!BoneBodies[i])
			continue;
		const int32 Parent = Bones[i].ParentIndex;
		const FMatrix Local = Parent < 0 ? NewCS[i] : NewCS[i] * NewCS[Parent].Inverse();
		BoneSpaceTransforms[i] = MatrixToTransform(Local);
	}

	// 3. 다음 UpdateBoneTransformsNow에서 RefreshBoneTransforms가 같은 행렬을 다시 만들고 렌더로 보낸다
	MarkBoneTransformsDirty();
}

const FString& USkeletalMeshComponent::GetAnimBlueprintPath() const
{
    return AnimBlueprintPath;
}

const FString& USkeletalMeshComponent::GetAnimBlueprintError() const
{
    return AnimBlueprintError;
}

bool USkeletalMeshComponent::SetAnimBlueprint(const FString& Path)
{
    if (bUpdatingAnimation)
    {
        AnimBlueprintError = "Load or reload Animation Blueprints from actor Tick, outside animation callbacks.";
        return false;
    }

    if (Path.empty())
    {
        const bool bHadBlueprint = !AnimBlueprintPath.empty();
        AnimBlueprintPath.clear();
        AnimBlueprintError.clear();
        bBlueprintReloadRequested = false;

        if (ManagedAnimGraph && bHadBlueprint)
        {
            SetAnimInstance(nullptr);
            ResetPose();
            RefreshBoneTransformsNow();
        }

        return true;
    }

    AnimBlueprintError.clear();
    const FString Key = FAssetUtils::GetAssetKey(Path);
    const FString FilePath = FAssetUtils::ResolveAssetFilePath(Key);
    FAnimBlueprintData Data;

    if (!AnimBlueprintFile::IsAnimBlueprintPath(Key) || !AnimBlueprintFile::Read(FPaths::FromUtf8(FilePath.empty() ? Key : FilePath), Data, AnimBlueprintError))
    {
        if (AnimBlueprintError.empty())
        {
            AnimBlueprintError = "Use an .animblueprint asset.";
        }

        return false;
    }

    TUniquePtr<UAnimGraphInstance> Instance;
    AnimBlueprintCompiler::FNodeMap NodeIndices;

    if (!AnimBlueprintCompiler::Build(Data, *this, Instance, NodeIndices, AnimBlueprintError))
    {
        return false;
    }

    SetAnimInstance(Instance.get());
    ManagedAnimGraph = std::move(Instance);
    AnimBlueprintPath = Key;
    bBlueprintReloadRequested = false;
    AnimBlueprintError.clear();
    RefreshAnimationPose();
    return true;
}

bool USkeletalMeshComponent::ReloadAnimBlueprint()
{
    const FString Path = AnimBlueprintPath;
    bBlueprintReloadRequested = false;

    if (Path.empty())
    {
        return false;
    }

    if (!SetAnimBlueprint(Path))
    {
        KRS_LOG(Warning, "AnimBlueprint load failed: {} ({})", Path, AnimBlueprintError);
        return false;
    }

    return true;
}

UAnimGraphInstance* USkeletalMeshComponent::CreateAnimGraph()
{
    if (bUpdatingAnimation)
    {
        AnimBlueprintError = "Create animation graphs outside animation callbacks.";
        return nullptr;
    }

    if (!AnimBlueprintPath.empty())
    {
        AnimBlueprintError = "An Animation Blueprint is assigned. Use GetAnimGraph to update its variables.";
        return nullptr;
    }

    AnimBlueprintError.clear();

    if (UAnimGraphInstance* Existing = Cast<UAnimGraphInstance>(AnimInstance))
    {
        Existing->ResetGraph();
        return Existing;
    }

    TUniquePtr<UAnimGraphInstance> Instance(NewObject<UAnimGraphInstance>(this));
    SetAnimInstance(Instance.get());
    ManagedAnimGraph = std::move(Instance);
    return ManagedAnimGraph.get();
}
