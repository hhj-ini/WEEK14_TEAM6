#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Viewports/EditorViewportClient.h"

class UPhysicsAsset;
class UClothingAssetCommon;

enum class EClothPaintTool : uint8
{
	Brush = 0,
	Fill = 1,
	Smooth = 2
};

struct FClothPaintSettings
{
	EClothPaintTool Tool = EClothPaintTool::Brush;
	float ViewMin = 0.0f;
	float ViewMax = 100.0f;
	float PaintValue = 25.0f;
	float BrushRadius = 15.0f;
	float BrushStrength = 0.5f;
	float BrushFalloff = 0.5f;
	bool bIgnoreBackfacing = false;
};

class FSkeletalMeshEditor : public FSkeletalPreviewEditor
{
public:
	FSkeletalMeshEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
		const FString& InAssetPath, USkeletalMesh& InMesh)
		: FSkeletalPreviewEditor(InEditorEngine, InEditorManager, InAssetPath)
	{
		Mesh = &InMesh;
	}

	const char* GetEditorTypeName() const override { return "SkeletalMesh"; }
	bool UsesPreviewSkybox() const override { return true; }
	const FGizmo* GetPreviewGizmo() const override { return BoneGizmo.GetTarget() ? &BoneGizmo : nullptr; }

	// 선택한 본의 프리뷰 포즈를 부모 기준 로컬 변환으로 조회·수정합니다.
	bool GetSelectedBoneLocalTransform(FTransform& OutTransform) const;
	void SetSelectedBoneLocalTransform(const FTransform& Transform);
	void ResetPose();
	void ResetBone(int32 BoneIndex);

	void CommitBonePoseUndo(bool bInteractionActive);
	void UndoPoseEdit();
	void RedoPoseEdit();
	bool CanUndoPoseEdit() const { return !PoseUndoStack.IsEmpty(); }
	bool CanRedoPoseEdit() const { return !PoseRedoStack.IsEmpty(); }

	void SetGizmoMode(EGizmoMode Mode);
	void SetGizmoSpace(EGizmoSpace Space);
	EGizmoMode  GetGizmoMode() const { return BoneGizmo.GetMode(); }
	EGizmoSpace GetGizmoSpace() const { return BoneGizmo.GetSpace(); }
	bool IsGizmoDragging() const { return BoneGizmo.IsUsing(); }

	// 에셋 브라우저에서 선택한 애니메이션 시퀀스 에디터를 연다.
	void OpenAnimationAsset(const FString& InAssetPath);

	// Physics Asset 생성
	void GeneratePhysicsAsset();
	UPhysicsAsset* GetPhysicsAsset() const { return PhysicsAsset; }

	void LoadPhysicsAsset();
	// .phys를 Physics Asset 에디터로 연다. 아직 없으면 먼저 생성해 저장한다
	void OpenPhysicsAssetEditor();

	bool bShowPhysicsBodies = true;

	// ---------- Clothing & Cloth Paint (언리얼 SkeletalMeshEditor 워크플로우) ----------
	void OnRenderLine(FLineBatcher& LineBatcher) override;
	bool PickMeshSection(const FVector2& MouseUV, int32& OutSectionIndex) const;

	UClothingAssetCommon* CreateClothingFromSection(int32 SectionIndex, const FString& Name);
	void ApplyClothingToSection(int32 SectionIndex, UClothingAssetCommon* Asset);
	void RemoveClothingFromSection(int32 SectionIndex);
	void DeleteClothingAsset(UClothingAssetCommon* Asset);

	void SelectClothingAsset(UClothingAssetCommon* Asset);
	UClothingAssetCommon* GetSelectedClothingAsset() const { return SelectedClothingAsset; }

	void SetClothPaintActive(bool bActive);
	bool IsClothPaintActive() const { return bClothPaintActive; }
	FClothPaintSettings& GetClothPaintSettings() { return ClothPaintSettings; }
	const FClothPaintSettings& GetClothPaintSettings() const { return ClothPaintSettings; }
	void RefreshClothPaintOverlay();

	void ApplyClothConfigLive();
	void QuickFillClothFromSelectedBone();
	void ClearSelectedClothingWeights();
	bool SaveClothingAssets();
	bool IsClothingDirty() const { return bClothingDirty; }
	const FString& GetClothStatusMessage() const { return ClothStatusMessage; }

	void UndoClothPaint();
	void RedoClothPaint();
	bool CanUndoClothPaint() const { return !ClothUndoStack.IsEmpty(); }
	bool CanRedoClothPaint() const { return !ClothRedoStack.IsEmpty(); }

	// Clothing 패널 자동 포커스 요청 소비 (클로스 페인트 활성화 시)
	bool ConsumeFocusClothingPanelRequest()
	{
		const bool bVal = bRequestFocusClothingPanel;
		bRequestFocusClothingPanel = false;
		return bVal;
	}

protected:
	// 이 에디터에 연결된 탭을 생성합니다.
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;     // InitializePreview() 후 BoneGizmoTarget 생성
	void ShutdownResources() override;       // 기즈모 정리 후 Super

	void TickAfterWorld(float DeltaTime) override;
	bool IsViewportInteractionBusy() const override { return BoneGizmo.IsUsing() || (bClothPaintActive && PointerInput.bMouseDown); }
	void OnBoneSelected() override;
	void CancelViewportInteraction() override { BoneGizmo.EndDrag(); }

private:
	void DrawPhysicsBodies(FLineBatcher& LineBatcher) const;
	void UpdateClothPainting(float DeltaTime);
	void DrawClothBrushOverlay(FLineBatcher& LineBatcher) const;
	bool RaycastPreviewMesh(const FVector2& MouseUV, FVector& OutLocalHitPos, FVector& OutLocalHitNormal, int32& OutSectionIndex) const;
	void GatherSectionVertexMask(int32 SectionIndex, TArray<uint8>& OutMask) const;
	void PushClothPaintUndo(UClothingAssetCommon* Asset, TArray<float>&& BeforeWeights);
	void CommitClothPaintStroke();
	void ClearClothPaintHistoryForAsset(UClothingAssetCommon* Asset);

	void SyncBoneGizmo();
	bool UpdateBoneGizmo();
	void ApplyBoneGizmoTransform();
	FQuat GetBoneWorldRotation(int32 BoneIndex) const;
	TArray<FTransform> CaptureBonePose() const;
	void ApplyBonePose(const TArray<FTransform>& Pose);
	void ResetBonePoseHistory();

	USceneComponent* BoneGizmoTarget = nullptr;
	FGizmo BoneGizmo;
	FTransform DragStartBoneTransform;
	FMatrix DragParentInverse;
	FQuat DragParentRotation;

	TArray<TArray<FTransform>> PoseUndoStack;
	TArray<TArray<FTransform>> PoseRedoStack;
	TArray<FTransform> CommittedBonePose;
	bool bPoseUndoPending = false;
	static constexpr int32 MaxUndoSteps = 100;

	UPhysicsAsset* PhysicsAsset = nullptr;

	// Cloth Paint 편집 상태
	struct FClothPaintUndoEntry
	{
		UClothingAssetCommon* Asset = nullptr;
		TArray<float> Weights;
	};

	bool bClothPaintActive = false;
	bool bClothingDirty = false;
	bool bRequestFocusClothingPanel = false;
	UClothingAssetCommon* SelectedClothingAsset = nullptr;
	FClothPaintSettings ClothPaintSettings;
	FString ClothStatusMessage;

	TArray<FClothPaintUndoEntry> ClothUndoStack;
	TArray<FClothPaintUndoEntry> ClothRedoStack;
	bool bClothStrokeActive = false;
	bool bClothStrokeModified = false;
	FClothPaintUndoEntry ClothStrokeBefore;

	bool bBrushHitValid = false;
	FVector BrushHitWorldPos = FVector::ZeroVector;
	FVector BrushHitWorldNormal = FVector(0.0f, 0.0f, 1.0f);
	float BrushWorldScale = 1.0f;
	TArray<FVector> BrushAffectedVertWorldPositions;
};
