#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditorTab.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetDebugRenderer.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditorMath.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Physics/PhysScene.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsLock.h"
#include "Physics/PhysXConversions.h"
#include "Render/SkeletalMesh.h"
#include "Render/SceneView.h"
#include "Collision/Ray.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/PhysicsAssetUtils.h"
#include "Asset/AssetUtils.h"
#include "Math/EngineMath.h"
#include "Physics/PhysicalMaterial.h"
#include "Render/LineBatcher.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>

namespace
{
	using namespace PhysicsAssetEditorMath;

	// 바디: 반투명 회색, 선택하면 파랑
	const FVector4& BodyColor = PhysicsDebugColors::Body;
	const FVector4& SelectedBodyColor = PhysicsDebugColors::SelectedBody;

	// 조인트: Swing은 빨강, Twist는 초록
	const FVector4 SwingLimitColor(1.0f, 0.25f, 0.25f, 0.35f);
	const FVector4 TwistLimitColor(0.3f, 1.0f, 0.3f, 0.35f);
	const FVector4 SwingDialColor(1.0f, 0.25f, 0.25f, 0.85f);
	const FVector4 TwistDialColor(0.3f, 1.0f, 0.3f, 0.85f);
	const FVector4 AxisColors[3] = { FVector4(1, 0, 0, 1), FVector4(0, 1, 0, 1), FVector4(0, 0.4f, 1, 1) };

	// 잠긴 축은 0, 자유 축은 반 바퀴, 제한 축은 그 각도
	float LimitAngleRadians(EAngularConstraintMotion Motion, float Degrees)
	{
		switch (Motion)
		{
		case EAngularConstraintMotion::Locked: return 0.0f;
		case EAngularConstraintMotion::Free:   return PI;
		default:                               return FMath::DegreesToRadians(FMath::Clamp(Degrees, 0.0f, 179.0f));
		}
	}

	// 광선(Origin + t·Dir)과 형상의 교차. 형상 로컬 공간에서 계산하고, Dir은 정규화하지 않아도 된다
	// (월드 → 로컬이 아핀 변환이라 t가 월드 광선의 t와 같다). 맞지 않으면 음수
	float RaySphere(const FVector& Origin, const FVector& Dir, const FVector& Center, float Radius)
	{
		const FVector O = Origin - Center;
		const float A = Dir.Dot(Dir);
		const float B = 2.0f * O.Dot(Dir);
		const float C = O.Dot(O) - Radius * Radius;
		const float Disc = B * B - 4.0f * A * C;
		if (A <= 0.0f || Disc < 0.0f)
			return -1.0f;
		const float Sqrt = std::sqrt(Disc);
		const float T0 = (-B - Sqrt) / (2.0f * A);
		const float T1 = (-B + Sqrt) / (2.0f * A);
		return T0 >= 0.0f ? T0 : T1;
	}

	float RayCapsule(const FVector& O, const FVector& D, float Radius, float HalfLength)
	{
		float Best = -1.0f;
		auto Consider = [&](float T) { if (T >= 0.0f && (Best < 0.0f || T < Best)) Best = T; };

		// 원기둥 옆면 (z가 원기둥 구간 안일 때만)
		const float A = D.X * D.X + D.Y * D.Y;
		if (A > 1e-12f)
		{
			const float B = 2.0f * (O.X * D.X + O.Y * D.Y);
			const float C = O.X * O.X + O.Y * O.Y - Radius * Radius;
			const float Disc = B * B - 4.0f * A * C;
			if (Disc >= 0.0f)
			{
				const float Sqrt = std::sqrt(Disc);
				for (const float T : { (-B - Sqrt) / (2.0f * A), (-B + Sqrt) / (2.0f * A) })
				{
					const float Z = O.Z + T * D.Z;
					if (Z >= -HalfLength && Z <= HalfLength)
						Consider(T);
				}
			}
		}

		// 양 끝 반구
		Consider(RaySphere(O, D, FVector(0, 0, HalfLength), Radius));
		Consider(RaySphere(O, D, FVector(0, 0, -HalfLength), Radius));
		return Best;
	}

	float RayBox(const FVector& O, const FVector& D, const FVector& Extent)
	{
		float TMin = -FLT_MAX;
		float TMax = FLT_MAX;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (std::abs(D[Axis]) < 1e-12f)
			{
				if (O[Axis] < -Extent[Axis] || O[Axis] > Extent[Axis])
					return -1.0f;
				continue;
			}
			float T1 = (-Extent[Axis] - O[Axis]) / D[Axis];
			float T2 = (Extent[Axis] - O[Axis]) / D[Axis];
			if (T1 > T2)
				std::swap(T1, T2);
			TMin = std::max(TMin, T1);
			TMax = std::min(TMax, T2);
			if (TMin > TMax)
				return -1.0f;
		}
		if (TMax < 0.0f)
			return -1.0f;
		return TMin >= 0.0f ? TMin : TMax;
	}

	// 로컬 Z축을 Dir로 돌리는 회전. 형상은 로컬 Z로 서므로 본 방향으로 눕힐 때 쓴다
	FQuat RotationFromZTo(const FVector& Dir)
	{
		const FVector Z(0, 0, 1);
		const FVector D = Dir.GetSafeNormal(SMALL_NUMBER, Z);
		const float Cos = FMath::Clamp(Z.Dot(D), -1.0f, 1.0f);
		if (Cos > 0.9999f)
			return FQuat::Identity();
		if (Cos < -0.9999f)
			return FQuat::MakeFromAxisAngle(FVector(1, 0, 0), PI);
		return FQuat::MakeFromAxisAngle(Z.Cross(D).GetSafeNormal(), acosf(Cos));
	}

	// 본 공간 점을 다른 본 공간으로 옮긴다 (바인드 포즈 기준)
	FVector BonePositionInBone(const TArray<FBoneInfo>& Bones, int32 FromBone, const FVector& Local, int32 ToBone)
	{
		const FVector Comp = Bones[FromBone].InverseBindPose.Inverse().TransformPosition(Local);
		return Bones[ToBone].InverseBindPose.TransformPosition(Comp);
	}

	FVector BoneDirectionInBone(const TArray<FBoneInfo>& Bones, int32 FromBone, const FVector& Dir, int32 ToBone)
	{
		const FVector Comp = Bones[FromBone].InverseBindPose.Inverse().TransformVector(Dir);
		return Bones[ToBone].InverseBindPose.TransformVector(Comp).GetSafeNormal();
	}

}

FPhysicsAssetEditor::FPhysicsAssetEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
	const FString& InAssetPath, USkeletalMesh& InMesh, UPhysicsAsset& InPhysicsAsset)
	: FSkeletalPreviewEditor(InEditorEngine, InEditorManager, InAssetPath)
	, DebugRenderer(MakeUnique<FPhysicsAssetDebugRenderer>())
	, PhysicsAsset(&InPhysicsAsset)
{
	Mesh = &InMesh;
}

FPhysicsAssetEditor::~FPhysicsAssetEditor() = default;

TUniquePtr<IEditorTab> FPhysicsAssetEditor::CreateTab()
{
	return MakeUnique<FPhysicsAssetEditorTab>(*this);
}

bool FPhysicsAssetEditor::InitializeResources()
{
	if (!PhysicsAsset)
		return false;

	// 프리뷰 컴포넌트가 랙돌 바디를 만들 때 이 에셋을 쓰게 한다
	if (Mesh)
		Mesh->SetPhysicsAsset(PhysicsAsset);

	if (!InitializePreview())
		return false;

	// 기즈모가 붙을 대상. 부모에서 떼어 두어 상대 변환이 곧 월드 변환이 되게 한다
	GizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
	if (!GizmoTarget)
		return false;
	GizmoTarget->DetachFromParent();
	return true;
}

void FPhysicsAssetEditor::ShutdownResources()
{
	EndGrab();
	PhysicsGizmo.EndDrag();
	PhysicsGizmo.SetTarget(nullptr);
	GizmoTarget = nullptr;
	FloorComponent = nullptr;   // 프리뷰 월드와 함께 사라진다
	bSimulating = false;
	FSkeletalPreviewEditor::ShutdownResources();
}

void FPhysicsAssetEditor::ToggleSimulation()
{
	if (bSimulating)
		StopSimulation();
	else
		StartSimulation();
}

void FPhysicsAssetEditor::RunSimulation(bool bSelected)
{
	// 같은 방식으로 실행 중이면 멈춘다. 다른 방식으로 실행 중이면 새 방식으로 처음부터 다시 떨어뜨린다
	if (bSimulating && bSelectedSimulation == bSelected)
	{
		StopSimulation();
		return;
	}
	bSelectedSimulation = bSelected;
	if (bSimulating)
		StopSimulation();
	StartSimulation();
}

void FPhysicsAssetEditor::SetFloorCollisionEnabled(bool bEnabled)
{
	bFloorCollision = bEnabled;
	ApplyFloorCollision();
}

void FPhysicsAssetEditor::StartSimulation()
{
	if (!MeshComponent || !PhysicsAsset || !EnsureSimulationFloor())
		return;

	PhysicsGizmo.EndDrag();
	PhysicsGizmo.SetTarget(nullptr);

	// 바디를 기준 포즈에서 새로 만든 뒤 켠다. 편집 직후라도 저장된 값 그대로 시작한다
	MeshComponent->DestroyPhysicsState();
	MeshComponent->CreatePhysicsState();
	MeshComponent->SetSimulatePhysics(true);
	bSimulating = true;
	ApplyMeshDrawMode();   // 메시 표시는 시뮬레이션용 값으로

	// 선택 시뮬레이트: 고른 바디(조인트면 그 자식 바디)와 그 아래 본의 바디만 움직이고 나머지는 제자리에 고정한다
	const TArray<int32> RootBones = bSelectedSimulation ? GetSelectedSimulationRootBones() : TArray<int32>();
	if (!RootBones.IsEmpty())
	{
		for (FBodyInstance* Body : MeshComponent->Bodies)
		{
			if (!Body)
				continue;
			const bool bUnderSelection = std::any_of(RootBones.begin(), RootBones.end(),
				[&](int32 Root) { return IsBoneDescendantOf(Body->InstanceBoneIndex, Root); });
			if (!bUnderSelection)
				Body->SetSimulatePhysics(false);
		}
	}

	if (FPhysScene* Scene = GetPreviewPhysScene())
		SavedGravity = Scene->GetGravity();
}

void FPhysicsAssetEditor::StopSimulation()
{
	EndGrab();   // 바디를 다시 만들기 전에 조인트부터 푼다
	bSimulating = false;
	if (FPhysScene* Scene = GetPreviewPhysScene())
		Scene->SetGravity(SavedGravity);
	if (!MeshComponent)
		return;

	// 랙돌을 끄고 기준 포즈로 되돌린 뒤 바디를 그 포즈에서 다시 만든다
	MeshComponent->SetSimulatePhysics(false);
	MeshComponent->ResetPose();
	MeshComponent->RefreshBoneTransformsNow();
	MeshComponent->DestroyPhysicsState();
	MeshComponent->CreatePhysicsState();
	ApplyMeshDrawMode();   // 메시 표시는 편집용 값으로
}

TArray<int32> FPhysicsAssetEditor::GetSelectedSimulationRootBones() const
{
	TArray<int32> Roots;
	for (const FPhysicsAssetSelection& Item : SelectedItems)
	{
		int32 Bone = -1;
		if (Item.IsBody())
			Bone = GetBodyBoneIndex(Item.Index);
		else if (Item.IsConstraint() && Mesh && PhysicsAsset && Item.Index < PhysicsAsset->ConstraintSetup.Num())
			Bone = Mesh->FindBoneIndex(PhysicsAsset->ConstraintSetup[Item.Index]->DefaultInstance.ConstraintBone1.ToString());   // 자식 쪽
		if (Bone >= 0)
			Roots.Add(Bone);
	}
	return Roots;
}

FPhysScene* FPhysicsAssetEditor::GetPreviewPhysScene() const
{
	UWorld* World = ViewportClient ? ViewportClient->GetWorld() : nullptr;
	return World ? World->GetPhysicsScene() : nullptr;
}

bool FPhysicsAssetEditor::EnsureSimulationFloor()
{
	if (FloorComponent)
		return true;
	if (!Mesh || Mesh->GetRenderData().LODRenderData.IsEmpty())
		return false;

	FloorComponent = Cast<UBoxComponent>(PreviewScene.AddComponent(UBoxComponent::StaticClass()));
	if (!FloorComponent)
		return false;

	// 윗면이 그리드(Z = 0)에 오게 둔다. 메시가 그리드 아래로 내려가 있으면 메시 바닥에 맞춘다
	const float MeshBottom = Mesh->GetRenderData().LODRenderData[0].BindAABB.Min.Z;
	const float FloorTop = FMath::Min(0.0f, MeshBottom);
	const FVector Extent(50.0f, 50.0f, 0.5f);
	FloorComponent->DetachFromParent();
	FloorComponent->BoxExtent = Extent;
	FloorComponent->BodyInstance.Mobility = EBodyMobility::Static;
	FloorComponent->BodyInstance.ObjectType = ECollisionCategory::WorldStatic;
	FloorComponent->SetTransform(FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, FloorTop - Extent.Z)));

	ApplyFloorCollision();
	return true;
}

void FPhysicsAssetEditor::ApplyFloorCollision()
{
	if (!FloorComponent)
		return;
	// NoCollision이면 바디를 만들지 않는다
	FloorComponent->BodyInstance.CollisionEnabled = bFloorCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
	FloorComponent->DestroyPhysicsState();
	FloorComponent->CreatePhysicsState();
}

void FPhysicsAssetEditor::TickBeforeWorld(float DeltaTime)
{
	// 월드 틱 안에서 메시 컴포넌트가 이번 결과를 본 포즈에 옮긴다
	if (!bSimulating)
		return;
	if (FPhysScene* Scene = GetPreviewPhysScene())
	{
		Scene->SetGravity(bNoGravitySimulation ? FVector(0.0f, 0.0f, 0.0f) : SavedGravity);
		Scene->Tick(DeltaTime);
	}
}

float FPhysicsAssetEditor::GetConstraintDrawLength(bool bSelected) const
{
	// 메시 대각선 기준. 키 1.7m 캐릭터면 4cm, 선택하면 10cm 정도
	const float Base = FMath::Max(BoneSphereSize * 8.0f, 0.001f);
	const float Scaled = Base * FMath::Max(RenderSettings.ConstraintScale, 0.01f);
	return bSelected ? Scaled * 2.5f : Scaled;
}

namespace
{
	// Items에 Item을 Mode대로 반영한다. 주 선택은 배열의 마지막 항목이다
	void ApplySelectMode(TArray<FPhysicsAssetSelection>& Items, const FPhysicsAssetSelection& Item, EPhysicsAssetSelectMode Mode)
	{
		int32 Found = -1;
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (Items[i].IsSameItem(Item))
			{
				Found = i;
				break;
			}
		}

		switch (Mode)
		{
		case EPhysicsAssetSelectMode::Replace:
			Items.Empty();
			Items.Add(Item);
			break;
		case EPhysicsAssetSelectMode::Add:
			if (Found >= 0)
				Items.RemoveAt(Found);
			Items.Add(Item);
			break;
		case EPhysicsAssetSelectMode::Toggle:
			if (Found >= 0)
				Items.RemoveAt(Found);
			else
				Items.Add(Item);
			break;
		}
	}
}

void FPhysicsAssetEditor::SyncBoneToPrimarySelection()
{
	Selection = SelectedItems.IsEmpty() ? FPhysicsAssetSelection() : SelectedItems.Last();

	// 본 트리·본 시각화도 주 선택의 본을 가리키게 한다. 조인트는 자식 본에 붙어 있다
	int32 Bone = -1;
	if (Selection.IsBody())
		Bone = GetBodyBoneIndex(Selection.Index);
	else if (Selection.IsConstraint() && Mesh)
		Bone = Mesh->FindBoneIndex(PhysicsAsset->ConstraintSetup[Selection.Index]->DefaultInstance.ConstraintBone1.ToString());

	bSyncingBoneSelection = true;
	SelectBone(Bone);
	bSyncingBoneSelection = false;
}

void FPhysicsAssetEditor::SelectBody(int32 BodyIndex, int32 PrimitiveIndex, EPhysicsAssetSelectMode Mode)
{
	if (!PhysicsAsset || BodyIndex < 0 || BodyIndex >= PhysicsAsset->BodySetup.Num())
	{
		if (Mode == EPhysicsAssetSelectMode::Replace)
			ClearSelection();
		return;
	}

	FPhysicsAssetSelection Item;
	Item.Type = EPhysicsAssetSelectionType::Body;
	Item.Index = BodyIndex;
	Item.PrimitiveIndex = PrimitiveIndex;
	ApplySelectMode(SelectedItems, Item, Mode);
	SyncBoneToPrimarySelection();
}

void FPhysicsAssetEditor::SelectConstraint(int32 ConstraintIndex, EPhysicsAssetSelectMode Mode)
{
	if (!PhysicsAsset || ConstraintIndex < 0 || ConstraintIndex >= PhysicsAsset->ConstraintSetup.Num())
	{
		if (Mode == EPhysicsAssetSelectMode::Replace)
			ClearSelection();
		return;
	}

	FPhysicsAssetSelection Item;
	Item.Type = EPhysicsAssetSelectionType::Constraint;
	Item.Index = ConstraintIndex;
	ApplySelectMode(SelectedItems, Item, Mode);
	SyncBoneToPrimarySelection();
}

void FPhysicsAssetEditor::SetSelections(const TArray<FPhysicsAssetSelection>& Items)
{
	SelectedItems.Empty();
	for (const FPhysicsAssetSelection& Item : Items)
	{
		const bool bValid = PhysicsAsset &&
			((Item.IsBody() && Item.Index >= 0 && Item.Index < PhysicsAsset->BodySetup.Num()) ||
			 (Item.IsConstraint() && Item.Index >= 0 && Item.Index < PhysicsAsset->ConstraintSetup.Num()));
		if (bValid)
			ApplySelectMode(SelectedItems, Item, EPhysicsAssetSelectMode::Add);
	}
	SyncBoneToPrimarySelection();
}

void FPhysicsAssetEditor::ClearSelection()
{
	SelectedItems.Empty();
	SyncBoneToPrimarySelection();
}

bool FPhysicsAssetEditor::IsBodySelected(int32 BodyIndex) const
{
	return std::any_of(SelectedItems.begin(), SelectedItems.end(),
		[BodyIndex](const FPhysicsAssetSelection& S) { return S.IsBody(BodyIndex); });
}

bool FPhysicsAssetEditor::IsConstraintSelected(int32 ConstraintIndex) const
{
	return std::any_of(SelectedItems.begin(), SelectedItems.end(),
		[ConstraintIndex](const FPhysicsAssetSelection& S) { return S.IsConstraint(ConstraintIndex); });
}

TArray<int32> FPhysicsAssetEditor::GetSelectedBodyIndices() const
{
	TArray<int32> Result;
	for (const FPhysicsAssetSelection& S : SelectedItems)
	{
		if (S.IsBody())
			Result.Add(S.Index);
	}
	return Result;
}

TArray<int32> FPhysicsAssetEditor::GetSelectedConstraintIndices() const
{
	TArray<int32> Result;
	for (const FPhysicsAssetSelection& S : SelectedItems)
	{
		if (S.IsConstraint())
			Result.Add(S.Index);
	}
	return Result;
}

UBodySetup* FPhysicsAssetEditor::GetSelectedBody() const
{
	if (!Selection.IsBody() || !PhysicsAsset || Selection.Index >= PhysicsAsset->BodySetup.Num())
		return nullptr;
	return PhysicsAsset->BodySetup[Selection.Index];
}

UPhysicsConstraintTemplate* FPhysicsAssetEditor::GetSelectedConstraint() const
{
	if (!Selection.IsConstraint() || !PhysicsAsset || Selection.Index >= PhysicsAsset->ConstraintSetup.Num())
		return nullptr;
	return PhysicsAsset->ConstraintSetup[Selection.Index];
}

int32 FPhysicsAssetEditor::GetBodyBoneIndex(int32 BodyIndex) const
{
	if (!Mesh || !PhysicsAsset || BodyIndex < 0 || BodyIndex >= PhysicsAsset->BodySetup.Num() || !PhysicsAsset->BodySetup[BodyIndex])
		return -1;
	return Mesh->FindBoneIndex(PhysicsAsset->BodySetup[BodyIndex]->BoneName.ToString());
}

int32 FPhysicsAssetEditor::FindBodyIndexForBone(int32 BoneIndex) const
{
	if (!Mesh || !PhysicsAsset || BoneIndex < 0 || BoneIndex >= Mesh->GetRefSkeleton().Num())
		return -1;
	return PhysicsAsset->FindBodyIndex(FName(Mesh->GetRefSkeleton()[BoneIndex].Name));
}

void FPhysicsAssetEditor::OnBoneSelected()
{
	if (bSyncingBoneSelection)
		return;

	// 뷰포트·본 트리에서 본을 고르면 그 본의 바디를 고른다. 바디가 없는 본이면 선택을 비운다
	const int32 BodyIndex = FindBodyIndexForBone(SelectedBoneIndex);
	SelectedItems.Empty();
	if (BodyIndex >= 0)
	{
		FPhysicsAssetSelection Item;
		Item.Type = EPhysicsAssetSelectionType::Body;
		Item.Index = BodyIndex;
		SelectedItems.Add(Item);
	}
	Selection = SelectedItems.IsEmpty() ? FPhysicsAssetSelection() : SelectedItems.Last();
}

bool FPhysicsAssetEditor::IsBoneDescendantOf(int32 BoneIndex, int32 AncestorBoneIndex) const
{
	if (!Mesh || AncestorBoneIndex < 0)
		return false;

	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	for (int32 B = BoneIndex; B >= 0 && B < Bones.Num(); B = Bones[B].ParentIndex)
	{
		if (B == AncestorBoneIndex)
			return true;
	}
	return false;
}

void FPhysicsAssetEditor::NotifyAssetEdited()
{
	if (bSimulating)
		return;
	BeginStructureChange();
	EndStructureChange();
}

void FPhysicsAssetEditor::BeginStructureChange()
{
	if (MeshComponent)
		MeshComponent->DestroyPhysicsState();
}

void FPhysicsAssetEditor::EndStructureChange()
{
	bDirty = true;
	if (MeshComponent)
		MeshComponent->CreatePhysicsState();
}

bool FPhysicsAssetEditor::AddPrimitive(int32 BoneIndex, EPhysicsAssetPrimitive Type)
{
	if (bSimulating)
		return false;
	if (!Mesh || !PhysicsAsset)
		return false;

	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	if (BoneIndex < 0 || BoneIndex >= Bones.Num())
		return false;

	BeginStructureChange();

	const FName BoneName(Bones[BoneIndex].Name);
	const bool bNewBody = PhysicsAsset->FindBodyIndex(BoneName) < 0;
	UBodySetup* Setup = PhysicsAsset->AddBodySetup(BoneName);

	// 첫 자식 본 쪽으로 눕히고 본 길이에 맞춘다. 자식이 없으면 메시 크기로 정한 기본 길이를 쓴다
	FVector Dir(0, 0, 1);
	float Len = Bones[BoneIndex].InverseBindPose.TransformVector(FVector(BoneSphereSize * 20.0f, 0, 0)).Size();
	for (int32 Child = BoneIndex + 1; Child < Bones.Num(); ++Child)
	{
		if (Bones[Child].ParentIndex != BoneIndex)
			continue;

		const FVector ChildLocal = BonePositionInBone(Bones, Child, FVector(0, 0, 0), BoneIndex);
		if (ChildLocal.Size() > KINDA_SMALL_NUMBER)
		{
			Dir = ChildLocal.GetSafeNormal();
			Len = ChildLocal.Size();
		}
		break;
	}

	const FQuat Rotation = RotationFromZTo(Dir);
	const FVector Center = Dir * (Len * 0.5f);

	switch (Type)
	{
	case EPhysicsAssetPrimitive::Sphere:
	{
		FKSphereElem Elem;
		Elem.Center = Center;
		Elem.Radius = Len * 0.3f;
		Setup->AggGeom.SphereElems.Add(Elem);
		break;
	}
	case EPhysicsAssetPrimitive::Box:
	{
		FKBoxElem Elem;
		Elem.Center = Center;
		Elem.Rotation = Rotation;
		Elem.Extent = FVector(Len * 0.2f, Len * 0.2f, Len * 0.5f);
		Setup->AggGeom.BoxElems.Add(Elem);
		break;
	}
	case EPhysicsAssetPrimitive::Capsule:
	default:
	{
		FKSphylElem Elem;
		Elem.Center = Center;
		Elem.Rotation = Rotation;
		Elem.Radius = Len * 0.2f;
		Elem.Length = FMath::Max(Len - 2.0f * Elem.Radius, 0.0f);
		Setup->AggGeom.SphylElems.Add(Elem);
		break;
	}
	}

	if (bNewBody)
		CreateConstraintToParentBody(BoneIndex);

	EndStructureChange();
	SelectBody(PhysicsAsset->FindBodyIndex(BoneName));
	return true;
}

void FPhysicsAssetEditor::CreateConstraintToParentBody(int32 BoneIndex)
{
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();

	int32 Parent = Bones[BoneIndex].ParentIndex;
	while (Parent >= 0 && PhysicsAsset->FindBodyIndex(FName(Bones[Parent].Name)) < 0)
		Parent = Bones[Parent].ParentIndex;
	if (Parent < 0)
		return;   // 위에 바디가 없으면 최상위 바디가 된다

	CreateConstraint(BoneIndex, Parent);
}

int32 FPhysicsAssetEditor::CreateConstraint(int32 ChildBoneIndex, int32 ParentBoneIndex)
{
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	const FName ChildName(Bones[ChildBoneIndex].Name);
	const FName ParentName(Bones[ParentBoneIndex].Name);
	const UBodySetup* ChildSetup = PhysicsAsset->FindBodySetup(ChildName);

	// Twist 축(X)은 형상의 긴 축을 따른다. 형상은 로컬 Z로 서므로 Rotation×Z가 긴 축이다
	FQuat ShapeRotation = FQuat::Identity();
	if (ChildSetup && !ChildSetup->AggGeom.SphylElems.IsEmpty())
		ShapeRotation = ChildSetup->AggGeom.SphylElems[0].Rotation;
	else if (ChildSetup && !ChildSetup->AggGeom.BoxElems.IsEmpty())
		ShapeRotation = ChildSetup->AggGeom.BoxElems[0].Rotation;

	// 자식 본 하나에 조인트가 여러 개일 수 있으므로 AddConstraint(자식당 하나)를 쓰지 않고 직접 넣는다
	UPhysicsConstraintTemplate* Template = NewObject<UPhysicsConstraintTemplate>(PhysicsAsset);
	FConstraintInstance& CI = Template->DefaultInstance;
	CI.ConstraintBone1 = ChildName;
	CI.ConstraintBone2 = ParentName;

	// Frame1은 자식 본 원점. Frame2는 같은 점·같은 축을 부모 본 공간에서 본 값이라 바인드 포즈에서 두 프레임이 겹친다
	CI.Pos1 = FVector(0, 0, 0);
	CI.PriAxis1 = ShapeRotation.RotateVector(FVector(0, 0, 1));
	CI.SecAxis1 = ShapeRotation.RotateVector(FVector(1, 0, 0));
	CI.Pos2 = BonePositionInBone(Bones, ChildBoneIndex, CI.Pos1, ParentBoneIndex);
	CI.PriAxis2 = BoneDirectionInBone(Bones, ChildBoneIndex, CI.PriAxis1, ParentBoneIndex);
	CI.SecAxis2 = BoneDirectionInBone(Bones, ChildBoneIndex, CI.SecAxis1, ParentBoneIndex);

	PhysicsAsset->ConstraintSetup.Add(Template);

	// 조인트로 이어진 두 바디는 서로 충돌하지 않게 한다 (자동 생성과 같은 규칙)
	PhysicsAsset->DisableCollision(ChildName, ParentName);
	return PhysicsAsset->ConstraintSetup.Num() - 1;
}

int32 FPhysicsAssetEditor::FindConstraintBetween(int32 BodyIndexA, int32 BodyIndexB) const
{
	if (!PhysicsAsset || BodyIndexA < 0 || BodyIndexB < 0 ||
		BodyIndexA >= PhysicsAsset->BodySetup.Num() || BodyIndexB >= PhysicsAsset->BodySetup.Num())
		return -1;

	const FName A = PhysicsAsset->BodySetup[BodyIndexA]->BoneName;
	const FName B = PhysicsAsset->BodySetup[BodyIndexB]->BoneName;
	for (int32 i = 0; i < PhysicsAsset->ConstraintSetup.Num(); ++i)
	{
		const FConstraintInstance& CI = PhysicsAsset->ConstraintSetup[i]->DefaultInstance;
		if ((CI.ConstraintBone1 == A && CI.ConstraintBone2 == B) || (CI.ConstraintBone1 == B && CI.ConstraintBone2 == A))
			return i;
	}
	return -1;
}

bool FPhysicsAssetEditor::MakeNewConstraint(int32 ParentBodyIndex, int32 ChildBodyIndex)
{
	if (bSimulating)
		return false;
	if (ParentBodyIndex == ChildBodyIndex || FindConstraintBetween(ParentBodyIndex, ChildBodyIndex) >= 0)
		return false;

	const int32 ParentBone = GetBodyBoneIndex(ParentBodyIndex);
	const int32 ChildBone = GetBodyBoneIndex(ChildBodyIndex);
	if (ParentBone < 0 || ChildBone < 0)
		return false;

	BeginStructureChange();
	const int32 NewIndex = CreateConstraint(ChildBone, ParentBone);
	EndStructureChange();
	SelectConstraint(NewIndex);
	return true;
}

void FPhysicsAssetEditor::DeleteBody(int32 BodyIndex)
{
	if (bSimulating)
		return;
	if (!PhysicsAsset || BodyIndex < 0 || BodyIndex >= PhysicsAsset->BodySetup.Num() || !PhysicsAsset->BodySetup[BodyIndex])
		return;

	BeginStructureChange();
	PhysicsAsset->RemoveBodySetup(PhysicsAsset->BodySetup[BodyIndex]->BoneName);   // 이 바디의 조인트·충돌 쌍도 같이 지운다
	EndStructureChange();
	ClearSelection();
}

void FPhysicsAssetEditor::DeleteBodiesBelow(int32 BodyIndex)
{
	if (bSimulating)
		return;
	const int32 RootBone = GetBodyBoneIndex(BodyIndex);
	if (RootBone < 0)
		return;

	// 지우는 중 인덱스가 밀리므로 이름을 먼저 모은다
	TArray<FName> Names;
	for (int32 i = 0; i < PhysicsAsset->BodySetup.Num(); ++i)
	{
		if (IsBoneDescendantOf(GetBodyBoneIndex(i), RootBone))
			Names.Add(PhysicsAsset->BodySetup[i]->BoneName);
	}

	BeginStructureChange();
	for (const FName& Name : Names)
		PhysicsAsset->RemoveBodySetup(Name);
	EndStructureChange();
	ClearSelection();
}

void FPhysicsAssetEditor::DeleteConstraint(int32 ConstraintIndex)
{
	if (bSimulating)
		return;
	if (!PhysicsAsset || ConstraintIndex < 0 || ConstraintIndex >= PhysicsAsset->ConstraintSetup.Num())
		return;

	BeginStructureChange();
	PhysicsAsset->ConstraintSetup.RemoveAt(ConstraintIndex);
	EndStructureChange();
	ClearSelection();
}

void FPhysicsAssetEditor::DeleteSelection()
{
	if (bSimulating || !PhysicsAsset || SelectedItems.IsEmpty())
		return;

	// 지우는 중 인덱스가 밀리므로 조인트는 객체로, 바디는 본 이름으로 먼저 모은다
	TArray<UPhysicsConstraintTemplate*> Constraints;
	TArray<FName> BodyNames;
	for (const FPhysicsAssetSelection& Item : SelectedItems)
	{
		if (Item.IsConstraint() && Item.Index < PhysicsAsset->ConstraintSetup.Num())
			Constraints.Add(PhysicsAsset->ConstraintSetup[Item.Index]);
		else if (Item.IsBody() && Item.Index < PhysicsAsset->BodySetup.Num() && PhysicsAsset->BodySetup[Item.Index])
			BodyNames.Add(PhysicsAsset->BodySetup[Item.Index]->BoneName);
	}

	BeginStructureChange();
	for (UPhysicsConstraintTemplate* Constraint : Constraints)
	{
		for (int32 i = 0; i < PhysicsAsset->ConstraintSetup.Num(); ++i)
		{
			if (PhysicsAsset->ConstraintSetup[i] == Constraint)
			{
				PhysicsAsset->ConstraintSetup.RemoveAt(i);
				break;
			}
		}
	}
	for (const FName& Name : BodyNames)
		PhysicsAsset->RemoveBodySetup(Name);   // 이 바디의 조인트·충돌 쌍도 같이 지운다
	EndStructureChange();
	ClearSelection();
}

void FPhysicsAssetEditor::ApplyConstraintPreset(EConstraintPreset Preset)
{
	if (bSimulating || !PhysicsAsset)
		return;

	struct FPresetValues
	{
		EAngularConstraintMotion Swing1, Swing2, Twist;
		float Swing1Limit, Swing2Limit, TwistLimit;
	};
	// 위치는 항상 잠겨 있다. Hinge는 Twist 축으로만 돈다
	FPresetValues Values{};
	switch (Preset)
	{
	case EConstraintPreset::BallAndSocket:
		Values = { EAngularConstraintMotion::Free, EAngularConstraintMotion::Free, EAngularConstraintMotion::Free, 0.0f, 0.0f, 0.0f };
		break;
	case EConstraintPreset::Hinge:
		Values = { EAngularConstraintMotion::Locked, EAngularConstraintMotion::Locked, EAngularConstraintMotion::Free, 0.0f, 0.0f, 45.0f };
		break;
	case EConstraintPreset::Skeletal:
		Values = { EAngularConstraintMotion::Limited, EAngularConstraintMotion::Limited, EAngularConstraintMotion::Limited, 45.0f, 45.0f, 15.0f };
		break;
	}

	bool bChanged = false;
	for (int32 Index : GetSelectedConstraintIndices())
	{
		FConstraintInstance& CI = PhysicsAsset->ConstraintSetup[Index]->DefaultInstance;
		CI.Swing1Motion = Values.Swing1;
		CI.Swing2Motion = Values.Swing2;
		CI.TwistMotion = Values.Twist;
		CI.Swing1LimitDegrees = Values.Swing1Limit;
		CI.Swing2LimitDegrees = Values.Swing2Limit;
		CI.TwistLimitDegrees = Values.TwistLimit;
		bChanged = true;
	}
	if (bChanged)
		NotifyAssetEdited();
}

void FPhysicsAssetEditor::SetCollisionBetweenSelected(bool bEnable)
{
	if (bSimulating || !PhysicsAsset)
		return;

	const TArray<int32> Bodies = GetSelectedBodyIndices();
	if (Bodies.Num() < 2)
		return;
	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		for (int32 j = i + 1; j < Bodies.Num(); ++j)
		{
			const FName A = PhysicsAsset->BodySetup[Bodies[i]]->BoneName;
			const FName B = PhysicsAsset->BodySetup[Bodies[j]]->BoneName;
			if (bEnable)
				PhysicsAsset->EnableCollision(A, B);
			else
				PhysicsAsset->DisableCollision(A, B);
		}
	}
	NotifyAssetEdited();
}

void FPhysicsAssetEditor::SetCollisionBetweenSelectedAndAll(bool bEnable)
{
	if (bSimulating || !PhysicsAsset)
		return;

	const TArray<int32> Bodies = GetSelectedBodyIndices();
	if (Bodies.IsEmpty())
		return;
	for (int32 Selected : Bodies)
	{
		const FName A = PhysicsAsset->BodySetup[Selected]->BoneName;
		for (int32 Other = 0; Other < PhysicsAsset->BodySetup.Num(); ++Other)
		{
			if (Other == Selected || !PhysicsAsset->BodySetup[Other])
				continue;
			const FName B = PhysicsAsset->BodySetup[Other]->BoneName;
			if (bEnable)
				PhysicsAsset->EnableCollision(A, B);
			else
				PhysicsAsset->DisableCollision(A, B);
		}
	}
	NotifyAssetEdited();
}

bool FPhysicsAssetEditor::RegenerateAllBodies()
{
	if (bSimulating || !PhysicsAsset || !Mesh)
		return false;

	BeginStructureChange();
	FPhysAssetCreateParams Params;
	FString Error;
	const bool bOk = FPhysicsAssetUtils::CreateFromSkeletalMesh(*PhysicsAsset, *Mesh, Params, Error);
	EndStructureChange();
	ClearSelection();
	if (!bOk)
		KRS_LOG(Warning, "[Physics] Regenerate failed: {}", Error);
	return bOk;
}

bool FPhysicsAssetEditor::RegenerateSelectedBodies()
{
	if (bSimulating || !PhysicsAsset || !Mesh)
		return false;

	const TArray<int32> Bodies = GetSelectedBodyIndices();
	if (Bodies.IsEmpty())
		return false;

	// 임시 에셋에 통째로 만든 뒤 고른 바디의 형상만 가져온다. 질량·조인트·충돌 설정은 그대로 둔다
	UPhysicsAsset* Generated = NewObject<UPhysicsAsset>();
	FPhysAssetCreateParams Params;
	FString Error;
	bool bChanged = false;
	if (FPhysicsAssetUtils::CreateFromSkeletalMesh(*Generated, *Mesh, Params, Error))
	{
		BeginStructureChange();
		for (int32 Index : Bodies)
		{
			UBodySetup* Target = PhysicsAsset->BodySetup[Index];
			if (const UBodySetup* Source = Target ? Generated->FindBodySetup(Target->BoneName) : nullptr)
			{
				Target->AggGeom = Source->AggGeom;
				bChanged = true;
			}
			else if (Target)
			{
				KRS_LOG(Warning, "[Physics] No generated shape for {} (too few vertices); kept the current shape", Target->BoneName.ToString());
			}
		}
		EndStructureChange();
	}
	else
	{
		KRS_LOG(Warning, "[Physics] Regenerate failed: {}", Error);
	}
	delete Generated;

	// 형상 수가 바뀌었을 수 있으므로 형상 선택은 풀고 바디 선택만 남긴다
	TArray<FPhysicsAssetSelection> Items = SelectedItems;
	for (FPhysicsAssetSelection& Item : Items)
		Item.PrimitiveIndex = -1;
	SetSelections(Items);
	return bChanged;
}

bool FPhysicsAssetEditor::Save()
{
	if (!PhysicsAsset)
		return false;

	const FString FilePath = FAssetUtils::ResolveAssetFilePath(GetAssetPath());
	FString Error;
	if (!PhysicsAssetFile::Write(std::filesystem::path(FilePath.empty() ? GetAssetPath() : FilePath), *PhysicsAsset, Error))
	{
		KRS_LOG(Error, "Failed to save physics asset '{}': {}", GetAssetPath(), Error);
		return false;
	}

	bDirty = false;
	KRS_LOG(Info, "Saved physics asset: {}", GetAssetPath());
	return true;
}

// ---------- 형상·프레임 조회 ----------

FKShapeElem* FPhysicsAssetEditor::GetGizmoShape(UBodySetup& Body, EPhysicsAssetPrimitive* OutType) const
{
	FKAggregateGeom& Geom = Body.AggGeom;
	int32 Index = Selection.IsBody() && Selection.PrimitiveIndex >= 0 ? Selection.PrimitiveIndex : 0;

	if (Index < Geom.SphylElems.Num())
	{
		if (OutType) *OutType = EPhysicsAssetPrimitive::Capsule;
		return &Geom.SphylElems[Index];
	}
	Index -= Geom.SphylElems.Num();
	if (Index < Geom.BoxElems.Num())
	{
		if (OutType) *OutType = EPhysicsAssetPrimitive::Box;
		return &Geom.BoxElems[Index];
	}
	Index -= Geom.BoxElems.Num();
	if (Index < Geom.SphereElems.Num())
	{
		if (OutType) *OutType = EPhysicsAssetPrimitive::Sphere;
		return &Geom.SphereElems[Index];
	}
	return nullptr;
}

void FPhysicsAssetEditor::GetConstraintFrameWorld(const FConstraintInstance& CI, bool bParent, FVector& OutLocation, FQuat& OutRotation) const
{
	OutLocation = FVector(0, 0, 0);
	OutRotation = FQuat::Identity();
	if (!Mesh || !MeshComponent)
		return;

	const int32 Bone = Mesh->FindBoneIndex((bParent ? CI.ConstraintBone2 : CI.ConstraintBone1).ToString());
	if (Bone < 0)
		return;

	// 프레임은 본 공간 값이다. 월드 회전 = 본 월드 회전 × 프레임 회전
	const FMatrix BoneToWorld = MeshComponent->GetBoneWorldMatrix(Bone);
	OutLocation = BoneToWorld.TransformPosition(bParent ? CI.Pos2 : CI.Pos1);
	OutRotation = (QuatFromMatrix(BoneToWorld) *
		QuatFromAxes(bParent ? CI.PriAxis2 : CI.PriAxis1, bParent ? CI.SecAxis2 : CI.SecAxis1)).Normalized();
}

// ---------- 그리기 ----------

namespace
{
	// 형상 하나의 부피와 본 공간 중심
	float ElemVolume(const FKSphereElem& S) { return 4.0f / 3.0f * PI * S.Radius * S.Radius * S.Radius; }
	float ElemVolume(const FKBoxElem& B) { return 8.0f * B.Extent.X * B.Extent.Y * B.Extent.Z; }
	float ElemVolume(const FKSphylElem& S) { return PI * S.Radius * S.Radius * S.Length + 4.0f / 3.0f * PI * S.Radius * S.Radius * S.Radius; }

	float GeomVolume(const FKAggregateGeom& Geom)
	{
		float Volume = 0.0f;
		for (const FKSphereElem& S : Geom.SphereElems) Volume += ElemVolume(S);
		for (const FKBoxElem& B : Geom.BoxElems) Volume += ElemVolume(B);
		for (const FKSphylElem& S : Geom.SphylElems) Volume += ElemVolume(S);
		return Volume;
	}

	// 형상 로컬 원 (Axis1, Axis2가 이루는 평면)
	void AddWireCircle(FLineBatcher& Lines, const FMatrix& ElemToWorld, const FVector& Center, const FVector& Axis1, const FVector& Axis2,
		float Radius, float StartRad, float EndRad, const FVector4& Color, int32 Segments)
	{
		FVector Prev;
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float A = StartRad + (EndRad - StartRad) * static_cast<float>(i) / static_cast<float>(Segments);
			const FVector P = ElemToWorld.TransformPosition(Center + Axis1 * (std::cos(A) * Radius) + Axis2 * (std::sin(A) * Radius));
			if (i > 0)
				Lines.AddLine(Prev, P, Color);
			Prev = P;
		}
	}

	void AddWireCapsule(FLineBatcher& Lines, const FMatrix& ElemToWorld, float Radius, float Length, const FVector4& Color)
	{
		const FVector X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
		const FVector Top = Z * (Length * 0.5f);
		const FVector Bottom = -Top;
		AddWireCircle(Lines, ElemToWorld, Top, X, Y, Radius, 0.0f, 2.0f * PI, Color, 24);
		AddWireCircle(Lines, ElemToWorld, Bottom, X, Y, Radius, 0.0f, 2.0f * PI, Color, 24);
		// 양끝 반구 (XZ, YZ 평면의 반원)
		AddWireCircle(Lines, ElemToWorld, Top, X, Z, Radius, 0.0f, PI, Color, 12);
		AddWireCircle(Lines, ElemToWorld, Top, Y, Z, Radius, 0.0f, PI, Color, 12);
		AddWireCircle(Lines, ElemToWorld, Bottom, X, Z, Radius, PI, 2.0f * PI, Color, 12);
		AddWireCircle(Lines, ElemToWorld, Bottom, Y, Z, Radius, PI, 2.0f * PI, Color, 12);
		const FVector Sides[] = { X, -X, Y, -Y };
		for (const FVector& Side : Sides)
			Lines.AddLine(ElemToWorld.TransformPosition(Top + Side * Radius), ElemToWorld.TransformPosition(Bottom + Side * Radius), Color);
	}

	void AddWireSphere(FLineBatcher& Lines, const FMatrix& ElemToWorld, float Radius, const FVector4& Color)
	{
		const FVector X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1), O(0, 0, 0);
		AddWireCircle(Lines, ElemToWorld, O, X, Y, Radius, 0.0f, 2.0f * PI, Color, 24);
		AddWireCircle(Lines, ElemToWorld, O, X, Z, Radius, 0.0f, 2.0f * PI, Color, 24);
		AddWireCircle(Lines, ElemToWorld, O, Y, Z, Radius, 0.0f, 2.0f * PI, Color, 24);
	}

	const FVector4 WireColor(0.85f, 0.86f, 0.88f, 1.0f);
	const FVector4 SelectedWireColor(0.3f, 0.65f, 1.0f, 1.0f);
	const FVector4 CoMColor(1.0f, 0.85f, 0.2f, 1.0f);
}

float FPhysicsAssetEditor::GetBodyMass(int32 BodyIndex) const
{
	if (!PhysicsAsset || BodyIndex < 0 || BodyIndex >= PhysicsAsset->BodySetup.Num() || !PhysicsAsset->BodySetup[BodyIndex])
		return 0.0f;
	const UBodySetup* Setup = PhysicsAsset->BodySetup[BodyIndex];
	if (Setup->DefaultInstance.MassInKgOverride > 0.0f)
		return Setup->DefaultInstance.MassInKgOverride;
	const UPhysicalMaterial* Material = Setup->DefaultInstance.GetPhysicalMaterial();
	return GeomVolume(Setup->AggGeom) * (Material ? Material->Density : 1000.0f);
}

bool FPhysicsAssetEditor::ShouldDrawBody(int32 BodyIndex) const
{
	const UBodySetup* Setup = PhysicsAsset ? PhysicsAsset->BodySetup[BodyIndex] : nullptr;
	if (!Setup)
		return false;
	const bool bKinematic = Setup->PhysicsType == EPhysicsType::Kinematic;
	if (bKinematic && RenderSettings.bHideKinematicBodies)
		return false;
	if (!bKinematic && RenderSettings.bHideSimulatedBodies)
		return false;
	return true;
}

bool FPhysicsAssetEditor::ShouldDrawConstraint(int32 ConstraintIndex) const
{
	if (!PhysicsAsset || !PhysicsAsset->ConstraintSetup[ConstraintIndex])
		return false;
	return !RenderSettings.bSelectedConstraintsOnly || IsConstraintSelected(ConstraintIndex);
}

bool FPhysicsAssetEditor::ShouldDrawCoM(int32 BodyIndex) const
{
	const EPhysicsCoMDrawMode Mode = GetCoMDrawMode();
	if (Mode == EPhysicsCoMDrawMode::None || !PhysicsAsset || !PhysicsAsset->BodySetup[BodyIndex])
		return false;
	if (Mode == EPhysicsCoMDrawMode::Selected && !IsBodySelected(BodyIndex))
		return false;
	return !(RenderSettings.bHideKinematicCoM && PhysicsAsset->BodySetup[BodyIndex]->PhysicsType == EPhysicsType::Kinematic);
}

bool FPhysicsAssetEditor::GetBodyCenterOfMass(int32 BodyIndex, FVector& OutWorld) const
{
	const int32 Bone = GetBodyBoneIndex(BodyIndex);
	if (Bone < 0 || !MeshComponent)
		return false;

	const FKAggregateGeom& Geom = PhysicsAsset->BodySetup[BodyIndex]->AggGeom;
	FVector Sum(0, 0, 0);
	float Total = 0.0f;
	for (const FKSphereElem& S : Geom.SphereElems) { Sum += S.Center * ElemVolume(S); Total += ElemVolume(S); }
	for (const FKBoxElem& B : Geom.BoxElems) { Sum += B.Center * ElemVolume(B); Total += ElemVolume(B); }
	for (const FKSphylElem& S : Geom.SphylElems) { Sum += S.Center * ElemVolume(S); Total += ElemVolume(S); }
	if (Total <= 0.0f)
		return false;

	OutWorld = MeshComponent->GetBoneWorldMatrix(Bone).TransformPosition(Sum * (1.0f / Total));
	return true;
}

bool FPhysicsAssetEditor::ProjectToViewportUV(const FVector& World, float& OutU, float& OutV) const
{
	const FVector4 Clip = LastViewProjection.TransformFVector4(FVector4(World.X, World.Y, World.Z, 1.0f));
	if (Clip.W <= 1e-4f)
		return false;   // 카메라 뒤
	OutU = (Clip.X / Clip.W) * 0.5f + 0.5f;
	OutV = 0.5f - (Clip.Y / Clip.W) * 0.5f;
	return OutU >= 0.0f && OutU <= 1.0f && OutV >= 0.0f && OutV <= 1.0f;
}

void FPhysicsAssetEditor::ApplyMeshDrawMode()
{
	if (!MeshComponent)
		return;

	// 와이어프레임은 뷰 모드로 그린다. 바디·조인트는 오버레이라 뷰 모드의 영향을 받지 않는다
	const EPhysicsMeshDrawMode Mode = GetMeshDrawMode();
	SetMeshVisible(Mode != EPhysicsMeshDrawMode::None);
	if (!ViewportClient)
		return;
	if (Mode == EPhysicsMeshDrawMode::Wireframe && !bMeshWireframeApplied)
	{
		SavedViewportMode = ViewportClient->GetViewportMode();
		ViewportClient->SetViewportMode(EViewportMode::Wireframe);
		bMeshWireframeApplied = true;
	}
	else if (Mode != EPhysicsMeshDrawMode::Wireframe && bMeshWireframeApplied)
	{
		ViewportClient->SetViewportMode(SavedViewportMode);
		bMeshWireframeApplied = false;
	}
}

void FPhysicsAssetEditor::OnRenderSceneOverlay(const FSceneView& View)
{
	LastViewProjection = View.ViewProjectionMatrix;

	const EPhysicsBodyDrawMode Mode = GetBodyDrawMode();
	const bool bSolidMode = Mode == EPhysicsBodyDrawMode::Solid || Mode == EPhysicsBodyDrawMode::SolidWireframe;
	if (!bSolidMode || !PhysicsAsset || !MeshComponent || !DebugRenderer)
		return;

	DebugRenderer->Begin(View.ViewProjectionMatrix, true);

	const float Opacity = FMath::Clamp(RenderSettings.CollisionOpacity, 0.0f, 1.0f);
	const FVector4 Color(BodyColor.X, BodyColor.Y, BodyColor.Z, Opacity);
	const FVector4 SelectedColor(SelectedBodyColor.X, SelectedBodyColor.Y, SelectedBodyColor.Z, FMath::Min(Opacity + 0.1f, 1.0f));

	// 선택한 바디를 마지막에 그려 반투명 섞임에서 앞에 보이게 한다
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset->BodySetup.Num(); ++BodyIndex)
		{
			const bool bSelected = IsBodySelected(BodyIndex);
			if (bSelected != (Pass == 1) || !ShouldDrawBody(BodyIndex))
				continue;
			if (RenderSettings.bSelectedSolidOnly && !bSelected)
				continue;   // 고르지 않은 바디는 OnRenderLine이 와이어프레임으로 그린다

			const UBodySetup* Setup = PhysicsAsset->BodySetup[BodyIndex];
			const int32 Bone = GetBodyBoneIndex(BodyIndex);
			if (Bone < 0)
				continue;

			const FMatrix BoneToWorld = MeshComponent->GetBoneWorldMatrix(Bone);
			const FVector4& DrawColor = bSelected ? SelectedColor : Color;
			for (const FKSphylElem& S : Setup->AggGeom.SphylElems)
				DebugRenderer->DrawCapsule(MakeMatrix(S.Center, S.Rotation) * BoneToWorld, S.Radius, S.Length, DrawColor);
			for (const FKBoxElem& B : Setup->AggGeom.BoxElems)
				DebugRenderer->DrawBox(MakeMatrix(B.Center, B.Rotation) * BoneToWorld, B.Extent, DrawColor);
			for (const FKSphereElem& S : Setup->AggGeom.SphereElems)
				DebugRenderer->DrawSphere(MakeMatrix(S.Center, S.Rotation) * BoneToWorld, S.Radius, DrawColor);
		}
	}
}

void FPhysicsAssetEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
	FSkeletalPreviewEditor::OnRenderLine(LineBatcher);   // 본

	const EPhysicsBodyDrawMode Mode = GetBodyDrawMode();
	if (Mode == EPhysicsBodyDrawMode::None || !PhysicsAsset || !MeshComponent)
		return;
	const bool bWireMode = Mode == EPhysicsBodyDrawMode::Wireframe || Mode == EPhysicsBodyDrawMode::SolidWireframe;

	for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset->BodySetup.Num(); ++BodyIndex)
	{
		if (!ShouldDrawBody(BodyIndex))
			continue;
		const bool bSelected = IsBodySelected(BodyIndex);
		// 와이어 모드이거나, "고른 것만 솔리드"에서 솔리드로 그리지 않은 바디
		const bool bSolidHere = (Mode == EPhysicsBodyDrawMode::Solid || Mode == EPhysicsBodyDrawMode::SolidWireframe) &&
			(!RenderSettings.bSelectedSolidOnly || bSelected);
		if (!bWireMode && bSolidHere)
			continue;

		const UBodySetup* Setup = PhysicsAsset->BodySetup[BodyIndex];
		const int32 Bone = GetBodyBoneIndex(BodyIndex);
		if (Bone < 0)
			continue;

		const FMatrix BoneToWorld = MeshComponent->GetBoneWorldMatrix(Bone);
		const FVector4& Color = bSelected ? SelectedWireColor : WireColor;
		for (const FKSphylElem& S : Setup->AggGeom.SphylElems)
			AddWireCapsule(LineBatcher, MakeMatrix(S.Center, S.Rotation) * BoneToWorld, S.Radius, S.Length, Color);
		for (const FKBoxElem& B : Setup->AggGeom.BoxElems)
			LineBatcher.AddOrientedBox(FBox(-B.Extent, B.Extent), MakeMatrix(B.Center, B.Rotation) * BoneToWorld, Color);
		for (const FKSphereElem& S : Setup->AggGeom.SphereElems)
			AddWireSphere(LineBatcher, MakeMatrix(S.Center, S.Rotation) * BoneToWorld, S.Radius, Color);
	}
}

void FPhysicsAssetEditor::OnRenderForegroundOverlay(const FSceneView& View)
{
	if (!PhysicsAsset || !MeshComponent || !DebugRenderer)
		return;

	DebugRenderer->Begin(View.ViewProjectionMatrix, false);

	// 질량 중심: 작은 구 + 세 축
	if (GetCoMDrawMode() != EPhysicsCoMDrawMode::None)
	{
		const float Size = FMath::Max(BoneSphereSize * 3.0f, 0.001f) * RenderSettings.CoMMarkerScale;
		for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset->BodySetup.Num(); ++BodyIndex)
		{
			FVector CoM;
			if (!ShouldDrawCoM(BodyIndex) || !GetBodyCenterOfMass(BodyIndex, CoM))
				continue;
			DebugRenderer->DrawSphere(MakeMatrix(CoM, FQuat::Identity()), Size * 0.35f, CoMColor);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				FVector Unit(0, 0, 0);
				Unit[Axis] = Size;
				DebugRenderer->DrawStick(CoM - Unit, CoM + Unit, Size * 0.06f, CoMColor);
			}
		}
	}

	const EPhysicsConstraintDrawMode Mode = GetConstraintDrawMode();
	if (Mode == EPhysicsConstraintDrawMode::None)
		return;

	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 Index = 0; Index < PhysicsAsset->ConstraintSetup.Num(); ++Index)
		{
			const bool bSelected = IsConstraintSelected(Index);
			if (bSelected != (Pass == 1) || !ShouldDrawConstraint(Index))
				continue;

			const FConstraintInstance& CI = PhysicsAsset->ConstraintSetup[Index]->DefaultInstance;
			FVector ChildPos, ParentPos;
			FQuat ChildRot, ParentRot;
			GetConstraintFrameWorld(CI, false, ChildPos, ChildRot);
			GetConstraintFrameWorld(CI, true, ParentPos, ParentRot);

			const float Length = GetConstraintDrawLength(bSelected);

			// 점으로 그리기: 조인트 자리에 작은 구 하나
			if (RenderSettings.bConstraintsAsPoints)
			{
				DebugRenderer->DrawSphere(MakeMatrix(ChildPos, FQuat::Identity()), Length * 0.12f,
					bSelected ? SwingDialColor : FVector4(1.0f, 0.55f, 0.15f, 0.9f));
				continue;
			}

			const FVector ParentX = ParentRot.RotateVector(FVector(1, 0, 0));
			const FVector ParentY = ParentRot.RotateVector(FVector(0, 1, 0));
			const FVector ParentZ = ParentRot.RotateVector(FVector(0, 0, 1));
			const FVector ChildX = ChildRot.RotateVector(FVector(1, 0, 0));
			const FVector ChildY = ChildRot.RotateVector(FVector(0, 1, 0));
			const float LimitSize = Length * 0.9f;
			const FMatrix LimitFrame = FPhysicsAssetDebugRenderer::MakeBasis(ParentX * LimitSize, ParentY * LimitSize, ParentZ * LimitSize, ParentPos);

			if (Mode == EPhysicsConstraintDrawMode::AllLimits)
			{
				// Swing 제한: 부모 프레임 X축 둘레의 원뿔. Swing1(Y축 회전)은 Z쪽, Swing2(Z축 회전)는 Y쪽으로 벌어진다.
				// 화살표는 지금 자식 프레임 X축이 가리키는 곳
				const bool bSwingLocked = CI.Swing1Motion == EAngularConstraintMotion::Locked && CI.Swing2Motion == EAngularConstraintMotion::Locked;
				if (!bSwingLocked)
				{
					DebugRenderer->DrawCone(LimitFrame,
						LimitAngleRadians(CI.Swing2Motion, CI.Swing2LimitDegrees),
						LimitAngleRadians(CI.Swing1Motion, CI.Swing1LimitDegrees), SwingLimitColor);
					DebugRenderer->DrawArrow(ParentPos, ChildX, Length, SwingDialColor);
				}

				// Twist 제한: 부모 X축을 도는 ±각도 부채꼴 (부모 YZ 평면). 화살표는 자식 Y축을 그 평면에 내린 방향
				if (CI.TwistMotion != EAngularConstraintMotion::Locked)
				{
					DebugRenderer->DrawFan(LimitFrame, LimitAngleRadians(CI.TwistMotion, CI.TwistLimitDegrees), TwistLimitColor);
					const FVector TwistIndicator = (ChildY - ParentX * ParentX.Dot(ChildY)).GetSafeNormal();
					DebugRenderer->DrawArrow(ParentPos, TwistIndicator, Length, TwistDialColor);
				}
			}

			// 자식·부모 프레임 축 (X 빨강, Y 초록, Z 파랑). 위치만 그리기에서도 남긴다
			const float AxisLength = Length * 0.25f;
			const float AxisRadius = Length * 0.025f;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				FVector Unit(0, 0, 0);
				Unit[Axis] = 1.0f;
				DebugRenderer->DrawStick(ChildPos, ChildPos + ChildRot.RotateVector(Unit) * AxisLength, AxisRadius, AxisColors[Axis]);
				DebugRenderer->DrawStick(ParentPos, ParentPos + ParentRot.RotateVector(Unit) * AxisLength, AxisRadius, AxisColors[Axis]);
			}
		}
	}
}

void FPhysicsAssetEditor::DrawViewportLabels(ImDrawList* DrawList, const ImVec2& Origin, const ImVec2& Size) const
{
	if (!DrawList || !RenderSettings.bShowMassValue || GetCoMDrawMode() == EPhysicsCoMDrawMode::None || !PhysicsAsset)
		return;

	// 질량 중심 옆에 kg 값을 적는다
	for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset->BodySetup.Num(); ++BodyIndex)
	{
		FVector CoM;
		float U = 0.0f, V = 0.0f;
		if (!ShouldDrawCoM(BodyIndex) || !GetBodyCenterOfMass(BodyIndex, CoM) || !ProjectToViewportUV(CoM, U, V))
			continue;
		char Text[32];
		snprintf(Text, sizeof(Text), "%.2f kg", GetBodyMass(BodyIndex));
		const ImVec2 Pos(Origin.x + U * Size.x + 6.0f, Origin.y + V * Size.y - 6.0f);
		DrawList->AddText(ImVec2(Pos.x + 1.0f, Pos.y + 1.0f), IM_COL32(0, 0, 0, 200), Text);
		DrawList->AddText(Pos, IM_COL32(255, 220, 80, 255), Text);
	}
}

// ---------- 잡기 ----------

namespace
{
	// 선형 가속도 드라이브. 질량과 상관없이 오차 1m당 GrabStiffness m/s²로 끈다. 감쇠는 임계 감쇠(2√k) 근처
	constexpr float GrabStiffness = 1000.0f;
	constexpr float GrabDamping = 60.0f;
	constexpr float GrabRayLength = 1000.0f;
}

FRay FPhysicsAssetEditor::GetMouseRay() const
{
	const FVector2 MousePosition(PointerInput.MouseUV.X * ViewportClient->GetWidth(), PointerInput.MouseUV.Y * ViewportClient->GetHeight());
	FRay Ray = ViewportClient->DeprojectScreenToWorld(MousePosition);
	Ray.Direction = Ray.Direction.GetSafeNormal();
	return Ray;
}

bool FPhysicsAssetEditor::TryBeginGrab()
{
	EndGrab();
	if (!CanProcessViewportInput() || !PointerInput.bHovered || !MeshComponent)
		return false;

	FPhysScene* Scene = GetPreviewPhysScene();
	physx::PxScene* PScene = Scene ? Scene->GetPxScene() : nullptr;
	physx::PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();
	if (!PScene || !Physics)
		return false;

	const FRay Ray = GetMouseRay();
	physx::PxRaycastBuffer Hit;
	{
		SCOPED_SCENE_READ_LOCK(PScene);
		// 바닥(정적)은 건너뛰고 움직이는 액터만 본다
		const physx::PxQueryFilterData Filter(physx::PxQueryFlag::eDYNAMIC);
		if (!PScene->raycast(K2PVector(Ray.Origin), K2PVector(Ray.Direction), GrabRayLength, Hit, physx::PxHitFlag::eDEFAULT, Filter) || !Hit.hasBlock)
			return false;
	}

	physx::PxRigidDynamic* Target = Hit.block.actor ? Hit.block.actor->is<physx::PxRigidDynamic>() : nullptr;
	FBodyInstance* Body = FBodyInstance::FromActor(Target);
	// 선택 시뮬레이트에서 고정된(키네마틱) 바디는 잡지 않는다
	if (!Target || !Body || Body->OwnerComponent != MeshComponent || !Body->IsSimulatingPhysics())
		return false;

	SCOPED_SCENE_WRITE_LOCK(PScene);

	const physx::PxVec3 HitPos = Hit.block.position;
	HandleActor = Physics->createRigidDynamic(physx::PxTransform(HitPos));
	if (!HandleActor)
		return false;
	HandleActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
	PScene->addActor(*HandleActor);

	// 조인트 프레임: 핸들 쪽은 핸들 원점, 바디 쪽은 맞은 지점. 두 프레임 모두 월드 축에 맞춘다
	const physx::PxTransform BodyFrame = Target->getGlobalPose().transformInv(physx::PxTransform(HitPos));
	HandleJoint = physx::PxD6JointCreate(*Physics, HandleActor, physx::PxTransform(physx::PxIdentity), Target, BodyFrame);
	if (!HandleJoint)
	{
		HandleActor->release();
		HandleActor = nullptr;
		return false;
	}

	// 6축 모두 풀고 선형 3축에만 드라이브를 건다. 잡은 지점을 축으로 자유롭게 돈다
	for (physx::PxD6Axis::Enum Axis : { physx::PxD6Axis::eX, physx::PxD6Axis::eY, physx::PxD6Axis::eZ,
		physx::PxD6Axis::eTWIST, physx::PxD6Axis::eSWING1, physx::PxD6Axis::eSWING2 })
	{
		HandleJoint->setMotion(Axis, physx::PxD6Motion::eFREE);
	}
	const physx::PxD6JointDrive Drive(GrabStiffness, GrabDamping, PX_MAX_F32, true);
	HandleJoint->setDrive(physx::PxD6Drive::eX, Drive);
	HandleJoint->setDrive(physx::PxD6Drive::eY, Drive);
	HandleJoint->setDrive(physx::PxD6Drive::eZ, Drive);
	HandleJoint->setDrivePosition(physx::PxTransform(physx::PxIdentity));
	Target->wakeUp();

	GrabbedBody = Body;
	GrabLocalPos = P2KVector(BodyFrame.p);
	GrabTarget = P2KVector(HitPos);
	GrabDistance = Hit.block.distance;
	return true;
}

void FPhysicsAssetEditor::UpdateGrab()
{
	if (!bSimulating || !ViewportClient || !PointerInput.bRightMouseDown || !PointerInput.bFocused || PointerInput.bCancel)
	{
		EndGrab();
		return;
	}

	const FRay Ray = GetMouseRay();
	GrabTarget = Ray.Origin + Ray.Direction * GrabDistance;

	SCOPED_SCENE_WRITE_LOCK(HandleActor->getScene());
	// setGlobalPose가 아니라 목표를 준다. 다음 스텝에서 속도를 가진 채 옮겨져 조인트가 부드럽게 끈다
	HandleActor->setKinematicTarget(physx::PxTransform(K2PVector(GrabTarget)));
	if (physx::PxRigidDynamic* Target = GrabbedBody && GrabbedBody->Actor ? GrabbedBody->Actor->is<physx::PxRigidDynamic>() : nullptr)
		Target->wakeUp();
}

void FPhysicsAssetEditor::EndGrab()
{
	if (HandleActor)
	{
		SCOPED_SCENE_WRITE_LOCK(HandleActor->getScene());
		if (HandleJoint)
			HandleJoint->release();
		HandleActor->release();   // 씬에서도 빠진다
	}
	else if (HandleJoint)
	{
		HandleJoint->release();
	}
	HandleJoint = nullptr;
	HandleActor = nullptr;
	GrabbedBody = nullptr;
}

void FPhysicsAssetEditor::DrawGrabHandle(ImDrawList* DrawList, const ImVec2& Origin, const ImVec2& Size) const
{
	if (!DrawList || !IsGrabbing() || !GrabbedBody || !GrabbedBody->Actor)
		return;

	FVector GrabPoint;
	{
		SCOPED_SCENE_READ_LOCK(GrabbedBody->Actor->getScene());
		GrabPoint = P2KVector(GrabbedBody->Actor->getGlobalPose().transform(K2PVector(GrabLocalPos)));
	}

	float U0 = 0.0f, V0 = 0.0f, U1 = 0.0f, V1 = 0.0f;
	if (!ProjectToViewportUV(GrabPoint, U0, V0) || !ProjectToViewportUV(GrabTarget, U1, V1))
		return;

	// 화면에서 잡은 지점 → 핸들 사이를 일정 간격으로 찍는다 (양 끝 포함)
	const ImVec2 A(Origin.x + U0 * Size.x, Origin.y + V0 * Size.y);
	const ImVec2 B(Origin.x + U1 * Size.x, Origin.y + V1 * Size.y);
	constexpr float Spacing = 16.0f;
	constexpr float HalfSize = 4.0f;
	const float Length = sqrtf((B.x - A.x) * (B.x - A.x) + (B.y - A.y) * (B.y - A.y));
	const int32 Steps = FMath::Max(1, static_cast<int32>(Length / Spacing));
	for (int32 i = 0; i <= Steps; ++i)
	{
		const float T = static_cast<float>(i) / static_cast<float>(Steps);
		const ImVec2 P(A.x + (B.x - A.x) * T, A.y + (B.y - A.y) * T);
		DrawList->AddRectFilled(ImVec2(P.x - HalfSize, P.y - HalfSize), ImVec2(P.x + HalfSize, P.y + HalfSize), IM_COL32(255, 0, 0, 255));
	}
}

// ---------- 뷰포트 입력 ----------

void FPhysicsAssetEditor::TickAfterWorld(float DeltaTime)
{
	// 시뮬레이션 중에는 기즈모를 숨기고 잡기(Ctrl+우클릭)와 클릭 선택만 받는다
	if (bSimulating)
	{
		PhysicsGizmo.SetTarget(nullptr);
		if (IsGrabbing())
		{
			UpdateGrab();
			return;
		}
		if (PointerInput.bRightMousePressed && PointerInput.bCtrlDown && TryBeginGrab())
			return;
		UpdateShapePicking();
		return;
	}

	// 기즈모가 입력을 쓰지 않았을 때만 피킹하고, 마지막에 기즈모를 선택한 대상에 맞춘다
	if (!UpdatePhysicsGizmo())
		UpdateShapePicking();
	SyncPhysicsGizmo();
}

void FPhysicsAssetEditor::UpdateShapePicking()
{
	if (!CanProcessViewportInput() || !PhysicsAsset || !Mesh || !MeshComponent ||
		!PointerInput.bHovered || !PointerInput.bMousePressed || PointerInput.bCancel ||
		CameraInput.bCaptured || IsViewportInteractionBusy())
	{
		return;
	}

	const FVector2 MousePosition(PointerInput.MouseUV.X * ViewportClient->GetWidth(), PointerInput.MouseUV.Y * ViewportClient->GetHeight());
	const FRay Ray = ViewportClient->DeprojectScreenToWorld(MousePosition);
	// Shift·Ctrl+클릭은 선택에 넣거나 뺀다. 빈 곳을 눌러도 선택을 지우지 않는다
	const EPhysicsAssetSelectMode PickMode = (PointerInput.bShiftDown || PointerInput.bCtrlDown)
		? EPhysicsAssetSelectMode::Toggle : EPhysicsAssetSelectMode::Replace;

	// 조인트가 바디 안쪽에 있으므로 조인트를 먼저 본다 (화면에서도 항상 위에 그린다)
	if (GetConstraintDrawMode() != EPhysicsConstraintDrawMode::None)
	{
		int32 BestConstraint = -1;
		float BestT = FLT_MAX;
		for (int32 i = 0; i < PhysicsAsset->ConstraintSetup.Num(); ++i)
		{
			if (!ShouldDrawConstraint(i))
				continue;
			FVector Pos;
			FQuat Rot;
			GetConstraintFrameWorld(PhysicsAsset->ConstraintSetup[i]->DefaultInstance, false, Pos, Rot);
			const float T = RaySphere(Ray.Origin, Ray.Direction, Pos, GetConstraintDrawLength(IsConstraintSelected(i)) * 0.5f);
			if (T >= 0.0f && T < BestT)
			{
				BestT = T;
				BestConstraint = i;
			}
		}
		if (BestConstraint >= 0)
		{
			SelectConstraint(BestConstraint, PickMode);
			return;
		}
	}

	int32 BestBody = -1;
	int32 BestPrimitive = -1;
	float BestT = FLT_MAX;
	if (GetBodyDrawMode() != EPhysicsBodyDrawMode::None)
	{
		for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset->BodySetup.Num(); ++BodyIndex)
		{
			const UBodySetup* Setup = PhysicsAsset->BodySetup[BodyIndex];
			const int32 Bone = GetBodyBoneIndex(BodyIndex);
			if (!Setup || Bone < 0 || !ShouldDrawBody(BodyIndex))
				continue;

			const FMatrix BoneToWorld = MeshComponent->GetBoneWorldMatrix(Bone);
			int32 PrimitiveIndex = 0;
			auto Test = [&](const FKShapeElem& Elem, auto&& Intersect)
			{
				// 광선을 형상 로컬 공간으로 옮겨 검사한다
				const FMatrix WorldToElem = (MakeMatrix(Elem.Center, Elem.Rotation) * BoneToWorld).Inverse();
				const float T = Intersect(WorldToElem.TransformPosition(Ray.Origin), WorldToElem.TransformVector(Ray.Direction));
				if (T >= 0.0f && T < BestT)
				{
					BestT = T;
					BestBody = BodyIndex;
					BestPrimitive = PrimitiveIndex;
				}
				++PrimitiveIndex;
			};

			for (const FKSphylElem& S : Setup->AggGeom.SphylElems)
				Test(S, [&](const FVector& O, const FVector& D) { return RayCapsule(O, D, S.Radius, S.Length * 0.5f); });
			for (const FKBoxElem& B : Setup->AggGeom.BoxElems)
				Test(B, [&](const FVector& O, const FVector& D) { return RayBox(O, D, B.Extent); });
			for (const FKSphereElem& S : Setup->AggGeom.SphereElems)
				Test(S, [&](const FVector& O, const FVector& D) { return RaySphere(O, D, FVector(0, 0, 0), S.Radius); });
		}
	}

	if (BestBody >= 0)
		SelectBody(BestBody, BestPrimitive, PickMode);
	else if (PickMode == EPhysicsAssetSelectMode::Replace)
		ClearSelection();
}

void FPhysicsAssetEditor::SyncPhysicsGizmo()
{
	if (!GizmoTarget || PhysicsGizmo.IsUsing())
		return;

	FVector Location;
	FQuat Rotation;
	bool bHasTarget = false;

	if (UBodySetup* Body = GetSelectedBody())
	{
		const int32 Bone = GetBodyBoneIndex(Selection.Index);
		if (FKShapeElem* Elem = GetGizmoShape(*Body); Elem && Bone >= 0 && MeshComponent)
		{
			const FMatrix ElemToWorld = MakeMatrix(Elem->Center, Elem->Rotation) * MeshComponent->GetBoneWorldMatrix(Bone);
			Location = ElemToWorld.TransformPosition(FVector(0, 0, 0));
			Rotation = QuatFromMatrix(ElemToWorld);
			bHasTarget = true;
		}
	}
	else if (UPhysicsConstraintTemplate* Template = GetSelectedConstraint())
	{
		// Alt만 누르면 부모 프레임, 그 외에는 자식 프레임에 둔다
		const bool bParent = PointerInput.bAltDown && !PointerInput.bShiftDown;
		GetConstraintFrameWorld(Template->DefaultInstance, bParent, Location, Rotation);
		if (PhysicsGizmo.GetMode() == EGizmoMode::Scale)
			PhysicsGizmo.SetMode(EGizmoMode::Location);   // 조인트 프레임에는 크기가 없다
		bHasTarget = true;
	}

	if (!bHasTarget)
	{
		PhysicsGizmo.SetTarget(nullptr);
		return;
	}

	GizmoTarget->SetTransform(FTransform(Rotation, Location));   // 스케일 1: 크기 드래그는 1에 대한 배율이 된다
	PhysicsGizmo.SetTarget(GizmoTarget);
}

bool FPhysicsAssetEditor::UpdatePhysicsGizmo()
{
	if (!CanProcessViewportInput() || !PhysicsGizmo.GetTarget())
	{
		PhysicsGizmo.EndDrag();
		return false;
	}

	if (PointerInput.bCancel)
	{
		if (PhysicsGizmo.IsUsing())
			RestoreGizmoDragStart();
		PhysicsGizmo.EndDrag();
		return false;
	}

	const bool bWasUsing = PhysicsGizmo.IsUsing();
	if ((!PointerInput.bHovered && !bWasUsing) || CameraInput.bCaptured)
	{
		PhysicsGizmo.EndDrag();
		return false;
	}

	SyncPhysicsGizmo();
	const int32 Width = static_cast<int32>(ViewportClient->GetWidth());
	const int32 Height = static_cast<int32>(ViewportClient->GetHeight());
	const FVector2 MousePosition(PointerInput.MouseUV.X * Width, PointerInput.MouseUV.Y * Height);
	const FRay MouseRay = ViewportClient->DeprojectScreenToWorld(MousePosition);

	// 클릭한 프레임에만 시작하고, 버튼을 놓는 프레임의 마지막 위치도 반영한다
	const bool bUpdateDrag = bWasUsing || (PointerInput.bMousePressed && PointerInput.bMouseDown);
	PhysicsGizmo.Update(MouseRay, MousePosition, ViewportClient->GetViewProjectionMatrix(),
		Width, Height, bUpdateDrag, ViewportClient->GetViewLocation(), !ViewportClient->IsPerspective());

	if (!bWasUsing && PhysicsGizmo.IsUsing())
		BeginGizmoDrag();

	if (PhysicsGizmo.IsUsing())
	{
		ApplyGizmoDrag();
		if (!PointerInput.bMouseDown)
			PhysicsGizmo.EndDrag();
	}

	return bWasUsing || PhysicsGizmo.IsUsing() || PhysicsGizmo.GetHoveredAxis() >= 0;
}

void FPhysicsAssetEditor::BeginGizmoDrag()
{
	DragStartGizmoLocation = GizmoTarget->GetRelativeLocation();
	DragStartGizmoRotation = GizmoTarget->GetRelativeRotation().Quaternion();

	if (UBodySetup* Body = GetSelectedBody())
	{
		DragStartGeom = Body->AggGeom;
	}
	else if (UPhysicsConstraintTemplate* Template = GetSelectedConstraint())
	{
		const FConstraintInstance& CI = Template->DefaultInstance;
		for (int32 k = 0; k < 2; ++k)
			GetConstraintFrameWorld(CI, k == 1, DragStartPos[k], DragStartRot[k]);
		DragStartLocalPos[0] = CI.Pos1; DragStartPriAxis[0] = CI.PriAxis1; DragStartSecAxis[0] = CI.SecAxis1;
		DragStartLocalPos[1] = CI.Pos2; DragStartPriAxis[1] = CI.PriAxis2; DragStartSecAxis[1] = CI.SecAxis2;

		// 기본은 두 프레임을 같이, Alt는 부모만, Shift+Alt는 자식만 옮긴다
		bDragChildFrame = !(PointerInput.bAltDown && !PointerInput.bShiftDown);
		bDragParentFrame = !(PointerInput.bAltDown && PointerInput.bShiftDown);
	}
}

void FPhysicsAssetEditor::ApplyGizmoDrag()
{
	const FVector TargetLocation = GizmoTarget->GetRelativeLocation();
	const FQuat TargetRotation = GizmoTarget->GetRelativeRotation().Quaternion();
	const FVector TargetScale = GizmoTarget->GetRelativeScale3D();
	const EGizmoMode Mode = PhysicsGizmo.GetMode();

	if (UBodySetup* Body = GetSelectedBody())
	{
		const int32 Bone = GetBodyBoneIndex(Selection.Index);
		if (Bone < 0)
			return;

		// 매 프레임 드래그 시작 값에서 다시 계산한다 (오차가 쌓이지 않게)
		Body->AggGeom = DragStartGeom;
		EPhysicsAssetPrimitive Type = EPhysicsAssetPrimitive::Capsule;
		FKShapeElem* Elem = GetGizmoShape(*Body, &Type);
		if (!Elem)
			return;

		const FMatrix BoneToWorld = MeshComponent->GetBoneWorldMatrix(Bone);
		switch (Mode)
		{
		case EGizmoMode::Location:
			Elem->Center = BoneToWorld.Inverse().TransformPosition(TargetLocation);
			break;
		case EGizmoMode::Rotation:
			Elem->Rotation = (QuatFromMatrix(BoneToWorld).Inverse() * TargetRotation).Normalized();
			break;
		case EGizmoMode::Scale:
		{
			const FVector S(std::abs(TargetScale.X), std::abs(TargetScale.Y), std::abs(TargetScale.Z));
			const float MinSize = 0.0001f;
			if (Type == EPhysicsAssetPrimitive::Capsule)
			{
				FKSphylElem* Sphyl = static_cast<FKSphylElem*>(Elem);
				Sphyl->Radius = FMath::Max(Sphyl->Radius * FMath::Max(S.X, S.Y), MinSize);
				Sphyl->Length = FMath::Max(Sphyl->Length * S.Z, 0.0f);
			}
			else if (Type == EPhysicsAssetPrimitive::Box)
			{
				FKBoxElem* Box = static_cast<FKBoxElem*>(Elem);
				Box->Extent = FVector(FMath::Max(Box->Extent.X * S.X, MinSize), FMath::Max(Box->Extent.Y * S.Y, MinSize), FMath::Max(Box->Extent.Z * S.Z, MinSize));
			}
			else
			{
				FKSphereElem* Sphere = static_cast<FKSphereElem*>(Elem);
				Sphere->Radius = FMath::Max(Sphere->Radius * FMath::Max(S.X, FMath::Max(S.Y, S.Z)), MinSize);
			}
			break;
		}
		default:
			break;
		}
		NotifyAssetEdited();
		return;
	}

	UPhysicsConstraintTemplate* Template = GetSelectedConstraint();
	if (!Template || Mode == EGizmoMode::Scale)
		return;

	FConstraintInstance& CI = Template->DefaultInstance;
	const FVector DeltaLocation = TargetLocation - DragStartGizmoLocation;
	const FQuat DeltaRotation = (TargetRotation * DragStartGizmoRotation.Inverse()).Normalized();

	for (int32 k = 0; k < 2; ++k)
	{
		if (!(k == 0 ? bDragChildFrame : bDragParentFrame))
			continue;

		const int32 Bone = Mesh->FindBoneIndex((k == 1 ? CI.ConstraintBone2 : CI.ConstraintBone1).ToString());
		if (Bone < 0)
			continue;

		// 기즈모가 움직인 만큼 프레임을 옮긴다. 회전은 기즈모 위치를 중심으로 돌린다
		FVector NewLocation = DragStartPos[k];
		FQuat NewRotation = DragStartRot[k];
		if (Mode == EGizmoMode::Location)
		{
			NewLocation = DragStartPos[k] + DeltaLocation;
		}
		else if (Mode == EGizmoMode::Rotation)
		{
			NewLocation = DragStartGizmoLocation + DeltaRotation.RotateVector(DragStartPos[k] - DragStartGizmoLocation);
			NewRotation = (DeltaRotation * DragStartRot[k]).Normalized();
		}

		// 월드 → 본 공간
		const FMatrix BoneToWorld = MeshComponent->GetBoneWorldMatrix(Bone);
		const FQuat Local = (QuatFromMatrix(BoneToWorld).Inverse() * NewRotation).Normalized();
		FVector& Pos = k == 1 ? CI.Pos2 : CI.Pos1;
		FVector& Pri = k == 1 ? CI.PriAxis2 : CI.PriAxis1;
		FVector& Sec = k == 1 ? CI.SecAxis2 : CI.SecAxis1;
		Pos = BoneToWorld.Inverse().TransformPosition(NewLocation);
		Pri = Local.RotateVector(FVector(1, 0, 0));
		Sec = Local.RotateVector(FVector(0, 1, 0));
	}
	NotifyAssetEdited();
}

void FPhysicsAssetEditor::RestoreGizmoDragStart()
{
	if (UBodySetup* Body = GetSelectedBody())
	{
		Body->AggGeom = DragStartGeom;
	}
	else if (UPhysicsConstraintTemplate* Template = GetSelectedConstraint())
	{
		FConstraintInstance& CI = Template->DefaultInstance;
		CI.Pos1 = DragStartLocalPos[0]; CI.PriAxis1 = DragStartPriAxis[0]; CI.SecAxis1 = DragStartSecAxis[0];
		CI.Pos2 = DragStartLocalPos[1]; CI.PriAxis2 = DragStartPriAxis[1]; CI.SecAxis2 = DragStartSecAxis[1];
	}
	NotifyAssetEdited();
}
