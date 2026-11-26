#pragma once

#include "PhysicsAssetEditorPanel.h"
#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"

// 본·바디·조인트 계층. 기본은 바디만 보여 주고, 바디의 부모는 바디가 있는 가장 가까운 조상이다.
// 선택은 에디터의 선택 상태 하나로 맞춘다. Ctrl·Shift로 여러 개를 고른다.
class FPhysicsAssetSkeletonTreePanel : public FPhysicsAssetEditorPanel
{
public:
	explicit FPhysicsAssetSkeletonTreePanel(FPhysicsAssetEditor& InEditor);

protected:
	void DrawContent() override;

private:
	enum class ENodeKind : uint8 { Bone, Body, Constraint };

	struct FTreeNode
	{
		ENodeKind Kind = ENodeKind::Bone;
		int32 Index = -1;          // 본 인덱스, BodySetup 인덱스, ConstraintSetup 인덱스
		int32 BoneIndex = -1;      // 우클릭 메뉴에서 형상을 더할 본
		FString Label;
		TArray<int32> Children;    // Nodes 인덱스
		bool bVisible = true;      // 검색 결과 (자기 또는 자손이 일치)
	};

	void BuildTree();
	TArray<int32> BuildBoneSubtree(int32 BoneIndex, const TArray<TArray<int32>>& ChildBones,
		const TArray<TArray<int32>>& ConstraintsByChildBone);
	int32 AddNode(ENodeKind Kind, int32 Index, int32 BoneIndex, const FString& Label);
	bool UpdateVisibility(int32 NodeIndex);

	void DrawOptionsMenu();
	void DrawNode(int32 NodeIndex, bool& bScrollToSelection);
	void DrawNodeContextMenu(const FTreeNode& Node);
	bool IsNodeSelected(const FTreeNode& Node) const;
	// 클릭은 그 행만, Ctrl+클릭은 넣거나 빼기, Shift+클릭은 마지막으로 누른 행부터 범위 선택
	void SelectNode(const FTreeNode& Node);
	// 행을 선택 항목으로 바꾼다. 바디 없는 본 행은 false
	bool ToSelectionItem(ENodeKind Kind, int32 NodeIndex, FPhysicsAssetSelection& OutItem) const;

	TArray<FTreeNode> Nodes;
	TArray<int32> RootNodes;

	// 화면에 그린 행 순서. Shift 범위 선택은 지난 프레임 순서를 쓴다
	struct FRowKey
	{
		ENodeKind Kind;
		int32 Index;
	};
	TArray<FRowKey> DrawOrder;
	TArray<FRowKey> LastDrawOrder;
	int32 AnchorKind = -1;    // 범위 선택의 시작 행 (마지막으로 누른 행)
	int32 AnchorIndex = -1;

	char SearchBuffer[128] = {};
	bool bShowBones = false;
	bool bShowBodies = true;
	bool bShowConstraints = false;

	// 선택이 바뀐 프레임에 그 행으로 스크롤한다
	int32 LastSelectionType = -1;
	int32 LastSelectionIndex = -1;
};
