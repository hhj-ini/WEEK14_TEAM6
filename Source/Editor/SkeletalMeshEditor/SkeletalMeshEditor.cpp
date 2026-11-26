#include "EnginePCH.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Editor/AssetEditor/EditorManager.h"
#include "SkeletalMeshEditorTab.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Render/SkeletalMeshObject.h"
#include "Render/LineBatcher.h"
#include "Clothing/ClothingAsset.h"
#include "Clothing/ClothingSimulationNv.h"
#include "Collision/Ray.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetUtils.h"
#include "Physics/PhysicsAssetFile.h"
#include "Asset/AssetUtils.h"
#include "Asset/AssetManager.h"
#include "Physics/PhysicsDebugDraw.h"

#include <cmath>
#include <filesystem>

namespace
{
	FString MakePhysicsAssetPath(const USkeletalMesh& Mesh)
	{
		std::filesystem::path Path(FAssetUtils::ResolveAssetFilePath(Mesh.GetPath()));
		Path.replace_extension(".phys");
		return Path.generic_string();
	}
}

bool FSkeletalMeshEditor::InitializeResources()
{
	if (!InitializePreview()) return false;
	BoneGizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
	if (!BoneGizmoTarget) return false;
	BoneGizmoTarget->DetachFromParent();
	ResetBonePoseHistory();
	LoadPhysicsAsset();   // 이전에 Generate해 둔 .phys가 있으면 읽어 온다

	if (Mesh)
	{
		const auto& LODs = Mesh->GetRenderData().LODRenderData;
		if (!LODs.IsEmpty())
		{
			const float Diag = (LODs[0].BindAABB.Max - LODs[0].BindAABB.Min).Size();
			if (Diag > 0.0f)
			{
				ClothPaintSettings.BrushRadius = FMath::Clamp(Diag * 0.06f, 0.05f, 25.0f);
			}
		}
		if (!Mesh->GetClothingAssets().IsEmpty())
		{
			SelectedClothingAsset = Mesh->GetClothingAssets()[0];
		}
	}
	return true;
}

void FSkeletalMeshEditor::ShutdownResources()
{
	if (bClothPaintActive)
	{
		SetClothPaintActive(false);
	}
	SelectedClothingAsset = nullptr;
	ClothUndoStack.Empty();
	ClothRedoStack.Empty();
	bClothStrokeActive = false;
	bClothStrokeModified = false;
	ClothStrokeBefore = {};
	bBrushHitValid = false;
	BrushAffectedVertWorldPositions.Empty();

	PoseUndoStack.Empty();
	PoseRedoStack.Empty();
	CommittedBonePose.Empty();
	bPoseUndoPending = false;
	BoneGizmo.EndDrag();
	BoneGizmo.SetTarget(nullptr);
	BoneGizmoTarget = nullptr;
	FSkeletalPreviewEditor::ShutdownResources();
}

void FSkeletalMeshEditor::TickAfterWorld(float DeltaTime)
{
	if (bClothPaintActive)
	{
		BoneGizmo.EndDrag();
		BoneGizmo.SetTarget(nullptr);
		UpdateClothPainting(DeltaTime);
		return;
	}

	// 기즈모가 입력을 쓰지 않았을 때만 본을 피킹하고, 마지막에 기즈모를 선택한 본 위치로 맞춘다
	if (!UpdateBoneGizmo())
		UpdateBonePicking();
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::OnBoneSelected()
{
	BoneGizmo.EndDrag();
	if (bClothPaintActive)
	{
		// SelectBone() 내부 RefreshHeatmap()이 DebugWeightBone을 덮어쓰므로 페인트 모드를 복원한다
		if (MeshComponent)
		{
			MeshComponent->SetDebugWeightBone(-2);
		}
		RefreshClothPaintOverlay();
		BoneGizmo.SetTarget(nullptr);
		return;
	}
	SyncBoneGizmo();
}

TUniquePtr<IEditorTab> FSkeletalMeshEditor::CreateTab()
{
	return MakeUnique<FSkeletalMeshEditorTab>(*this);
}

void FSkeletalMeshEditor::SetGizmoMode(EGizmoMode Mode)
{
	BoneGizmo.EndDrag();
	BoneGizmo.SetMode(Mode);
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::SetGizmoSpace(EGizmoSpace Space)
{
	BoneGizmo.EndDrag();
	BoneGizmo.SetSpace(Space);
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::OpenAnimationAsset(const FString& InAssetPath)
{
	if (!InAssetPath.empty())
	{
		EditorManager.OpenEditor(InAssetPath);
	}
}

void FSkeletalMeshEditor::GeneratePhysicsAsset()
{
	if (!Mesh)
		return;

	// 로드된 에셋이 있으면 그 객체를 그대로 덮어쓴다.
	// 새로 NewObject하면 AssetManager 캐시에 옛 객체가 남아, 다른 곳에서 Load하면 예전 결과가 나온다.
	if (!PhysicsAsset)
		PhysicsAsset = NewObject<UPhysicsAsset>();

	FPhysAssetCreateParams Params;   // 기본값으로 시작. 결과 보고 조정
	FString Error;
	if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(*PhysicsAsset, *Mesh, Params, Error))
	{
		KRS_LOG(Warning, "[Physics] Generate failed: {}", Error);
		return;
	}

	// 메시 파일 옆에 같은 이름으로 저장 (Character.fbx → Character.phys). 로드와 같은 경로 함수를 쓴다
	const FString Path = MakePhysicsAssetPath(*Mesh);
	Mesh->SetPhysicsAsset(PhysicsAsset);
	if (!PhysicsAssetFile::Write(std::filesystem::path(Path), *PhysicsAsset, Error))
		KRS_LOG(Warning, "[Physics] Save failed: {}", Error);
	else
		KRS_LOG(Info, "[Physics] Saved {}", Path);
}

void FSkeletalMeshEditor::LoadPhysicsAsset()
{
	PhysicsAsset = nullptr;
	if (!Mesh)
		return;

	const FString Path = MakePhysicsAssetPath(*Mesh);
	if (!std::filesystem::exists(Path))
		return;   // 아직 Generate한 적 없음 → 조용히 넘어간다 (경고 X)

	PhysicsAsset = UAssetManager::LoadPhysicsAsset(Path);
}

void FSkeletalMeshEditor::OpenPhysicsAssetEditor()
{
	if (!Mesh)
		return;

	const FString Path = MakePhysicsAssetPath(*Mesh);
	if (!std::filesystem::exists(Path))
	{
		GeneratePhysicsAsset();
		if (!std::filesystem::exists(Path))
			return;   // 생성·저장 실패는 GeneratePhysicsAsset이 로그를 남긴다

		// 에셋 매니저 캐시의 객체로 바꿔 두어야 Physics Asset 에디터와 같은 객체를 편집한다
		LoadPhysicsAsset();
	}

	EditorManager.OpenEditor(Path);
}

void FSkeletalMeshEditor::DrawPhysicsBodies(FLineBatcher& LineBatcher) const
{
	if (!bShowPhysicsBodies || !PhysicsAsset || !Mesh || !MeshComponent)
		return;

	const FVector4 Color(0.2f, 1.0f, 0.3f, 1.0f);
	for (UBodySetup* Setup : PhysicsAsset->BodySetup)
	{
		const int32 Bone = Mesh->FindBoneIndex(Setup->BoneName.ToString());
		if (Bone < 0)
			continue;

		DrawAggGeomWire(LineBatcher, MeshComponent->GetBoneWorldMatrix(Bone), Setup->AggGeom, Color);
	}
}

void FSkeletalMeshEditor::SyncBoneGizmo()
{
	if (!MeshComponent || !BoneGizmoTarget || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num() ||
		BoneDrawMode == EBoneDrawMode::None)
	{
		BoneGizmo.SetTarget(nullptr);
		return;
	}

	if (BoneGizmo.IsUsing())
	{
		return;
	}

	const FMatrix BoneWorld = MeshComponent->GetBoneWorldMatrix(SelectedBoneIndex);
	const FVector Location = BoneWorld.TransformPosition(FVector::ZeroVector);
	const FRotator Rotation = GetBoneWorldRotation(SelectedBoneIndex).ToFRotator();

	// 스케일은 조작 배율로만 사용합니다. 부모의 비균일 스케일과 전단을 축 회전에 섞지 않습니다.
	BoneGizmoTarget->SetTransform(FTransform(Rotation, Location));
	BoneGizmo.SetTarget(BoneGizmoTarget);
}

FQuat FSkeletalMeshEditor::GetBoneWorldRotation(int32 BoneIndex) const
{
	FQuat Rotation = FQuat::Identity();
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	while (BoneIndex >= 0)
	{
		Rotation = MeshComponent->GetBoneLocalTransform(BoneIndex).Rotation * Rotation;
		BoneIndex = Bones[BoneIndex].ParentIndex;
	}

	return (MeshComponent->GetWorldRotation().Quaternion() * Rotation).Normalized();
}

bool FSkeletalMeshEditor::UpdateBoneGizmo()
{
	if
		(
			// 공통 뷰포트 입력 조건을 만족하지 않으면 입력을 무시
			!CanProcessViewportInput() ||

			// BoneGizmo의 대상이 없으면 입력을 무시
			!BoneGizmo.GetTarget()
			)
	{
		BoneGizmo.EndDrag();
		return false;
	}

	if (PointerInput.bCancel)
	{
		if (BoneGizmo.IsUsing())
		{
			MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, DragStartBoneTransform);
			MeshComponent->RefreshBoneTransformsNow();
		}
		BoneGizmo.EndDrag();
		return false;
	}

	const bool bWasUsing = BoneGizmo.IsUsing();
	if ((!PointerInput.bHovered && !bWasUsing) || CameraInput.bCaptured)
	{
		BoneGizmo.EndDrag();
		return false;
	}

	SyncBoneGizmo();
	const int32 Width = static_cast<int32>(ViewportClient->GetWidth());
	const int32 Height = static_cast<int32>(ViewportClient->GetHeight());
	const FVector2 MousePosition(PointerInput.MouseUV.X * Width, PointerInput.MouseUV.Y * Height);
	const FRay MouseRay = ViewportClient->DeprojectScreenToWorld(MousePosition);

	// 클릭한 프레임에만 시작하고, 버튼을 놓는 프레임의 마지막 마우스 위치도 반영합니다.
	const bool bUpdateDrag = bWasUsing || (PointerInput.bMousePressed && PointerInput.bMouseDown);
	BoneGizmo.Update(MouseRay, MousePosition, ViewportClient->GetViewProjectionMatrix(),
		Width, Height, bUpdateDrag, ViewportClient->GetViewLocation(), !ViewportClient->IsPerspective());

	if (!bWasUsing && BoneGizmo.IsUsing())
	{
		const int32 ParentIndex = Mesh->GetRefSkeleton()[SelectedBoneIndex].ParentIndex;
		const FMatrix ParentWorld = ParentIndex >= 0
			? MeshComponent->GetBoneWorldMatrix(ParentIndex) : MeshComponent->GetWorldMatrix();

		// 스케일이 0인 부모의 역행렬로 이동을 계산할 수 없습니다.
		if (BoneGizmo.GetMode() == EGizmoMode::Location && fabsf(ParentWorld.Determinant()) < 1e-12f)
		{
			BoneGizmo.EndDrag();
			return true;
		}

		DragStartBoneTransform = MeshComponent->GetBoneLocalTransform(SelectedBoneIndex);
		DragParentRotation = GetBoneWorldRotation(ParentIndex);
		if (BoneGizmo.GetMode() == EGizmoMode::Location)
		{
			DragParentInverse = ParentWorld.Inverse();
		}
	}

	if (BoneGizmo.IsUsing())
	{
		ApplyBoneGizmoTransform();
		if (!PointerInput.bMouseDown)
		{
			BoneGizmo.EndDrag();
		}
	}

	return bWasUsing || BoneGizmo.IsUsing() || BoneGizmo.GetHoveredAxis() >= 0;
}

void FSkeletalMeshEditor::ApplyBoneGizmoTransform()
{
	FTransform Local = DragStartBoneTransform;
	switch (BoneGizmo.GetMode())
	{
	case EGizmoMode::Location:
		Local.Location = DragParentInverse.TransformPosition(BoneGizmoTarget->GetWorldLocation());
		break;
	case EGizmoMode::Rotation:
		Local.Rotation = (DragParentRotation.Inverse() *
			BoneGizmoTarget->GetRelativeRotation().Quaternion()).Normalized();
		break;
	case EGizmoMode::Scale:
		Local.Scale = DragStartBoneTransform.Scale * BoneGizmoTarget->GetRelativeScale3D();
		break;
	}

	// 조작한 성분만 변경하고 나머지는 드래그 시작 시점의 로컬 값을 유지합니다.
	MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, Local);
	MeshComponent->RefreshBoneTransformsNow();
	bPoseUndoPending = true;
}

bool FSkeletalMeshEditor::GetSelectedBoneLocalTransform(FTransform& OutTransform) const
{
	if (!MeshComponent || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		return false;
	}

	OutTransform = MeshComponent->GetBoneLocalTransform(SelectedBoneIndex);
	return true;
}

void FSkeletalMeshEditor::SetSelectedBoneLocalTransform(const FTransform& Transform)
{
	if (!MeshComponent || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		return;
	}

	// 원본 애셋 대신 프리뷰 컴포넌트의 포즈만 수정합니다. 본 갱신은 다음 World Tick에서 수행합니다.
	BoneGizmo.EndDrag();
	MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, Transform);
	bPoseUndoPending = true;
}

void FSkeletalMeshEditor::ResetPose()
{
	if (!MeshComponent)
	{
		return;
	}

	BoneGizmo.EndDrag();
	MeshComponent->ResetPose();
	bPoseUndoPending = true;
}

void FSkeletalMeshEditor::ResetBone(int32 BoneIndex)
{
	if (!Mesh)
	{
		return;
	}

	if (!MeshComponent)
	{
		return;
	}

	if (BoneIndex < 0 || BoneIndex >= Mesh->GetRefSkeleton().Num())
	{
		return;
	}

	BoneGizmo.EndDrag();
	FTransform Transform = Mesh->GetRefSkeleton()[BoneIndex].LocalBindPose;
	MeshComponent->SetBoneLocalTransform(BoneIndex, Transform);
	bPoseUndoPending = true;
}

namespace
{
	bool AreBonePosesEqual(const TArray<FTransform>& A, const TArray<FTransform>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 i = 0; i < A.Num(); ++i)
		{
			if (A[i].Location.X != B[i].Location.X || A[i].Location.Y != B[i].Location.Y || A[i].Location.Z != B[i].Location.Z ||
				A[i].Rotation.X != B[i].Rotation.X || A[i].Rotation.Y != B[i].Rotation.Y || A[i].Rotation.Z != B[i].Rotation.Z || A[i].Rotation.W != B[i].Rotation.W ||
				A[i].Scale.X != B[i].Scale.X || A[i].Scale.Y != B[i].Scale.Y || A[i].Scale.Z != B[i].Scale.Z)
			{
				return false;
			}
		}
		return true;
	}
}

TArray<FTransform> FSkeletalMeshEditor::CaptureBonePose() const
{
	TArray<FTransform> Pose;
	if (!Mesh || !MeshComponent)
	{
		return Pose;
	}

	const int32 NumBones = Mesh->GetRefSkeleton().Num();
	Pose.Reserve(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		Pose.Add(MeshComponent->GetBoneLocalTransform(i));
	}
	return Pose;
}

void FSkeletalMeshEditor::ApplyBonePose(const TArray<FTransform>& Pose)
{
	if (!Mesh || !MeshComponent)
	{
		return;
	}

	BoneGizmo.EndDrag();
	const int32 NumBones = std::min(static_cast<int32>(Pose.Num()), static_cast<int32>(Mesh->GetRefSkeleton().Num()));
	for (int32 i = 0; i < NumBones; ++i)
	{
		MeshComponent->SetBoneLocalTransform(i, Pose[i]);
	}
	MeshComponent->RefreshBoneTransformsNow();
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::ResetBonePoseHistory()
{
	PoseUndoStack.Empty();
	PoseRedoStack.Empty();
	CommittedBonePose = CaptureBonePose();
	bPoseUndoPending = false;
}

void FSkeletalMeshEditor::CommitBonePoseUndo(bool bInteractionActive)
{
	if (!bPoseUndoPending || bInteractionActive || !MeshComponent)
	{
		return;
	}
	bPoseUndoPending = false;

	TArray<FTransform> Current = CaptureBonePose();
	if (AreBonePosesEqual(Current, CommittedBonePose))
	{
		return;
	}

	PoseUndoStack.Add(std::move(CommittedBonePose));
	if (PoseUndoStack.Num() > MaxUndoSteps)
	{
		PoseUndoStack.RemoveAt(0);
	}
	PoseRedoStack.Empty();
	CommittedBonePose = std::move(Current);
}

void FSkeletalMeshEditor::UndoPoseEdit()
{
	CommitBonePoseUndo(false);
	if (PoseUndoStack.IsEmpty() || !MeshComponent)
	{
		return;
	}

	PoseRedoStack.Add(std::move(CommittedBonePose));
	CommittedBonePose = std::move(PoseUndoStack[PoseUndoStack.Num() - 1]);
	PoseUndoStack.RemoveAt(PoseUndoStack.Num() - 1);
	ApplyBonePose(CommittedBonePose);
}

void FSkeletalMeshEditor::RedoPoseEdit()
{
	CommitBonePoseUndo(false);
	if (PoseRedoStack.IsEmpty() || !MeshComponent)
	{
		return;
	}

	PoseUndoStack.Add(std::move(CommittedBonePose));
	CommittedBonePose = std::move(PoseRedoStack[PoseRedoStack.Num() - 1]);
	PoseRedoStack.RemoveAt(PoseRedoStack.Num() - 1);
	ApplyBonePose(CommittedBonePose);
}

// ---------- Clothing & Cloth Paint ----------

void FSkeletalMeshEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
	FSkeletalPreviewEditor::OnRenderLine(LineBatcher);   // 본 시각화는 그대로
	DrawPhysicsBodies(LineBatcher);

	if (bClothPaintActive && bBrushHitValid)
	{
		DrawClothBrushOverlay(LineBatcher);
	}
}

void FSkeletalMeshEditor::GatherSectionVertexMask(int32 SectionIndex, TArray<uint8>& OutMask) const
{
	OutMask.Empty();
	if (!Mesh)
		return;

	const auto& LODs = Mesh->GetRenderData().LODRenderData;
	if (LODs.IsEmpty())
		return;

	const FSkeletalMeshLODRenderData& LOD = LODs[0];
	const int32 NumVerts = LOD.BindVertices.Num();
	OutMask.SetNum(NumVerts);

	if (SectionIndex < 0 || SectionIndex >= LOD.Sections.Num())
	{
		for (int32 v = 0; v < NumVerts; ++v)
			OutMask[v] = 1;
		return;
	}

	for (int32 v = 0; v < NumVerts; ++v)
		OutMask[v] = 0;

	const FSkeletalMeshSection& Sec = LOD.Sections[SectionIndex];
	const uint32 Start = Sec.StartIndex;
	const uint32 End = FMath::Min(Start + Sec.IndexCount, static_cast<uint32>(LOD.Indices.Num()));
	for (uint32 i = Start; i < End; ++i)
	{
		const uint32 VertIdx = LOD.Indices[i];
		if (VertIdx < static_cast<uint32>(NumVerts))
		{
			OutMask[VertIdx] = 1;
		}
	}
}

bool FSkeletalMeshEditor::RaycastPreviewMesh(
	const FVector2& MouseUV,
	FVector& OutLocalHitPos,
	FVector& OutLocalHitNormal,
	int32& OutSectionIndex) const
{
	if (!ViewportClient || !Mesh || !MeshComponent)
		return false;

	const auto& LODs = Mesh->GetRenderData().LODRenderData;
	if (LODs.IsEmpty())
		return false;

	const FSkeletalMeshLODRenderData& LOD = LODs[0];
	const TArray<FVertex>* Verts = nullptr;
	if (const FSkeletalMeshObject* MeshObj = MeshComponent->GetMeshObject())
	{
		Verts = MeshObj->GetCPUSkinnedVertices();
	}
	if (!Verts || Verts->IsEmpty())
	{
		Verts = &LOD.BindVertices;
	}
	if (Verts->IsEmpty() || LOD.Indices.IsEmpty())
		return false;

	const float Width = static_cast<float>(ViewportClient->GetWidth());
	const float Height = static_cast<float>(ViewportClient->GetHeight());
	if (Width < 1.0f || Height < 1.0f)
		return false;

	const FVector2 MousePos(MouseUV.X * Width, MouseUV.Y * Height);
	const FRay WorldRay = ViewportClient->DeprojectScreenToWorld(MousePos);
	const FRay LocalRay = ToLocalRay(WorldRay, MeshComponent->GetWorldMatrix());

	float BestT = FLT_MAX;
	bool bHit = false;
	FVector BestNormal(0.0f, 0.0f, 1.0f);
	int32 BestSection = 0;

	const int32 NumVerts = Verts->Num();
	const uint32 NumIndices = static_cast<uint32>(LOD.Indices.Num());

	auto TestRange = [&](uint32 StartIdx, uint32 EndIdx, int32 SecIdx)
	{
		const uint32 ClampedEnd = FMath::Min(EndIdx, NumIndices);
		for (uint32 i = StartIdx; i + 2 < ClampedEnd; i += 3)
		{
			const uint32 I0 = LOD.Indices[i];
			const uint32 I1 = LOD.Indices[i + 1];
			const uint32 I2 = LOD.Indices[i + 2];
			if (I0 >= static_cast<uint32>(NumVerts) ||
				I1 >= static_cast<uint32>(NumVerts) ||
				I2 >= static_cast<uint32>(NumVerts))
			{
				continue;
			}

			const FVector& P0 = (*Verts)[I0].Position;
			const FVector& P1 = (*Verts)[I1].Position;
			const FVector& P2 = (*Verts)[I2].Position;
			float T = 0.0f;
			if (RayIntersectsTriangle(LocalRay, P0, P1, P2, T) && T < BestT)
			{
				BestT = T;
				BestSection = SecIdx;
				FVector N = FVector::Cross(P1 - P0, P2 - P0).GetSafeNormal(SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
				if (N.Dot(LocalRay.Direction) > 0.0f)
				{
					N = -N;
				}
				BestNormal = N;
				bHit = true;
			}
		}
	};

	if (LOD.Sections.IsEmpty())
	{
		TestRange(0, NumIndices, 0);
	}
	else
	{
		for (int32 SecIdx = 0; SecIdx < LOD.Sections.Num(); ++SecIdx)
		{
			const FSkeletalMeshSection& Sec = LOD.Sections[SecIdx];
			TestRange(Sec.StartIndex, Sec.StartIndex + Sec.IndexCount, SecIdx);
		}
	}

	if (!bHit)
		return false;

	OutLocalHitPos = LocalRay.PointAt(BestT);
	OutLocalHitNormal = BestNormal;
	OutSectionIndex = BestSection;
	return true;
}

bool FSkeletalMeshEditor::PickMeshSection(const FVector2& MouseUV, int32& OutSectionIndex) const
{
	FVector HitPos;
	FVector HitNormal;
	return RaycastPreviewMesh(MouseUV, HitPos, HitNormal, OutSectionIndex);
}

UClothingAssetCommon* FSkeletalMeshEditor::CreateClothingFromSection(int32 SectionIndex, const FString& Name)
{
	if (!Mesh)
		return nullptr;

	UClothingAssetCommon* NewAsset = Mesh->CreateClothingAssetFromSection(SectionIndex, Name);
	if (!NewAsset)
		return nullptr;

	SelectedClothingAsset = NewAsset;
	bClothingDirty = true;
	ClothStatusMessage = "Created clothing asset: " + NewAsset->GetAssetName();
	if (bClothPaintActive)
	{
		RefreshClothPaintOverlay();
	}
	return NewAsset;
}

void FSkeletalMeshEditor::ApplyClothingToSection(int32 SectionIndex, UClothingAssetCommon* Asset)
{
	if (!Mesh || !Asset)
		return;

	if (!Mesh->ApplyClothingAssetToSection(SectionIndex, Asset))
		return;

	Asset->RebuildFromPaintedWeights(*Mesh);
	Mesh->RebuildClothingMappingBuffer();
	SelectedClothingAsset = Asset;
	bClothingDirty = true;
	ClothStatusMessage = "Applied '" + Asset->GetAssetName() + "' to Section " + std::to_string(SectionIndex);

	if (bClothPaintActive)
	{
		RefreshClothPaintOverlay();
	}
	else if (MeshComponent)
	{
		MeshComponent->CreateClothingSimulation();
	}
}

void FSkeletalMeshEditor::RemoveClothingFromSection(int32 SectionIndex)
{
	if (!Mesh)
		return;

	Mesh->RemoveClothingAssetFromSection(SectionIndex);
	bClothingDirty = true;
	ClothStatusMessage = "Removed clothing data from Section " + std::to_string(SectionIndex);

	if (bClothPaintActive)
	{
		RefreshClothPaintOverlay();
	}
	else if (MeshComponent)
	{
		MeshComponent->CreateClothingSimulation();
	}
}

void FSkeletalMeshEditor::DeleteClothingAsset(UClothingAssetCommon* Asset)
{
	if (!Mesh || !Asset)
		return;

	ClearClothPaintHistoryForAsset(Asset);

	const FString RemovedName = Asset->GetAssetName();
	Mesh->RemoveClothingAsset(Asset);
	if (SelectedClothingAsset == Asset)
	{
		const auto& Assets = Mesh->GetClothingAssets();
		SelectedClothingAsset = Assets.IsEmpty() ? nullptr : Assets[0];
	}
	bClothingDirty = true;
	ClothStatusMessage = "Deleted clothing asset: " + RemovedName;

	if (bClothPaintActive)
	{
		RefreshClothPaintOverlay();
	}
	else if (MeshComponent)
	{
		MeshComponent->CreateClothingSimulation();
	}
}

void FSkeletalMeshEditor::SelectClothingAsset(UClothingAssetCommon* Asset)
{
	CommitClothPaintStroke();
	SelectedClothingAsset = Asset;
	if (bClothPaintActive)
	{
		RefreshClothPaintOverlay();
	}
}

void FSkeletalMeshEditor::SetClothPaintActive(bool bActive)
{
	CommitClothPaintStroke();

	if (bClothPaintActive == bActive)
	{
		if (bActive)
		{
			bRequestFocusClothingPanel = true;
		}
		return;
	}

	bClothPaintActive = bActive;
	bBrushHitValid = false;
	BrushAffectedVertWorldPositions.Empty();

	if (bClothPaintActive)
	{
		bRequestFocusClothingPanel = true;
		BoneGizmo.EndDrag();
		BoneGizmo.SetTarget(nullptr);

		if (!SelectedClothingAsset && Mesh && !Mesh->GetClothingAssets().IsEmpty())
		{
			SelectedClothingAsset = Mesh->GetClothingAssets()[0];
		}

		if (MeshComponent)
		{
			MeshComponent->SetClothSimulationSuspended(true);
			MeshComponent->SetDebugWeightBone(-2);
		}
		RefreshClothPaintOverlay();
	}
	else
	{
		if (MeshComponent)
		{
			if (FSkeletalMeshObject* MeshObj = MeshComponent->GetMeshObject())
			{
				MeshObj->SetClothPaintOverlay(nullptr);
			}
			MeshComponent->SetDebugWeightBone(-1);
			RefreshHeatmap();
		}

		// 언리얼 방식(안 B): 클로스 페인트 비활성화 시 페인트 값을 즉시 리빌드해 프리뷰 시뮬레이션 시작
		if (Mesh)
		{
			for (UClothingAssetCommon* Asset : Mesh->GetClothingAssets())
			{
				if (Asset)
				{
					Asset->RebuildFromPaintedWeights(*Mesh);
				}
			}
			Mesh->RebuildClothingMappingBuffer();
		}

		if (MeshComponent)
		{
			MeshComponent->SetClothSimulationSuspended(false);
			MeshComponent->CreateClothingSimulation();
		}

		if (SelectedClothingAsset && SelectedClothingAsset->IsValid())
		{
			const auto& Phys = SelectedClothingAsset->GetPhysicalMeshData();
			ClothStatusMessage = "Previewing Cloth (" + std::to_string(Phys.Vertices.Num()) + " sim verts, "
				+ std::to_string(Phys.Indices.Num() / 3) + " tris)";
		}
	}
}

void FSkeletalMeshEditor::RefreshClothPaintOverlay()
{
	if (!Mesh || !MeshComponent)
		return;

	FSkeletalMeshObject* MeshObj = MeshComponent->GetMeshObject();
	if (!MeshObj)
		return;

	if (!bClothPaintActive)
	{
		MeshObj->SetClothPaintOverlay(nullptr);
		return;
	}

	const auto& LODs = Mesh->GetRenderData().LODRenderData;
	if (LODs.IsEmpty())
		return;

	const int32 NumVerts = LODs[0].BindVertices.Num();
	TArray<FClothVertMapping> Overlay;
	Overlay.SetNum(NumVerts);
	for (int32 v = 0; v < NumVerts; ++v)
	{
		Overlay[v].SimIndex = 0u;
		Overlay[v].BlendWeight = 0.0f;
	}

	if (SelectedClothingAsset)
	{
		TArray<uint8> SectionMask;
		GatherSectionVertexMask(SelectedClothingAsset->GetSourceSectionIndex(), SectionMask);

		TArray<float>& Painted = SelectedClothingAsset->GetPaintedMaxDistances();
		if (Painted.Num() != NumVerts)
		{
			Painted.SetNum(NumVerts);
		}

		const float ViewMin = ClothPaintSettings.ViewMin;
		const float ViewRange = std::max(0.001f, ClothPaintSettings.ViewMax - ViewMin);

		for (int32 v = 0; v < NumVerts; ++v)
		{
			if (SectionMask[v] == 0)
				continue;

			Overlay[v].SimIndex = 1u; // 섹션 내부 정점
			const float Val = Painted[v];
			if (Val <= 0.0f)
			{
				Overlay[v].BlendWeight = 0.0f; // 0 = 분홍(고정)
			}
			else
			{
				const float Norm = FMath::Clamp((Val - ViewMin) / ViewRange, 0.0f, 1.0f);
				Overlay[v].BlendWeight = 0.001f + 0.999f * Norm; // >0 = 검정~흰색
			}
		}
	}

	MeshObj->SetClothPaintOverlay(&Overlay);
}

void FSkeletalMeshEditor::ApplyClothConfigLive()
{
	bClothingDirty = true;
	if (SelectedClothingAsset && MeshComponent && MeshComponent->GetClothingSimulation())
	{
		MeshComponent->GetClothingSimulation()->ApplyConfig(SelectedClothingAsset->GetConfig());
	}
}

void FSkeletalMeshEditor::QuickFillClothFromSelectedBone()
{
	if (!Mesh || !SelectedClothingAsset)
	{
		ClothStatusMessage = "Select or create a clothing asset first.";
		return;
	}
	if (SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		ClothStatusMessage = "Select a bone in the Bone Hierarchy first.";
		return;
	}

	TArray<float> Generated;
	TArray<int32> Roots;
	Roots.Add(SelectedBoneIndex);
	UClothingAssetCommon::GenerateMaxDistancesFromBones(*Mesh, Roots, std::max(0.0f, ClothPaintSettings.PaintValue), Generated);

	TArray<uint8> SectionMask;
	GatherSectionVertexMask(SelectedClothingAsset->GetSourceSectionIndex(), SectionMask);

	TArray<float>& Painted = SelectedClothingAsset->GetPaintedMaxDistances();
	const int32 NumVerts = FMath::Min(Generated.Num(), SectionMask.Num());
	if (Painted.Num() != NumVerts)
	{
		Painted.SetNum(NumVerts);
	}

	TArray<float> BeforeWeights = Painted;
	int32 FilledCount = 0;
	for (int32 v = 0; v < NumVerts; ++v)
	{
		if (SectionMask[v] && Generated[v] > 0.0f)
		{
			const float NextVal = std::max(Painted[v], Generated[v]);
			if (std::fabs(NextVal - Painted[v]) > 1e-5f)
			{
				Painted[v] = NextVal;
				++FilledCount;
			}
		}
	}

	if (FilledCount > 0)
	{
		PushClothPaintUndo(SelectedClothingAsset, std::move(BeforeWeights));
		bClothingDirty = true;
	}
	ClothStatusMessage = "Filled " + std::to_string(FilledCount) + " verts from bone '" + BoneItems[SelectedBoneIndex].Name + "'";

	if (bClothPaintActive)
	{
		RefreshClothPaintOverlay();
	}
	else
	{
		SelectedClothingAsset->RebuildFromPaintedWeights(*Mesh);
		Mesh->RebuildClothingMappingBuffer();
		if (MeshComponent)
		{
			MeshComponent->CreateClothingSimulation();
		}
	}
}

void FSkeletalMeshEditor::ClearSelectedClothingWeights()
{
	if (!Mesh || !SelectedClothingAsset)
		return;

	TArray<float>& Painted = SelectedClothingAsset->GetPaintedMaxDistances();
	TArray<float> BeforeWeights = Painted;
	bool bAnyChanged = false;
	for (int32 v = 0; v < Painted.Num(); ++v)
	{
		if (Painted[v] != 0.0f)
		{
			Painted[v] = 0.0f;
			bAnyChanged = true;
		}
	}

	if (bAnyChanged)
	{
		PushClothPaintUndo(SelectedClothingAsset, std::move(BeforeWeights));
		bClothingDirty = true;
	}
	ClothStatusMessage = "Cleared all painted weights to 0.";

	if (bClothPaintActive)
	{
		RefreshClothPaintOverlay();
	}
	else
	{
		SelectedClothingAsset->RebuildFromPaintedWeights(*Mesh);
		Mesh->RebuildClothingMappingBuffer();
		if (MeshComponent)
		{
			MeshComponent->CreateClothingSimulation();
		}
	}
}

bool FSkeletalMeshEditor::SaveClothingAssets()
{
	if (!Mesh)
		return false;

	for (UClothingAssetCommon* Asset : Mesh->GetClothingAssets())
	{
		if (Asset)
		{
			Asset->RebuildFromPaintedWeights(*Mesh);
		}
	}
	Mesh->RebuildClothingMappingBuffer();

	FString ResolvedPath = FAssetUtils::ResolveAssetFilePath(GetAssetPath());
	if (ResolvedPath.empty())
	{
		ResolvedPath = GetAssetPath();
	}

	FString Error;
	if (!Mesh->SaveClothingAssets(ResolvedPath, Error))
	{
		ClothStatusMessage = "Save failed: " + Error;
		return false;
	}

	bClothingDirty = false;
	USkeletalMeshComponent::RefreshClothingOnAllComponents(Mesh);

	// 페인트 모드가 켜져 있다면 방금 리프레시된 프리뷰 컴포넌트의 오버레이/일시정지 상태를 유지한다
	if (bClothPaintActive && MeshComponent)
	{
		MeshComponent->SetClothSimulationSuspended(true);
		MeshComponent->SetDebugWeightBone(-2);
		RefreshClothPaintOverlay();
	}

	ClothStatusMessage = "Saved: " + ClothingAssetFile::GetClothBinaryPath(ResolvedPath);
	return true;
}

void FSkeletalMeshEditor::UpdateClothPainting(float DeltaTime)
{
	bBrushHitValid = false;
	BrushAffectedVertWorldPositions.Empty();

	// 드래그 도중 뷰포트 밖으로 나갔다가 마우스를 뗀 경우에도 스트로크를 안전하게 커밋한다
	if (!PointerInput.bMouseDown && bClothStrokeActive)
	{
		CommitClothPaintStroke();
	}

	if (!ViewportClient || !bViewportVisible ||
		ViewportClient->GetWidth() != ViewportWidth ||
		ViewportClient->GetHeight() != ViewportHeight ||
		!PointerInput.bHovered || CameraInput.bCaptured ||
		!Mesh || !MeshComponent || !SelectedClothingAsset)
	{
		return;
	}

	FVector LocalHitPos;
	FVector LocalHitNormal;
	int32 HitSection = -1;
	if (!RaycastPreviewMesh(PointerInput.MouseUV, LocalHitPos, LocalHitNormal, HitSection))
	{
		return;
	}

	const FMatrix WorldMatrix = MeshComponent->GetWorldMatrix();
	BrushHitWorldPos = WorldMatrix.TransformPosition(LocalHitPos);
	BrushHitWorldNormal = WorldMatrix.TransformVector(LocalHitNormal).GetSafeNormal(SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
	BrushWorldScale = FMath::Max(WorldMatrix.TransformVector(FVector(1.0f, 0.0f, 0.0f)).Length(), 0.0001f);
	bBrushHitValid = true;

	const auto& LODs = Mesh->GetRenderData().LODRenderData;
	if (LODs.IsEmpty())
		return;

	const FSkeletalMeshLODRenderData& LOD = LODs[0];
	const TArray<FVertex>* SkinnedVerts = nullptr;
	if (const FSkeletalMeshObject* MeshObj = MeshComponent->GetMeshObject())
	{
		SkinnedVerts = MeshObj->GetCPUSkinnedVertices();
	}
	if (!SkinnedVerts || SkinnedVerts->IsEmpty())
	{
		SkinnedVerts = &LOD.BindVertices;
	}

	const int32 NumVerts = SkinnedVerts->Num();
	TArray<uint8> SectionMask;
	GatherSectionVertexMask(SelectedClothingAsset->GetSourceSectionIndex(), SectionMask);

	const float Radius = std::max(0.01f, ClothPaintSettings.BrushRadius);
	const float RadiusSq = Radius * Radius;

	struct FAffectedVert
	{
		int32 Index;
		float Dist;
	};
	TArray<FAffectedVert> Affected;

	for (int32 v = 0; v < NumVerts; ++v)
	{
		if (SectionMask[v] == 0)
			continue;

		const FVector& Pos = (*SkinnedVerts)[v].Position;
		const float DistSq = (Pos - LocalHitPos).SizeSquared();
		if (DistSq > RadiusSq)
			continue;

		if (ClothPaintSettings.bIgnoreBackfacing)
		{
			if ((*SkinnedVerts)[v].Normal.Dot(LocalHitNormal) < 0.0f)
				continue;
		}

		Affected.Add(FAffectedVert{ v, std::sqrt(DistSq) });
		BrushAffectedVertWorldPositions.Add(WorldMatrix.TransformPosition(Pos));
	}

	if (!PointerInput.bMouseDown)
		return;

	TArray<float>& Painted = SelectedClothingAsset->GetPaintedMaxDistances();
	if (Painted.Num() != NumVerts)
	{
		Painted.SetNum(NumVerts);
	}

	if (!bClothStrokeActive)
	{
		ClothStrokeBefore.Asset = SelectedClothingAsset;
		ClothStrokeBefore.Weights = Painted;
		bClothStrokeActive = true;
		bClothStrokeModified = false;
	}

	const float TargetVal = PointerInput.bShiftDown ? 0.0f : std::max(0.0f, ClothPaintSettings.PaintValue);
	const float Strength = FMath::Clamp(ClothPaintSettings.BrushStrength, 0.01f, 1.0f);
	const float Falloff = FMath::Clamp(ClothPaintSettings.BrushFalloff, 0.0f, 1.0f);
	const float InnerR = Radius * (1.0f - Falloff);
	const float Rate = FMath::Clamp(Strength * std::max(DeltaTime * 12.0f, 0.15f), 0.01f, 1.0f);

	bool bModified = false;

	switch (ClothPaintSettings.Tool)
	{
	case EClothPaintTool::Brush:
	{
		for (const FAffectedVert& AV : Affected)
		{
			float FalloffWeight = 1.0f;
			if (AV.Dist > InnerR && Radius > InnerR)
			{
				const float t = FMath::Clamp((AV.Dist - InnerR) / (Radius - InnerR), 0.0f, 1.0f);
				FalloffWeight = 1.0f - (t * t * (3.0f - 2.0f * t));
			}
			const float Alpha = FMath::Clamp(Rate * FalloffWeight, 0.0f, 1.0f);
			float NewVal = Painted[AV.Index] + (TargetVal - Painted[AV.Index]) * Alpha;
			if (TargetVal <= 0.0f && NewVal < 0.05f)
			{
				NewVal = 0.0f;
			}
			const float ClampedVal = std::max(0.0f, NewVal);
			if (std::fabs(ClampedVal - Painted[AV.Index]) > 1e-5f)
			{
				Painted[AV.Index] = ClampedVal;
				bModified = true;
			}
		}
		break;
	}
	case EClothPaintTool::Fill:
	{
		if (PointerInput.bMousePressed)
		{
			for (int32 v = 0; v < NumVerts; ++v)
			{
				if (SectionMask[v] && std::fabs(TargetVal - Painted[v]) > 1e-5f)
				{
					Painted[v] = TargetVal;
					bModified = true;
				}
			}
		}
		break;
	}
	case EClothPaintTool::Smooth:
	{
		if (!Affected.IsEmpty())
		{
			float Sum = 0.0f;
			for (const FAffectedVert& AV : Affected)
			{
				Sum += Painted[AV.Index];
			}
			const float Avg = Sum / static_cast<float>(Affected.Num());

			for (const FAffectedVert& AV : Affected)
			{
				float FalloffWeight = 1.0f;
				if (AV.Dist > InnerR && Radius > InnerR)
				{
					const float t = FMath::Clamp((AV.Dist - InnerR) / (Radius - InnerR), 0.0f, 1.0f);
					FalloffWeight = 1.0f - (t * t * (3.0f - 2.0f * t));
				}
				const float Alpha = FMath::Clamp(Rate * FalloffWeight, 0.0f, 1.0f);
				float NewVal = Painted[AV.Index] + (Avg - Painted[AV.Index]) * Alpha;
				if (NewVal < 0.05f)
				{
					NewVal = 0.0f;
				}
				const float ClampedVal = std::max(0.0f, NewVal);
				if (std::fabs(ClampedVal - Painted[AV.Index]) > 1e-5f)
				{
					Painted[AV.Index] = ClampedVal;
					bModified = true;
				}
			}
		}
		break;
	}
	}

	if (bModified)
	{
		bClothStrokeModified = true;
		bClothingDirty = true;
		RefreshClothPaintOverlay();
	}
}

void FSkeletalMeshEditor::PushClothPaintUndo(UClothingAssetCommon* Asset, TArray<float>&& BeforeWeights)
{
	if (!Asset)
		return;

	FClothPaintUndoEntry Entry;
	Entry.Asset = Asset;
	Entry.Weights = std::move(BeforeWeights);
	ClothUndoStack.Add(std::move(Entry));
	if (ClothUndoStack.Num() > MaxUndoSteps)
	{
		ClothUndoStack.RemoveAt(0);
	}
	ClothRedoStack.Empty();
}

void FSkeletalMeshEditor::CommitClothPaintStroke()
{
	if (!bClothStrokeActive)
		return;

	if (bClothStrokeModified && ClothStrokeBefore.Asset)
	{
		PushClothPaintUndo(ClothStrokeBefore.Asset, std::move(ClothStrokeBefore.Weights));
	}
	bClothStrokeActive = false;
	bClothStrokeModified = false;
	ClothStrokeBefore = {};
}

void FSkeletalMeshEditor::ClearClothPaintHistoryForAsset(UClothingAssetCommon* Asset)
{
	if (!Asset)
		return;

	if (ClothStrokeBefore.Asset == Asset)
	{
		bClothStrokeActive = false;
		bClothStrokeModified = false;
		ClothStrokeBefore = {};
	}

	for (int32 i = ClothUndoStack.Num() - 1; i >= 0; --i)
	{
		if (ClothUndoStack[i].Asset == Asset)
			ClothUndoStack.RemoveAt(i);
	}
	for (int32 i = ClothRedoStack.Num() - 1; i >= 0; --i)
	{
		if (ClothRedoStack[i].Asset == Asset)
			ClothRedoStack.RemoveAt(i);
	}
}

void FSkeletalMeshEditor::UndoClothPaint()
{
	CommitClothPaintStroke();
	if (!Mesh || ClothUndoStack.IsEmpty())
		return;

	FClothPaintUndoEntry Entry = std::move(ClothUndoStack[ClothUndoStack.Num() - 1]);
	ClothUndoStack.RemoveAt(ClothUndoStack.Num() - 1);

	const auto& Assets = Mesh->GetClothingAssets();
	bool bValid = false;
	for (UClothingAssetCommon* A : Assets)
	{
		if (A == Entry.Asset)
		{
			bValid = true;
			break;
		}
	}
	if (!bValid || !Entry.Asset)
		return;

	FClothPaintUndoEntry RedoEntry;
	RedoEntry.Asset = Entry.Asset;
	RedoEntry.Weights = Entry.Asset->GetPaintedMaxDistances();
	ClothRedoStack.Add(std::move(RedoEntry));

	Entry.Asset->GetPaintedMaxDistances() = std::move(Entry.Weights);
	SelectedClothingAsset = Entry.Asset;
	bClothingDirty = true;
	ClothStatusMessage = "Undo Cloth Paint";

	if (bClothPaintActive)
	{
		RefreshClothPaintOverlay();
	}
	else
	{
		Entry.Asset->RebuildFromPaintedWeights(*Mesh);
		Mesh->RebuildClothingMappingBuffer();
		if (MeshComponent)
		{
			MeshComponent->CreateClothingSimulation();
		}
	}
}

void FSkeletalMeshEditor::RedoClothPaint()
{
	CommitClothPaintStroke();
	if (!Mesh || ClothRedoStack.IsEmpty())
		return;

	FClothPaintUndoEntry Entry = std::move(ClothRedoStack[ClothRedoStack.Num() - 1]);
	ClothRedoStack.RemoveAt(ClothRedoStack.Num() - 1);

	const auto& Assets = Mesh->GetClothingAssets();
	bool bValid = false;
	for (UClothingAssetCommon* A : Assets)
	{
		if (A == Entry.Asset)
		{
			bValid = true;
			break;
		}
	}
	if (!bValid || !Entry.Asset)
		return;

	FClothPaintUndoEntry UndoEntry;
	UndoEntry.Asset = Entry.Asset;
	UndoEntry.Weights = Entry.Asset->GetPaintedMaxDistances();
	ClothUndoStack.Add(std::move(UndoEntry));

	Entry.Asset->GetPaintedMaxDistances() = std::move(Entry.Weights);
	SelectedClothingAsset = Entry.Asset;
	bClothingDirty = true;
	ClothStatusMessage = "Redo Cloth Paint";

	if (bClothPaintActive)
	{
		RefreshClothPaintOverlay();
	}
	else
	{
		Entry.Asset->RebuildFromPaintedWeights(*Mesh);
		Mesh->RebuildClothingMappingBuffer();
		if (MeshComponent)
		{
			MeshComponent->CreateClothingSimulation();
		}
	}
}

void FSkeletalMeshEditor::DrawClothBrushOverlay(FLineBatcher& LineBatcher) const
{
	const FVector N = BrushHitWorldNormal.GetSafeNormal(SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
	const FVector RefUp = (std::fabs(N.Z) < 0.9f) ? FVector(0.0f, 0.0f, 1.0f) : FVector(1.0f, 0.0f, 0.0f);
	const FVector Tangent = FVector::Cross(RefUp, N).GetSafeNormal(SMALL_NUMBER, FVector(1.0f, 0.0f, 0.0f));
	const FVector Bitangent = FVector::Cross(N, Tangent).GetSafeNormal(SMALL_NUMBER, FVector(0.0f, 1.0f, 0.0f));

	const float OuterRadius = std::max(0.01f, ClothPaintSettings.BrushRadius) * BrushWorldScale;
	const float InnerRadius = OuterRadius * (1.0f - FMath::Clamp(ClothPaintSettings.BrushFalloff, 0.0f, 1.0f));
	const FVector Center = BrushHitWorldPos + N * (OuterRadius * 0.015f);

	const FVector4 OuterColor(0.25f, 0.95f, 0.30f, 1.0f);
	const FVector4 InnerColor(0.15f, 0.70f, 0.20f, 1.0f);
	const FVector4 DotColor(0.15f, 1.0f, 0.25f, 1.0f);

	constexpr int32 Segments = 48;
	constexpr float TwoPi = 6.28318530718f;

	auto DrawCircle = [&](float R, const FVector4& Color)
	{
		if (R <= 0.0001f)
			return;

		FVector Prev = Center + Tangent * R;
		for (int32 i = 1; i <= Segments; ++i)
		{
			const float Angle = TwoPi * (static_cast<float>(i) / static_cast<float>(Segments));
			const FVector Curr = Center + (Tangent * std::cos(Angle) + Bitangent * std::sin(Angle)) * R;
			LineBatcher.AddLine(Prev, Curr, Color);
			Prev = Curr;
		}
	};

	DrawCircle(OuterRadius, OuterColor);
	if (InnerRadius > OuterRadius * 0.02f && InnerRadius < OuterRadius * 0.99f)
	{
		DrawCircle(InnerRadius, InnerColor);
	}

	// 중심 법선 틱
	LineBatcher.AddLine(Center, Center + N * (OuterRadius * 0.18f), OuterColor);

	// 브러시 반경 내 영향 정점들을 초록색 십자 점으로 표시 (언리얼 Image 6)
	const float DotHalf = FMath::Max(BoneSphereSize * 0.45f, OuterRadius * 0.018f);
	for (const FVector& P : BrushAffectedVertWorldPositions)
	{
		const FVector OffsetP = P + N * (DotHalf * 0.4f);
		LineBatcher.AddLine(OffsetP - Tangent * DotHalf, OffsetP + Tangent * DotHalf, DotColor);
		LineBatcher.AddLine(OffsetP - Bitangent * DotHalf, OffsetP + Bitangent * DotHalf, DotColor);
	}
}
