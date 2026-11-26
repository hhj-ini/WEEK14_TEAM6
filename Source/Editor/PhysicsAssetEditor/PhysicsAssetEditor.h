#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Physics/BodySetup.h"

class UPhysicsAsset;
class UPhysicsConstraintTemplate;
class FPhysicsAssetDebugRenderer;
struct FConstraintInstance;
struct FBodyInstance;
struct FRay;

namespace physx
{
	class PxRigidDynamic;
	class PxD6Joint;
}

// 에디터에서 지금 고른 것. 트리·그래프·뷰포트·디테일이 모두 이 값 하나를 읽고 쓴다.
enum class EPhysicsAssetSelectionType : uint8
{
	None,
	Body,         // UPhysicsAsset::BodySetup[Index]
	Constraint,   // UPhysicsAsset::ConstraintSetup[Index]
};

struct FPhysicsAssetSelection
{
	EPhysicsAssetSelectionType Type = EPhysicsAssetSelectionType::None;
	int32 Index = -1;
	int32 PrimitiveIndex = -1;   // 바디 안의 형상. -1이면 바디 전체

	bool IsBody() const { return Type == EPhysicsAssetSelectionType::Body; }
	bool IsConstraint() const { return Type == EPhysicsAssetSelectionType::Constraint; }
	bool IsBody(int32 InIndex) const { return IsBody() && Index == InIndex; }
	bool IsConstraint(int32 InIndex) const { return IsConstraint() && Index == InIndex; }
	bool IsSameItem(const FPhysicsAssetSelection& Other) const { return Type == Other.Type && Index == Other.Index; }
};

// 선택을 바꾸는 방식. Add는 이미 있으면 맨 뒤(주 선택)로 옮기고, Toggle은 있으면 뺀다
enum class EPhysicsAssetSelectMode : uint8
{
	Replace,
	Add,
	Toggle,
};

enum class EPhysicsAssetPrimitive : uint8
{
	Sphere,
	Box,
	Capsule,
};

// 뷰포트 표시 메뉴(눈 아이콘)의 Physics Rendering 항목. 편집 중과 시뮬레이션 중 값을 따로 둔다
enum class EPhysicsMeshDrawMode : uint8 { Solid, Wireframe, None };
enum class EPhysicsBodyDrawMode : uint8 { Solid, Wireframe, SolidWireframe, None };
enum class EPhysicsConstraintDrawMode : uint8 { None, AllPositions, AllLimits };   // 위치: 프레임 축만, 한계: 원뿔·부채꼴까지
enum class EPhysicsCoMDrawMode : uint8 { All, Selected, None };

struct FPhysicsAssetRenderSettings
{
	// 질량 중심
	bool bShowMassValue = false;
	bool bHideKinematicCoM = false;
	float CoMMarkerScale = 1.0f;
	EPhysicsCoMDrawMode CoMEdit = EPhysicsCoMDrawMode::None;
	EPhysicsCoMDrawMode CoMSimulation = EPhysicsCoMDrawMode::None;

	// 메시
	EPhysicsMeshDrawMode MeshEdit = EPhysicsMeshDrawMode::Solid;
	EPhysicsMeshDrawMode MeshSimulation = EPhysicsMeshDrawMode::Solid;

	// 바디
	bool bSelectedSolidOnly = false;       // 고르지 않은 바디는 와이어프레임으로만
	bool bHideSimulatedBodies = false;
	bool bHideKinematicBodies = false;
	float CollisionOpacity = 0.55f;
	EPhysicsBodyDrawMode BodyEdit = EPhysicsBodyDrawMode::Solid;
	EPhysicsBodyDrawMode BodySimulation = EPhysicsBodyDrawMode::Solid;

	// 조인트
	bool bConstraintsAsPoints = false;
	bool bSelectedConstraintsOnly = false;
	float ConstraintScale = 1.0f;
	EPhysicsConstraintDrawMode ConstraintEdit = EPhysicsConstraintDrawMode::AllLimits;
	EPhysicsConstraintDrawMode ConstraintSimulation = EPhysicsConstraintDrawMode::None;
};

// .phys 편집기. 스켈레탈 메시 프리뷰 위에 PhysicsAsset의 바디와 조인트를 보여 주고 편집한다.
class FPhysicsAssetEditor : public FSkeletalPreviewEditor
{
public:
	FPhysicsAssetEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
		const FString& InAssetPath, USkeletalMesh& InMesh, UPhysicsAsset& InPhysicsAsset);
	~FPhysicsAssetEditor() override;

	const char* GetEditorTypeName() const override { return "PhysicsAsset"; }
	const FGizmo* GetPreviewGizmo() const override { return PhysicsGizmo.GetTarget() ? &PhysicsGizmo : nullptr; }

	// 뷰포트 표시. 값을 바꾼 뒤 메시 모드가 바뀌었으면 ApplyMeshDrawMode를 부른다
	FPhysicsAssetRenderSettings RenderSettings;
	EPhysicsMeshDrawMode GetMeshDrawMode() const { return bSimulating ? RenderSettings.MeshSimulation : RenderSettings.MeshEdit; }
	EPhysicsBodyDrawMode GetBodyDrawMode() const { return bSimulating ? RenderSettings.BodySimulation : RenderSettings.BodyEdit; }
	EPhysicsConstraintDrawMode GetConstraintDrawMode() const { return bSimulating ? RenderSettings.ConstraintSimulation : RenderSettings.ConstraintEdit; }
	EPhysicsCoMDrawMode GetCoMDrawMode() const { return bSimulating ? RenderSettings.CoMSimulation : RenderSettings.CoMEdit; }
	void ApplyMeshDrawMode();
	// 조인트 표시 크기 (원뿔 길이). 선택된 조인트는 2.5배로 키운다
	float GetConstraintDrawLength(bool bSelected) const;

	// 질량 중심 표시에 쓰는 바디 질량. 오버라이드가 없으면 형상 부피 × 물리 머티리얼 밀도
	float GetBodyMass(int32 BodyIndex) const;
	// 뷰포트 위에 글자(질량 값)를 그린다. Origin·Size는 뷰포트 이미지의 화면 사각형
	void DrawViewportLabels(struct ImDrawList* DrawList, const struct ImVec2& Origin, const struct ImVec2& Size) const;

	// 기즈모: 바디를 고르면 형상, 조인트를 고르면 조인트 프레임을 옮긴다
	void SetGizmoMode(EGizmoMode Mode) { PhysicsGizmo.SetMode(Mode); }
	EGizmoMode GetGizmoMode() const { return PhysicsGizmo.GetMode(); }
	bool UsesPreviewSkybox() const override { return true; }
	bool IsDirty() const override { return bDirty; }

	UPhysicsAsset* GetPhysicsAsset() const { return PhysicsAsset; }

	// 선택. 여러 개를 고를 수 있고, 마지막에 고른 것이 주 선택이다 (기즈모·본 강조는 주 선택을 따른다)
	const FPhysicsAssetSelection& GetSelection() const { return Selection; }
	const TArray<FPhysicsAssetSelection>& GetSelections() const { return SelectedItems; }
	void SelectBody(int32 BodyIndex, int32 PrimitiveIndex = -1, EPhysicsAssetSelectMode Mode = EPhysicsAssetSelectMode::Replace);
	void SelectConstraint(int32 ConstraintIndex, EPhysicsAssetSelectMode Mode = EPhysicsAssetSelectMode::Replace);
	void SetSelections(const TArray<FPhysicsAssetSelection>& Items);   // 마지막 항목이 주 선택
	void ClearSelection();
	bool IsBodySelected(int32 BodyIndex) const;
	bool IsConstraintSelected(int32 ConstraintIndex) const;
	TArray<int32> GetSelectedBodyIndices() const;
	TArray<int32> GetSelectedConstraintIndices() const;

	UBodySetup* GetSelectedBody() const;
	UPhysicsConstraintTemplate* GetSelectedConstraint() const;

	// 바디·조인트와 본의 대응. 없으면 -1
	int32 GetBodyBoneIndex(int32 BodyIndex) const;
	int32 FindBodyIndexForBone(int32 BoneIndex) const;

	// 편집. 바디·조인트 수가 바뀌면 프리뷰 컴포넌트의 물리 바디를 다시 만든다
	// 본에 형상을 하나 더한다. 바디가 없으면 만들고, 바디가 있는 가장 가까운 조상과 조인트도 만든다
	bool AddPrimitive(int32 BoneIndex, EPhysicsAssetPrimitive Type);
	void DeleteBody(int32 BodyIndex);
	void DeleteBodiesBelow(int32 BodyIndex);   // 이 바디와 자손 본의 바디를 모두 지운다
	void DeleteConstraint(int32 ConstraintIndex);
	void DeleteSelection();   // 고른 바디·조인트를 모두 지운다
	// 두 바디 사이에 조인트를 만든다. 프레임은 자식 본 원점에 맞춘다. 같은 바디이거나 이미 이어져 있으면 false
	bool MakeNewConstraint(int32 ParentBodyIndex, int32 ChildBodyIndex);
	int32 FindConstraintBetween(int32 BodyIndexA, int32 BodyIndexB) const;   // 순서 무관. 없으면 -1

	bool IsBoneDescendantOf(int32 BoneIndex, int32 AncestorBoneIndex) const;   // 같은 본이면 true

	// 조인트 프리셋. 고른 조인트 모두의 Swing·Twist 방식과 한계 각도를 한 번에 바꾼다
	enum class EConstraintPreset : uint8 { BallAndSocket, Hinge, Skeletal };
	void ApplyConstraintPreset(EConstraintPreset Preset);

	// 바디끼리 충돌. Selected는 고른 바디끼리, All은 고른 바디와 나머지 모든 바디 사이
	void SetCollisionBetweenSelected(bool bEnable);
	void SetCollisionBetweenSelectedAndAll(bool bEnable);
	int32 GetSelectedBodyCount() const { return GetSelectedBodyIndices().Num(); }

	// 자동 생성으로 다시 만든다. All은 바디·조인트·충돌 표를 통째로, Selected는 고른 바디의 형상만 바꾼다
	bool RegenerateAllBodies();
	bool RegenerateSelectedBodies();

	// 디테일 창에서 값을 바꾼 뒤 부른다. 프리뷰 랙돌을 새 값으로 다시 만든다
	void NotifyAssetEdited();

	// 시뮬레이션: 켜면 프리뷰 메시가 랙돌로 바닥에 떨어진다. 끄면 기준 포즈로 돌아간다.
	// 켜져 있는 동안 바디·조인트 편집은 막는다
	bool IsSimulating() const { return bSimulating; }
	void ToggleSimulation();   // 마지막에 고른 방식으로 켜고 끈다
	// bSelected: 선택 시뮬레이트. 고른 바디(조인트면 자식 바디)와 그 아래만 움직이고 나머지는 고정한다
	void RunSimulation(bool bSelected);
	bool IsSelectedSimulation() const { return bSelectedSimulation; }
	bool bNoGravitySimulation = false;
	bool IsFloorCollisionEnabled() const { return bFloorCollision; }
	void SetFloorCollisionEnabled(bool bEnabled);

	// 시뮬레이션 중 Ctrl+우클릭 드래그로 바디를 잡아 끈다.
	// 셰이프 없는 키네마틱 핸들 액터를 마우스 광선 위로 옮기고, D6 조인트의 선형 드라이브(스프링)가 잡은 지점을 끌어온다
	bool IsGrabbing() const { return HandleJoint != nullptr; }
	// 잡은 지점과 핸들 사이에 빨간 사각형을 찍는다. Origin·Size는 뷰포트 이미지의 화면 사각형
	void DrawGrabHandle(struct ImDrawList* DrawList, const struct ImVec2& Origin, const struct ImVec2& Size) const;

	// 저장
	void MarkDirty() { bDirty = true; }
	bool Save();

	void OnRenderSceneOverlay(const FSceneView& View) override;        // 바디 (메시와 깊이 비교)
	void OnRenderForegroundOverlay(const FSceneView& View) override;   // 조인트·질량 중심 (항상 위)
	void OnRenderLine(FLineBatcher& LineBatcher) override;            // 본 + 바디 와이어프레임

protected:
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;
	void ShutdownResources() override;
	void OnBoneSelected() override;

	void TickBeforeWorld(float DeltaTime) override;  // 시뮬레이션 중이면 물리 씬을 진행한다
	void TickAfterWorld(float DeltaTime) override;   // 기즈모가 입력을 안 쓰면 바디·조인트 피킹
	bool IsViewportInteractionBusy() const override { return PhysicsGizmo.IsUsing() || IsGrabbing(); }
	void CancelViewportInteraction() override { PhysicsGizmo.EndDrag(); EndGrab(); }

private:
	void CreateConstraintToParentBody(int32 BoneIndex);
	// 배열에 새 조인트를 넣고 프레임을 자식 본 원점에 맞춘다. 물리 재생성은 부르는 쪽이 한다
	int32 CreateConstraint(int32 ChildBoneIndex, int32 ParentBoneIndex);

	// 바디·조인트 배열을 바꾸기 전후에 부른다. 프리뷰 랙돌이 옛 배열 인덱스를 들고 있지 않게
	// 바꾸기 전에 물리 바디를 지우고, 바꾼 뒤 다시 만든다
	void BeginStructureChange();
	void EndStructureChange();

	void StartSimulation();
	void StopSimulation();
	bool EnsureSimulationFloor();   // 메시 발밑에 정적 바닥 바디를 하나 둔다
	void ApplyFloorCollision();
	TArray<int32> GetSelectedSimulationRootBones() const;   // 비어 있으면 전체 시뮬레이트
	void SyncBoneToPrimarySelection();

	// 표시 설정으로 이 바디를 그릴지 (시뮬레이트·키네마틱 바디 숨기기)
	bool ShouldDrawBody(int32 BodyIndex) const;
	bool ShouldDrawConstraint(int32 ConstraintIndex) const;
	// 바디 질량 중심의 월드 위치. 형상 중심을 부피로 가중 평균한다
	bool GetBodyCenterOfMass(int32 BodyIndex, FVector& OutWorld) const;
	bool ShouldDrawCoM(int32 BodyIndex) const;
	bool ProjectToViewportUV(const FVector& World, float& OutU, float& OutV) const;

	FMatrix LastViewProjection;          // 마지막으로 그린 뷰. 글자 위치를 화면으로 옮길 때 쓴다
	EViewportMode SavedViewportMode = EViewportMode::Solid;   // 메시 와이어프레임 전 뷰 모드
	bool bMeshWireframeApplied = false;
	class FPhysScene* GetPreviewPhysScene() const;

	// 선택한 바디에서 기즈모가 붙는 형상 (PrimitiveIndex, 없으면 첫 형상). 순서는 캡슐, 박스, 구
	FKShapeElem* GetGizmoShape(UBodySetup& Body, EPhysicsAssetPrimitive* OutType = nullptr) const;
	// 조인트 프레임의 월드 위치·회전. bParent면 부모 프레임
	void GetConstraintFrameWorld(const FConstraintInstance& CI, bool bParent, FVector& OutLocation, FQuat& OutRotation) const;

	// 뷰포트 입력
	void UpdateShapePicking();
	bool UpdatePhysicsGizmo();
	void SyncPhysicsGizmo();
	void BeginGizmoDrag();
	void ApplyGizmoDrag();
	void RestoreGizmoDragStart();

	// 잡기. 광선에 맞은 시뮬레이트 바디가 없으면 false
	FRay GetMouseRay() const;
	bool TryBeginGrab();
	void UpdateGrab();
	void EndGrab();   // 조인트 → 핸들 액터 순으로 해제한다. 바디를 지우기 전에 반드시 부른다

	physx::PxRigidDynamic* HandleActor = nullptr;
	physx::PxD6Joint* HandleJoint = nullptr;
	FBodyInstance* GrabbedBody = nullptr;
	FVector GrabLocalPos;          // 잡은 지점 (바디 액터 로컬)
	FVector GrabTarget;            // 핸들 위치 (월드)
	float GrabDistance = 0.0f;     // 카메라에서 잡은 지점까지. 드래그하는 동안 유지한다

	TUniquePtr<FPhysicsAssetDebugRenderer> DebugRenderer;

	FGizmo PhysicsGizmo;
	USceneComponent* GizmoTarget = nullptr;

	// 드래그를 시작할 때의 값 (Esc로 되돌리고, 조인트는 이동량만큼 옮긴다)
	FKAggregateGeom DragStartGeom;
	FVector DragStartPos[2];       // 조인트 [0] 자식 프레임, [1] 부모 프레임 (월드)
	FQuat DragStartRot[2];
	FVector DragStartLocalPos[2];  // 조인트 프레임의 저장 값 (Pos1/Pos2 등)
	FVector DragStartPriAxis[2];
	FVector DragStartSecAxis[2];
	FVector DragStartGizmoLocation;
	FQuat DragStartGizmoRotation;
	bool bDragChildFrame = true;   // 조인트 드래그 대상. 기본은 둘 다, Alt는 부모만, Shift+Alt는 자식만
	bool bDragParentFrame = true;

	UPhysicsAsset* PhysicsAsset = nullptr;
	FPhysicsAssetSelection Selection;              // 주 선택 (SelectedItems의 마지막)
	TArray<FPhysicsAssetSelection> SelectedItems;  // 고른 것 전부
	class UBoxComponent* FloorComponent = nullptr;
	bool bSimulating = false;
	bool bSelectedSimulation = false;
	bool bFloorCollision = true;
	FVector SavedGravity = FVector(0.0f, 0.0f, -9.81f);   // 시작할 때 씬 중력. 멈추면 되돌린다
	bool bDirty = false;
	bool bSyncingBoneSelection = false;   // SelectBody → SelectBone → OnBoneSelected 되돌이 방지
};
